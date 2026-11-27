#include "TCPServer.h"


namespace other::net::sockets
{
	
	// Возвращает количество непрочитанных/неотправленных байтов в `sendQueue`.
    size_t ClientEntry::QueuedBytes() const noexcept { return (sendOffset < sendQueue.size()) ? (sendQueue.size() - sendOffset) : 0; }
			
    
    // Создаёт неактивный сервер с заданной конфигурацией и настраивает размер батча в SocketSelector.
    TCPServer::TCPServer(const ServerConfig& config)
        : _socket()
        , _selector(0, config.maxEventsPerPoll)
        , _config(config)
        , _isListening(false)
        , _listenerRegistered(false)
        , _stopRequested(false)
        , _nextClientId(0)
        , _stats()
    {
    }

    // Создаёт сервер и сразу запускает его на всех интерфейсах ("0.0.0.0" для IPv4 или "::" для IPv6).
    TCPServer::TCPServer(int family, int port, const ServerConfig& config) : TCPServer(config)
    {
        Start(port, (family == AF_INET6 ? "::" : "0.0.0.0"), _config.backlog, family);
    }

    // Создаёт сервер и сразу запускает его на конкретном интерфейсе (host) и порту.
    TCPServer::TCPServer(int family, const std::string& host, int port, const ServerConfig& config) : TCPServer(config)
    {
        Start(port, host, _config.backlog, family);
    }

    // Деструктор: гарантированно закрывает все клиентские соединения и слушающий сокет (RAII).
    TCPServer::~TCPServer() noexcept
    {
        try
        {
            Stop();
        }
        catch (...)
        {
            // Гасим исключение из пользовательского _onDisconnected, чтобы не вызвать std::terminate().
        }
    }

    // Конструктор перемещения: переносит все ресурсы и актуализирует указатель &_socket в _selector.
    TCPServer::TCPServer(TCPServer&& other) noexcept
        : _socket(std::move(other._socket))
        , _selector(std::move(other._selector))
        , _config(other._config)
        , _isListening(other._isListening)
        , _listenerRegistered(other._listenerRegistered)
        , _stopRequested(other._stopRequested.load(std::memory_order_relaxed))
        , _nextClientId(other._nextClientId)
        , _clients(std::move(other._clients))
        , _handleToClient(std::move(other._handleToClient))
        , _readBuffer(std::move(other._readBuffer))
        , _stats(other._stats)
        , _onConnected(std::move(other._onConnected))
        , _onData(std::move(other._onData))
        , _onDisconnected(std::move(other._onDisconnected))
        , _onError(std::move(other._onError))
    {
        // Обновляем указатель на слушающий сокет в _selector; адреса ClientEntry::socket в unordered_map остаются валидными.
        if (_listenerRegistered && _socket.IsCreated())
            _selector.Modify(_socket, true, false);

        other._isListening        = false;
        other._listenerRegistered = false;
        other._stopRequested.store(false, std::memory_order_relaxed);
        other._nextClientId       = 0;
        other._stats              = ServerStats{};
    }

    // Оператор перемещающего присваивания: сначала останавливает текущий сервер, затем забирает ресурсы other.
    TCPServer& TCPServer::operator=(TCPServer&& other) noexcept
    {
        if (this != &other)
        {
            try { Stop(); } catch (...) {}

            _socket             = std::move(other._socket);
            _selector           = std::move(other._selector);
            _config             = other._config;
            _isListening        = other._isListening;
            _listenerRegistered = other._listenerRegistered;
            _stopRequested.store(other._stopRequested.load(std::memory_order_relaxed), std::memory_order_relaxed);
            _nextClientId       = other._nextClientId;
            _clients            = std::move(other._clients);
            _handleToClient     = std::move(other._handleToClient);
            _readBuffer         = std::move(other._readBuffer);
            _stats              = other._stats;
            _onConnected        = std::move(other._onConnected);
            _onData             = std::move(other._onData);
            _onDisconnected     = std::move(other._onDisconnected);
            _onError            = std::move(other._onError);

            if (_listenerRegistered && _socket.IsCreated())
                _selector.Modify(_socket, true, false);

            other._isListening        = false;
            other._listenerRegistered = false;
            other._stopRequested.store(false, std::memory_order_relaxed);
            other._nextClientId       = 0;
            other._stats              = ServerStats{};
        }
        return *this;
    }
	
	// Возвращает текущую конфигурацию сервера.
	const ServerConfig& TCPServer::Config() const noexcept { return _config; }
	
    // Обновляет конфигурацию сервера и размер батча событий в SocketSelector.
    void TCPServer::SetConfig(const ServerConfig& config)
    {
        _config = config;
        _selector.SetMaxEventsPerPoll(_config.maxEventsPerPoll);
    }

    // Заранее резервирует память в _selector и картах клиентов под capacity одновременных соединений.
    void TCPServer::Reserve(size_t capacity)
    {
        // Резервируем на 1 слот больше, чтобы вместить сам слушающий сокет _socket.
        _selector.Reserve(capacity + 1U);
        _clients.reserve(capacity);
        _handleToClient.reserve(capacity);
    }
	
	// Устанавливает обработчик подключения нового клиента (вызывается в `PollOnce()` / `Run()`).
    void TCPServer::SetOnClientConnected(OnConnectedCallback callback) noexcept { _onConnected = std::move(callback); }
    // Устанавливает обработчик входящих данных от клиента (вызывается в `PollOnce()` / `Run()`).
    void TCPServer::SetOnClientData(OnDataCallback callback) noexcept { _onData = std::move(callback); }
    // Устанавливает обработчик отключения клиента (вызывается при разрыве, ошибке или `DisconnectClient()`).
    void TCPServer::SetOnClientDisconnected(OnDisconnectedCallback callback) noexcept { _onDisconnected = std::move(callback); }
    // Устанавливает обработчик системных ошибок сервера.
    void TCPServer::SetOnError(OnErrorCallback callback) noexcept { _onError = std::move(callback); }
	
    // Вызывает пользовательский колбэк ошибок, если он задан.
    void TCPServer::NotifyError(int errorCode, const std::string& context) const
    {
        if (_onError)
            _onError(errorCode, context);
    }

    // Переводит слушающий сокет в неблокирующий режим и добавляет его в _selector при первом PollOnce().
    bool TCPServer::EnsureListenerInSelector() noexcept
    {
        if (!_isListening || _socket.IsClosed())
        {
            SET_NET_ERROR(SOCK_EBADF);
            return false;
        }

        if (_listenerRegistered)
            return true;

        // В Event Loop слушающий сокет должен быть неблокирующим, чтобы HandleAccept() не заблокировал поток.
        if (_socket.IsBlocking() && !_socket.SetBlocking(false))
            return false;

        if (!_selector.Add(_socket, true, false))
            return false;

        _listenerRegistered = true;
        return true;
    }

    // Принимает все накопившиеся входящие TCP-подключения в цикле до SOCK_EWOULDBLOCK.
    void TCPServer::HandleAccept()
    {
        while (_isListening && _socket.IsCreated())
        {
            Socket clientSock = _socket.Accept();
            if (!clientSock.IsCreated())
            {
                const int err = GET_NET_ERROR();
				
                // Очередь входящих соединений полностью опустошена.
                if (err == SOCK_EWOULDBLOCK)
                    break;
				
                if (err == SOCK_EINTR)
                    continue;

                NotifyError(err, "TCPServer::HandleAccept: accept failed");
                break;
            }

            // Проверяем лимит одновременных подключений (защита от перегрузки).
            if (_config.maxClients > 0 && _clients.size() >= _config.maxClients)
            {
                ++_stats.totalRejected;
                clientSock.Close();
                NotifyError(SOCK_ENOBUFS, "TCPServer::HandleAccept: maxClients limit reached");
                continue;
            }

            // Переводим клиентский сокет в неблокирующий режим и применяем опции из ServerConfig.
            if (!clientSock.SetBlocking(false))
            {
                const int err = GET_NET_ERROR();
                ++_stats.totalRejected;
                clientSock.Close();
                NotifyError(err, "TCPServer::HandleAccept: failed to set non-blocking mode");
                continue;
            }

            if (_config.noDelay)
                clientSock.SetNoDelay(true);
			
            if (_config.keepAlive)
                clientSock.SetKeepAlive(true);

            const socket_t    clientHandle = clientSock.SockHandle();
            const std::string clientIp     = clientSock.RemoteAddress(false);
            const int         clientPort   = clientSock.RemotePort();
            const ClientId    newId        = ++_nextClientId;

            // Создаём запись клиента (время создания и счётчики байтов уже ведутся внутри clientSock).
            ClientEntry entry;
            entry.id     = newId;
            entry.socket = std::move(clientSock);
            entry.ip     = clientIp;
            entry.port   = clientPort;

            auto [it, inserted] = _clients.emplace(newId, std::move(entry));
            if (!inserted)
            {
                ++_stats.totalRejected;
                continue;
            }

            // Регистрируем дескриптор в _handleToClient и _selector с откатом при ошибке.
            try
            {
                _handleToClient.emplace(clientHandle, newId);
            }
            catch (...)
            {
                _clients.erase(it);
                throw;
            }

            if (!_selector.Add(it->second.socket, true, false))
            {
                const int err = GET_NET_ERROR();
                _handleToClient.erase(clientHandle);
                _clients.erase(it);
                ++_stats.totalRejected;
                NotifyError(err, "TCPServer::HandleAccept: SocketSelector::Add failed");
                continue;
            }

            ++_stats.totalAccepted;

            if (_onConnected)
                _onConnected(newId, clientIp, clientPort);
        }
    }

    // Вычитывает доступные данные от клиента в общий _readBuffer и вызывает _onData.
    void TCPServer::HandleClientRead(ClientId id)
    {
        const size_t chunkSize = (_config.readChunkSize > 0) ? _config.readChunkSize : 4096U;
        if (_readBuffer.size() < chunkSize)
            _readBuffer.resize(chunkSize);

        while (true)
        {
            // Проверяем наличие клиента на каждой итерации: колбэк _onData мог закрыть это соединение.
            auto it = _clients.find(id);
            if (it == _clients.end())
                return;

            ClientEntry& entry = it->second;
            int n = entry.socket.Recv(reinterpret_cast<char*>(_readBuffer.data()), static_cast<int>(chunkSize));
            if (n > 0)
            {
                _stats.totalBytesReceived += static_cast<uint64_t>(n);

                if (_onData)
                    _onData(id, _readBuffer.data(), static_cast<size_t>(n));

                // Если вернулось меньше полного чанка, в буфере ядра больше ничего нет — избегаем лишнего Recv().
                if (static_cast<size_t>(n) < chunkSize)
                    break;
            }
            else if (n == 0)
            {
                // Штатное закрытие соединения удалённой стороной (EOF).
                CloseClientInternal(id, 0);
                return;
            }
            else
            {
                const int err = GET_NET_ERROR();
                if (err == SOCK_EWOULDBLOCK)
                    break;
                if (err == SOCK_EINTR)
                    continue;

                CloseClientInternal(id, err);
                return;
            }
        }
    }

    // Досылает отложенные байты из sendQueue при готовности сокета к записи.
    void TCPServer::HandleClientWrite(ClientId id) noexcept
    {
        auto it = _clients.find(id);
        if (it == _clients.end())
            return;

        ClientEntry& entry = it->second;

        while (entry.QueuedBytes() > 0)
        {
            const size_t remaining = entry.QueuedBytes();
            const int    toSend    = (remaining > static_cast<size_t>(std::numeric_limits<int>::max())) ? std::numeric_limits<int>::max() : static_cast<int>(remaining);

            int sent = entry.socket.Send(reinterpret_cast<const char*>(entry.sendQueue.data() + entry.sendOffset), toSend);
            if (sent > 0)
            {
                entry.sendOffset      += static_cast<size_t>(sent);
                _stats.totalBytesSent += static_cast<uint64_t>(sent);
            }
            else if (sent == 0)
            {
                // Буфер отправки ОС заполнен — дождёмся следующего события Write.
                break;
            }
            else
            {
                const int err = GET_NET_ERROR();
                if (err == SOCK_EINTR)
                    continue;

                try 
				{ 
				    CloseClientInternal(id, err); 
				} 
				catch (...) 
				{
				}
				
                return;
            }
        }

        // Если очередь полностью отправлена — очищаем буфер и при необходимости отключаем watchWrite или закрываем сокет.
        if (entry.QueuedBytes() == 0)
        {
            entry.sendQueue.clear();
            entry.sendOffset = 0;

            if (entry.pendingClose)
            {
                try 
				{ 
				    CloseClientInternal(id, 0); 
				} 
				catch (...) 
				{
				}
				
                return;
            }

            if (entry.watchingWrite)
            {
                _selector.Modify(entry.socket, true, false);
                entry.watchingWrite = false;
            }
        }
    }

    // Удаляет клиента из селектора и карт, закрывает сокет и уведомляет _onDisconnected.
    void TCPServer::CloseClientInternal(ClientId id, int errorCode)
    {
        auto it = _clients.find(id);
        if (it == _clients.end())
            return;

        const socket_t handle = it->second.socket.SockHandle();

        // Снимаем наблюдение до закрытия сокета, чтобы дескриптор не успел переиспользоваться ядром.
        _selector.Remove(handle);
        _handleToClient.erase(handle);
        it->second.socket.Close();
        _clients.erase(it);

        ++_stats.totalDisconnected;

        if (_onDisconnected)
            _onDisconnected(id, errorCode);
    }

    // Запускает прослушивание на уже созданном и привязанном сокете с размером очереди из _config.
    bool TCPServer::Start() noexcept
    {
        return Start(_config.backlog > 0 ? _config.backlog : SOMAXCONN);
    }

    // Запускает прослушивание на уже созданном и привязанном сокете с заданным backlog.
    bool TCPServer::Start(int backlog) noexcept
    {
        if (!_socket.IsCreated() || _isListening)
        {
            SET_NET_ERROR(_isListening ? SOCK_EISCONN : SOCK_EBADF);
            return false;
        }

        const int effectiveBacklog = (backlog > 0) ? backlog : SOMAXCONN;
        if (!_socket.Listen(effectiveBacklog))
            return false;

        _isListening        = true;
        _listenerRegistered = false;
        _stopRequested.store(false, std::memory_order_relaxed);
        _stats.startedAt    = other::time::DateTime::Now();
        return true;
    }

    // Универсальный запуск: создаёт сокет, настраивает опции из _config, выполняет Bind и Listen.
    bool TCPServer::Start(int port, const std::string& address, int backlog, int family)
    {
        if (_isListening || _socket.IsCreated())
            Stop();

        // Если семейство не задано явно (-1), определяем IPv6 по наличию ':' в адресе.
        if (family == -1)
            family = (address.find(':') != std::string::npos) ? AF_INET6 : AF_INET;

        _socket = Socket(family, SOCK_STREAM, IPPROTO_TCP);
        if (!_socket.IsCreated())
        {
            NotifyError(GET_NET_ERROR(), "TCPServer::Start: socket creation failed");
            return false;
        }

        if (family == AF_INET6)
            _socket.SetDualMode(_config.dualStack);

        if (_config.reuseAddress)
            _socket.SetReuseAddress(true);

        if (_config.exclusiveAddressUse)
            _socket.SetExclusiveAddressUse(true);

        if (!_socket.Bind(address, port))
        {
            const int err = GET_NET_ERROR();
            _socket.Close();
            SET_NET_ERROR(err);
            NotifyError(err, "TCPServer::Start: bind failed");
            return false;
        }

        const int effectiveBacklog = (backlog > 0) ? backlog : (_config.backlog > 0 ? _config.backlog : SOMAXCONN);
        if (!_socket.Listen(effectiveBacklog))
        {
            const int err = GET_NET_ERROR();
            _socket.Close();
            SET_NET_ERROR(err);
            NotifyError(err, "TCPServer::Start: listen failed");
            return false;
        }

        _isListening        = true;
        _listenerRegistered = false;
        _stopRequested.store(false, std::memory_order_relaxed);
        _stats              = ServerStats{};
        _stats.startedAt    = other::time::DateTime::Now();
        return true;
    }

    // Останавливает сервер: отключает всех клиентов, очищает селектор и закрывает слушающий сокет.
    bool TCPServer::Stop()
    {
        _stopRequested.store(true, std::memory_order_relaxed);

        DisconnectAll();

        _selector.Clear();
        _listenerRegistered = false;

        bool success = true;
        if (_socket.IsCreated())
            success = _socket.Close();

        _isListening = false;
        return success;
    }

    // Потокобезопасно запрашивает выход из блокирующего цикла Run().
    void TCPServer::RequestStop() noexcept
    {
        _stopRequested.store(true, std::memory_order_relaxed);
    }

    // Выполняет одну итерацию событийного цикла сервера через SocketSelector.
    int TCPServer::PollOnce(int timeoutMs)
    {
        if (!EnsureListenerInSelector())
            return -1;

        int ready = _selector.Poll(timeoutMs);
        if (ready <= 0)
        {
            if (ready < 0)
                NotifyError(GET_NET_ERROR(), "TCPServer::PollOnce: SocketSelector::Poll failed");
			
            return ready;
        }

        const socket_t listenHandle = _socket.SockHandle();

        // Обходим только сработавшие события за O(K); удаление клиентов внутри цикла безопасно.
        for (const auto& ev : _selector.ReadyEvents())
        {
            if (ev.handle == listenHandle)
            {
                if (ev.IsReadable())
                    HandleAccept();
                else if (ev.HasCriticalError())
                    NotifyError(_socket.SockError(), "TCPServer::PollOnce: listener socket error");
                continue;
            }

            auto mapIt = _handleToClient.find(ev.handle);
            if (mapIt == _handleToClient.end())
                continue;

            const ClientId clientId = mapIt->second;

            // Сначала вычитываем доступные данные, чтобы не потерять хвост перед закрытием.
            if (ev.IsReadable())
                HandleClientRead(clientId);

            auto clientIt = _clients.find(clientId);
            if (clientIt == _clients.end())
                continue;

            if (ev.HasCriticalError())
            {
                const int sockErr = clientIt->second.socket.SockError();
                CloseClientInternal(clientId, sockErr > 0 ? sockErr : SOCK_ECONNRESET);
                continue;
            }

            if (ev.IsWritable())
                HandleClientWrite(clientId);
        }

        return ready;
    }

    // Запускает непрерывный цикл обработки событий до RequestStop() или Stop().
    bool TCPServer::Run(int pollTimeoutMs)
    {
        if (!_isListening || _socket.IsClosed())
        {
            SET_NET_ERROR(SOCK_EBADF);
            return false;
        }

        _stopRequested.store(false, std::memory_order_relaxed);

        while (!_stopRequested.load(std::memory_order_relaxed) && _isListening)
        {
            int res = PollOnce(pollTimeoutMs);
            if (res < 0 && GET_NET_ERROR() != SOCK_EINTR)
                break;
        }

        return true;
    }

    // Проверяет наличие ожидающих подключений через Socket::PollRead().
    bool TCPServer::Pending(int timeoutMs) const noexcept
    {
        if (!_isListening || _socket.IsClosed())
            return false;
		
        return _socket.PollRead(timeoutMs);
    }

    // Принимает входящего клиента как сырой Socket с применением опций из _config.
    Socket TCPServer::AcceptSocket() noexcept
    {
        if (!_isListening || _socket.IsClosed())
        {
            SET_NET_ERROR(SOCK_EBADF);
            return Socket();
        }

        Socket clientSock = _socket.Accept();
        if (clientSock.IsCreated())
        {
            if (_config.noDelay)
                clientSock.SetNoDelay(true);
			
            if (_config.keepAlive)
                clientSock.SetKeepAlive(true);
			
            ++_stats.totalAccepted;
        }
		
        return clientSock;
    }

    // Принимает входящего клиента и возвращает его IP и порт.
    Socket TCPServer::Accept(std::string& outClientIp, int& outClientPort)
    {
        Socket clientSocket = AcceptSocket();
        if (!clientSocket.IsCreated())
        {
            outClientIp.clear();
            outClientPort = 0;
            return Socket();
        }

        outClientIp   = clientSocket.RemoteAddress(false);
        outClientPort = clientSocket.RemotePort();
        return clientSocket;
    }

    // Принимает входящего клиента и оборачивает его в потоковый StreamSocket.
    StreamSocket TCPServer::AcceptStream() noexcept
    {
        return StreamSocket(AcceptSocket());
    }

    // Неблокирующе отправляет сырые байты клиенту; остаток помещает в sendQueue.
    bool TCPServer::Send(ClientId id, const char* data, size_t size)
    {
        if (data == nullptr || size == 0)
        {
            SET_NET_ERROR(SOCK_EINVAL);
            return false;
        }

        auto it = _clients.find(id);
        if (it == _clients.end())
        {
            SET_NET_ERROR(SOCK_ENOENT);
            return false;
        }

        ClientEntry& entry = it->second;
        if (entry.pendingClose)
        {
            SET_NET_ERROR(SOCK_ENOTCONN);
            return false;
        }

        const size_t queued = entry.QueuedBytes();
        if (_config.maxClientSendQueue > 0 && (size > _config.maxClientSendQueue || queued > _config.maxClientSendQueue - size))
        {
            SET_NET_ERROR(SOCK_ENOBUFS);
            return false;
        }

        size_t offset = 0;

        // Если очередь пуста — пробуем отправить данные напрямую без буферизации.
        if (queued == 0)
        {
            while (offset < size)
            {
                const size_t rem    = size - offset;
                const int    toSend = (rem > static_cast<size_t>(std::numeric_limits<int>::max())) ? std::numeric_limits<int>::max() : static_cast<int>(rem);

                int sent = entry.socket.Send(data + offset, toSend);
                if (sent > 0)
                {
                    offset                += static_cast<size_t>(sent);
                    _stats.totalBytesSent += static_cast<uint64_t>(sent);
                }
                else if (sent == 0)
                {
                    // Буфер отправки ядра заполнен — остаток уйдёт в sendQueue.
                    break;
                }
                else
                {
                    const int err = GET_NET_ERROR();
                    if (err == SOCK_EINTR)
                        continue;

                    CloseClientInternal(id, err);
                    SET_NET_ERROR(err);
                    return false;
                }
            }
        }

        // Остаток складываем в sendQueue и включаем наблюдение за Write в _selector.
        if (offset < size)
        {
            if (entry.sendOffset > 0 && entry.sendOffset >= entry.sendQueue.size() / 2U)
            {
                entry.sendQueue.erase(entry.sendQueue.begin(), entry.sendQueue.begin() + static_cast<std::ptrdiff_t>(entry.sendOffset));
                entry.sendOffset = 0;
            }

            const auto* begin = reinterpret_cast<const uint8_t*>(data + offset);
            entry.sendQueue.insert(entry.sendQueue.end(), begin, begin + (size - offset));

            if (!entry.watchingWrite)
            {
                if (_selector.Modify(entry.socket, true, true))
                    entry.watchingWrite = true;
            }
        }

        return true;
    }

    // Неблокирующе отправляет строку std::string клиенту.
    bool TCPServer::Send(ClientId id, const std::string& data)
    {
        if (data.empty())
            return HasClient(id);
		
        return Send(id, data.data(), data.size());
    }

    // Неблокирующе отправляет сырые байты ByteBuffer клиенту.
    bool TCPServer::Send(ClientId id, const ByteBuffer& buffer)
    {
        if (buffer.Empty())
            return HasClient(id);
		
        return Send(id, reinterpret_cast<const char*>(buffer.Data()), buffer.Size());
    }

    // Отправляет ByteBuffer с 4-байтным заголовком длины в Big-Endian (совместим с StreamSocket::ReadPacket).
    bool TCPServer::SendPacket(ClientId id, const ByteBuffer& packet)
    {
        if (packet.Size() > static_cast<size_t>(std::numeric_limits<int>::max()))
        {
            SET_NET_ERROR(SOCK_EMSGSIZE);
            return false;
        }

        auto it = _clients.find(id);
        if (it == _clients.end())
        {
            SET_NET_ERROR(SOCK_ENOENT);
            return false;
        }

        const size_t totalSize = sizeof(uint32_t) + packet.Size();
        if (_config.maxClientSendQueue > 0 && (totalSize > _config.maxClientSendQueue || it->second.QueuedBytes() > _config.maxClientSendQueue - totalSize))
        {
            SET_NET_ERROR(SOCK_ENOBUFS);
            return false;
        }

        const uint32_t netSize = htonl(static_cast<uint32_t>(packet.Size()));
        if (!Send(id, reinterpret_cast<const char*>(&netSize), sizeof(netSize)))
            return false;

        if (!packet.Empty())
            return Send(id, reinterpret_cast<const char*>(packet.Data()), packet.Size());

        return true;
    }

    // Рассылает сырые байты всем подключённым клиентам (кроме excludeClientId).
    size_t TCPServer::Broadcast(const char* data, size_t size, ClientId excludeClientId)
    {
        if (data == nullptr || size == 0 || _clients.empty())
            return 0;

        // Копируем список ID заранее, чтобы избежать инвалидации итераторов при закрытии клиента по ошибке.
        std::vector<ClientId> ids = GetClientIds();
        size_t sentCount = 0;

        for (ClientId cid : ids)
        {
            if (cid == excludeClientId)
                continue;
			
            if (Send(cid, data, size))
                ++sentCount;
        }

        return sentCount;
    }

    // Рассылает строку всем подключённым клиентам.
    size_t TCPServer::Broadcast(const std::string& data, ClientId excludeClientId)
    {
        if (data.empty())
            return 0;
		
        return Broadcast(data.data(), data.size(), excludeClientId);
    }

    // Рассылает пакет ByteBuffer всем подключённым клиентам.
    size_t TCPServer::BroadcastPacket(const ByteBuffer& packet, ClientId excludeClientId)
    {
        if (_clients.empty())
            return 0;

        std::vector<ClientId> ids = GetClientIds();
        size_t sentCount = 0;

        for (ClientId cid : ids)
        {
            if (cid == excludeClientId)
                continue;
			
            if (SendPacket(cid, packet))
                ++sentCount;
        }

        return sentCount;
    }

    // Отключает клиента (при graceful=true ждёт опустошения sendQueue).
    bool TCPServer::DisconnectClient(ClientId id, bool graceful)
    {
        auto it = _clients.find(id);
        if (it == _clients.end())
            return false;

        if (graceful && it->second.QueuedBytes() > 0)
        {
            it->second.pendingClose = true;
            if (!it->second.watchingWrite)
            {
                if (_selector.Modify(it->second.socket, true, true))
                    it->second.watchingWrite = true;
            }
            return true;
        }

        CloseClientInternal(id, 0);
        return true;
    }

    // Немедленно отключает всех активных клиентов.
    void TCPServer::DisconnectAll()
    {
        std::vector<ClientId> ids = GetClientIds();
        for (ClientId cid : ids)
            CloseClientInternal(cid, 0);
    }
	
	bool TCPServer::IsListening() const noexcept { return _isListening; }
	
    // Проверяет режим прослушивания напрямую в ядре ОС через SO_ACCEPTCONN.
    bool TCPServer::Active() const noexcept
    {
        if (!_isListening || _socket.IsClosed())
            return false;

        return _socket.SockAccept() != -1;
    }

    std::string TCPServer::LocalAddress(bool includePort) const { return _socket.LocalAddress(includePort); }
    int         TCPServer::LocalPort() const                    { return _socket.LocalPort(); }

    bool TCPServer::HasClient(ClientId id) const noexcept
    {
        return _clients.find(id) != _clients.end();
    }

    // Возвращает константный указатель на внутренний Socket активного клиента (или nullptr).
    const Socket* TCPServer::GetClientSocket(ClientId id) const noexcept
    {
        auto it = _clients.find(id);
        if (it == _clients.end())
            return nullptr;
		
        return &it->second.socket;
    }

    // Заполняет структуру ClientInfo актуальными данными клиента, включая StartTime и SockUptime.
    bool TCPServer::GetClientInfo(ClientId id, ClientInfo& outInfo) const
    {
        auto it = _clients.find(id);
        if (it == _clients.end())
            return false;

        const ClientEntry& e  = it->second;
        outInfo.id            = e.id;
        outInfo.handle        = e.socket.SockHandle();
        outInfo.ip            = e.ip;
        outInfo.port          = e.port;
        outInfo.connectedAt   = e.socket.StartTime();
        outInfo.uptime        = e.socket.SockUptime();
        outInfo.bytesSent     = e.socket.BytesSent();
        outInfo.bytesReceived = e.socket.BytesRecv();
        outInfo.queuedToSend  = e.QueuedBytes();
        outInfo.pendingClose  = e.pendingClose;
        return true;
    }

    // Возвращает вектор идентификаторов всех текущих клиентов.
    std::vector<ClientId> TCPServer::GetClientIds() const
    {
        std::vector<ClientId> ids;
        ids.reserve(_clients.size());
        for (const auto& [cid, _] : _clients)
            ids.push_back(cid);
        return ids;
    }
	
	// Возвращает текущее количество подключённых клиентов в Event Loop.
	size_t TCPServer::ClientCount() const noexcept { return _clients.size(); }

    // Возвращает актуальный снимок статистики сервера.
    ServerStats TCPServer::Stats() const noexcept
    {
        ServerStats snapshot = _stats;
        snapshot.activeClients = _clients.size();
        return snapshot;
    }
	
	// Возвращает время последнего запуска сервера.
	other::time::DateTime TCPServer::StartTime() const noexcept { return _stats.startedAt; }
	
    // Возвращает длительность работы сервера с момента запуска.
    other::time::DateTime TCPServer::Uptime() const noexcept
    {
        if (!_isListening || _stats.startedAt.Ticks() == 0)
            return other::time::DateTime::Zero;
		
        return other::time::DateTime::Now() - _stats.startedAt;
    }

    bool TCPServer::SetExclusiveAddressUse(bool exclusive) noexcept { return _socket.SetExclusiveAddressUse(exclusive); }
    bool TCPServer::IsExclusiveAddressUse() const noexcept          { return _socket.IsExclusiveAddressUse(); }
	
	// Возвращает ссылку на `Socket`.
    Socket& TCPServer::SockInternal() noexcept { return _socket; }
    const Socket& TCPServer::SockInternal() const noexcept { return _socket; }
	
    // Возвращает ссылку на `SocketSelector`.
	SocketSelector& TCPServer::SockSelector() noexcept { return _selector; }
    const SocketSelector& TCPServer::SockSelector() const noexcept { return _selector; }
}
