#include "TCPClient.h"


namespace other::net::sockets
{
    // Создаёт TCP-сокет указанного семейства и применяет Dual-Stack и опции из _config.
    bool TCPClient::CreateAndConfigureSocket(int family) noexcept
    {
        if (family != AF_INET && family != AF_INET6)
        {
            SET_NET_ERROR(SOCK_EAFNOSUPPORT);
            return false;
        }

        Socket sock(family, SOCK_STREAM, IPPROTO_TCP);
        if (!sock.IsCreated())
            return false;

        // Для IPv6 включаем или отключаем Dual-Stack (IPV6_V6ONLY) согласно конфигурации.
        if (family == AF_INET6)
            sock.SetDualMode(_config.dualStack);

        _socket = std::move(sock);
        return true;
    }

    // Применяет таймауты и флаги TCP_NODELAY / SO_KEEPALIVE к активному сокету.
    void TCPClient::ApplyPostConnectOptions() noexcept
    {
        if (!_socket.IsCreated())
            return;

        if (_config.noDelay)
            _socket.SetNoDelay(true);
        if (_config.keepAlive)
            _socket.SetKeepAlive(true);
        if (_config.recvTimeoutMs >= -1)
            _socket.SetRecvTimeout(_config.recvTimeoutMs);
        if (_config.sendTimeoutMs >= -1)
            _socket.SetSendTimeout(_config.sendTimeoutMs);
    }

    // Конструктор пустого неподключённого клиента.
    TCPClient::TCPClient(const ClientConfig& config)
        : _socket()
        , _config(config)
        , _lastHost()
        , _lastPort(0)
        , _lastFamily(0)
        , _hasLastEndpoint(false)
        , _boundLocally(false)
    {
    }

    // Конструктор с автоопределением семейства (IPv6 при наличии ':' в host, иначе IPv4 с Dual-Stack).
    TCPClient::TCPClient(const std::string& host, int port, const ClientConfig& config) : TCPClient(config)
    {
        const int family = (host.find(':') != std::string::npos) ? AF_INET6 : AF_INET;
        Connect(family, host, port);
    }

    // Конструктор с явным семейством протоколов (AF_INET или AF_INET6).
    TCPClient::TCPClient(int family, const std::string& host, int port, const ClientConfig& config) : TCPClient(config)
    {
        Connect(family, host, port);
    }

    // Конструктор из готового объекта Socket (например, принятого на стороне сервера).
    TCPClient::TCPClient(Socket&& socket, const ClientConfig& config)
        : _socket(std::move(socket))
        , _config(config)
        , _lastHost()
        , _lastPort(0)
        , _lastFamily(0)
        , _hasLastEndpoint(false)
        , _boundLocally(false)
    {
        if (_socket.IsCreated())
        {
            _lastFamily = _socket.AddressFamily();
            ApplyPostConnectOptions();
        }
    }

    // Деструктор: автоматически закрывает открытое соединение.
    TCPClient::~TCPClient() noexcept
    {
        Dispose();
    }

    // Конструктор перемещения: переносит владение сокетом, конфигом и параметрами Reconnect.
    TCPClient::TCPClient(TCPClient&& other) noexcept
        : _socket(std::move(other._socket))
        , _config(other._config)
        , _lastHost(std::move(other._lastHost))
        , _lastPort(other._lastPort)
        , _lastFamily(other._lastFamily)
        , _hasLastEndpoint(other._hasLastEndpoint)
        , _boundLocally(other._boundLocally)
    {
        other._lastPort        = 0;
        other._lastFamily      = 0;
        other._hasLastEndpoint = false;
        other._boundLocally    = false;
    }

    // Оператор перемещающего присваивания: сначала закрывает текущий сокет, затем забирает состояние other.
    TCPClient& TCPClient::operator=(TCPClient&& other) noexcept
    {
        if (this != &other)
        {
            Dispose();

            _socket          = std::move(other._socket);
            _config          = other._config;
            _lastHost        = std::move(other._lastHost);
            _lastPort        = other._lastPort;
            _lastFamily      = other._lastFamily;
            _hasLastEndpoint = other._hasLastEndpoint;
            _boundLocally    = other._boundLocally;

            other._lastPort        = 0;
            other._lastFamily      = 0;
            other._hasLastEndpoint = false;
            other._boundLocally    = false;
        }
        return *this;
    }

    // Обновляет конфигурацию клиента и при необходимости применяет опции к текущему сокету.
    void TCPClient::SetConfig(const ClientConfig& config) noexcept
    {
        _config = config;
        if (_socket.IsCreated())
            ApplyPostConnectOptions();
    }

    // Подключается к хосту/порту, автоматически выбирая IPv6 (с Dual-Stack) при наличии ':' или IPv4.
    bool TCPClient::Connect(const std::string& host, int port, int timeoutMs)
    {
        const int family = (host.find(':') != std::string::npos) ? AF_INET6 : AF_INET;
        return Connect(family, host, port, timeoutMs);
    }

    // Подключается к серверу с указанным семейством адресов и таймаутом.
    bool TCPClient::Connect(int family, const std::string& host, int port, int timeoutMs)
    {
        if (host.empty() || port < 0 || port > 65535)
        {
            SET_NET_ERROR(SOCK_EINVAL);
            return false;
        }

        // Сохраняем параметры подключения для возможности Reconnect().
        _lastHost        = host;
        _lastPort        = port;
        _lastFamily      = family;
        _hasLastEndpoint = true;

        // Если сокет был привязан через BindLocal() и ещё не подключался — переиспользуем его,
        // чтобы сохранить заданный локальный порт/адрес. Во всех остальных случаях (включая Reconnect)
        // закрываем старый сокет, чтобы не получить ошибку 10056 (WSAEISCONN) на уже задействованном дескрипторе.
        if (_socket.IsCreated() && !_boundLocally)
            _socket.Close();

        if (!_socket.IsCreated())
        {
            if (!CreateAndConfigureSocket(family))
                return false;
        }

        _socket.SetReuseAddress(true);

        const int effectiveTimeout = (timeoutMs == -2) ? _config.connectTimeoutMs : timeoutMs;

        bool connected = false;
        if (effectiveTimeout >= 0)
            connected = _socket.Connect(host, port, effectiveTimeout);
        else
            connected = _socket.Connect(host, port);

        _boundLocally = false;

        if (!connected)
        {
            const int err = GET_NET_ERROR();
            _socket.Close();
            SET_NET_ERROR(err);
            return false;
        }

        ApplyPostConnectOptions();
        return true;
    }

    // Повторно подключается к сохранённым в Connect() хосту и порту.
    bool TCPClient::Reconnect(int timeoutMs)
    {
        if (!_hasLastEndpoint || _lastHost.empty())
        {
            SET_NET_ERROR(SOCK_EDESTADDRREQ);
            return false;
        }

        const int targetFamily = (_lastFamily != 0) ? _lastFamily : AF_INET;
        return Connect(targetFamily, _lastHost, _lastPort, timeoutMs);
    }

    // Привязывает сокет клиента к конкретному локальному IP и порту перед вызовом Connect().
    bool TCPClient::BindLocal(const std::string& host, int port)
    {
        if (port < 0 || port > 65535)
        {
            SET_NET_ERROR(SOCK_EINVAL);
            return false;
        }

        if (!_socket.IsCreated())
        {
            const int family = (host.find(':') != std::string::npos) ? AF_INET6 : AF_INET;
            if (!CreateAndConfigureSocket(family))
                return false;
        }

        _socket.SetReuseAddress(true);

        if (!_socket.Bind(host, port))
            return false;

        _boundLocally = true;
        return true;
    }

    // Закрывает соединение с опциональным таймаутом SO_LINGER.
    bool TCPClient::Close(int lingerTimeoutMs) noexcept
    {
        _boundLocally = false;
        if (_socket.IsClosed())
            return false;
		
        return _socket.Close(lingerTimeoutMs);
    }

    // Полностью закрывает сокет и сбрасывает сохранённое состояние подключения.
    bool TCPClient::Dispose() noexcept
    {
        bool res = _socket.Dispose();
        _lastHost.clear();
        _lastPort        = 0;
        _lastFamily      = 0;
        _hasLastEndpoint = false;
        _boundLocally    = false;
        return res;
    }

    // Отправляет до size байт за один системный вызов send().
    int TCPClient::Send(const char* data, int size, int flags)
    {
        if (!_socket.IsCreated())
        {
            SET_NET_ERROR(SOCK_EBADF);
            return -1;
        }
		
        return _socket.Send(data, size, flags);
    }

    // Гарантированно отправляет ровно size байт в цикле.
    bool TCPClient::SendAll(const char* data, int size, int flags)
    {
        if (!_socket.IsCreated())
        {
            SET_NET_ERROR(SOCK_EBADF);
            return false;
        }
		
        return _socket.SendAll(data, size, flags);
    }

    // Гарантированно отправляет строку std::string без завершающего '\0'.
    bool TCPClient::SendString(const std::string& text)
    {
        if (!_socket.IsCreated())
        {
            SET_NET_ERROR(SOCK_EBADF);
            return false;
        }
		
        if (text.empty())
            return true;
		
        if (text.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
        {
            SET_NET_ERROR(SOCK_EMSGSIZE);
            return false;
        }
		
        return _socket.SendAll(text.data(), static_cast<int>(text.size()));
    }

    // Гарантированно отправляет сырое содержимое ByteBuffer (без заголовка длины).
    bool TCPClient::SendBuffer(const ByteBuffer& buffer)
    {
        if (!_socket.IsCreated())
        {
            SET_NET_ERROR(SOCK_EBADF);
            return false;
        }
		
        if (buffer.Empty())
            return true;
		
        if (buffer.Size() > static_cast<size_t>(std::numeric_limits<int>::max()))
        {
            SET_NET_ERROR(SOCK_EMSGSIZE);
            return false;
        }
		
        return _socket.SendAll(reinterpret_cast<const char*>(buffer.Data()), static_cast<int>(buffer.Size()));
    }

    // Отправляет ByteBuffer как пакет: 4 байта длины (Big-Endian) + полезная нагрузка.
    bool TCPClient::SendPacket(const ByteBuffer& packet)
    {
        if (!_socket.IsCreated())
        {
            SET_NET_ERROR(SOCK_EBADF);
            return false;
        }
		
        if (packet.Size() > static_cast<size_t>(std::numeric_limits<int>::max()))
        {
            SET_NET_ERROR(SOCK_EMSGSIZE);
            return false;
        }

        // Переводим длину в сетевой порядок байтов (Big-Endian), чтобы пакет корректно читался.
        const uint32_t netSize = htonl(static_cast<uint32_t>(packet.Size()));
        if (!_socket.SendAll(reinterpret_cast<const char*>(&netSize), sizeof(netSize)))
            return false;

        if (!packet.Empty())
        {
            if (!_socket.SendAll(reinterpret_cast<const char*>(packet.Data()), static_cast<int>(packet.Size())))
                return false;
        }
		
        return true;
    }

    // Принимает до size байт из соединения (0 = EOF, -1 с SOCK_EWOULDBLOCK = нет данных на неблокирующем сокете).
    int TCPClient::Recv(char* buffer, int size, int flags)
    {
        if (!_socket.IsCreated())
        {
            SET_NET_ERROR(SOCK_EBADF);
            return -1;
        }
		
        return _socket.Recv(buffer, size, flags);
    }

    // Гарантированно вычитывает ровно size байт в цикле.
    bool TCPClient::RecvAll(char* buffer, int size, int flags)
    {
        if (!_socket.IsCreated())
        {
            SET_NET_ERROR(SOCK_EBADF);
            return false;
        }
		
        return _socket.RecvAll(buffer, size, flags);
    }

    // Гарантированно вычитывает ровно size байт и возвращает их как std::string.
    std::string TCPClient::RecvString(int size)
    {
        if (size <= 0)
        {
            SET_NET_ERROR(SOCK_EINVAL);
            return "";
        }
		
        if (!_socket.IsCreated())
        {
            SET_NET_ERROR(SOCK_EBADF);
            return "";
        }

        std::string result(static_cast<size_t>(size), '\0');
        if (!_socket.RecvAll(result.data(), size))
            return "";
		
        return result;
    }

    // Принимает пакет с 4-байтным Big-Endian заголовком длины и записывает тело в outPacket.
    bool TCPClient::RecvPacket(ByteBuffer& outPacket, uint32_t maxPacketSize)
    {
        outPacket.Clear();

        if (!_socket.IsCreated())
        {
            SET_NET_ERROR(SOCK_EBADF);
            return false;
        }

        const uint32_t effectiveLimit = static_cast<uint32_t>((maxPacketSize > 0) ? maxPacketSize : _config.maxPacketSize);

        // вычитываем 4-байтный заголовок длины пакета.
        uint32_t netSize = 0;
        if (!_socket.RecvAll(reinterpret_cast<char*>(&netSize), sizeof(netSize)))
            return false;

        // декодируем длину из Big-Endian и сверяем с лимитом (защита от DoS / OOM).
        const uint32_t payloadSize = ntohl(netSize);
        if (payloadSize > effectiveLimit || payloadSize > static_cast<uint32_t>(std::numeric_limits<int>::max()))
        {
            SET_NET_ERROR(SOCK_EMSGSIZE);
            return false;
        }

        if (payloadSize == 0)
            return true;

        //выделяем буфер под полезную нагрузку и вычитываем её целиком.
        outPacket.Resize(payloadSize);
        if (!_socket.RecvAll(reinterpret_cast<char*>(outPacket.Data()), static_cast<int>(payloadSize)))
        {
            outPacket.Clear();
            return false;
        }

        // ставим курсор чтения в начало буфера для удобного чтения полей.
        outPacket.Rewind();
        return true;
    }

    bool TCPClient::Connected() const noexcept               { return _socket.IsConnected(); }
    bool TCPClient::Pending(long timeoutMs) const noexcept   { return _socket.PollRead(timeoutMs); }
    bool TCPClient::PollRead(long timeoutMs) const noexcept  { return _socket.PollRead(timeoutMs); }
    bool TCPClient::PollWrite(long timeoutMs) const noexcept { return _socket.PollWrite(timeoutMs); }
    int  TCPClient::Available() const                        { return _socket.Available(); }

    std::string TCPClient::LocalAddress(bool includePort) const  { return _socket.LocalAddress(includePort); }
    std::string TCPClient::RemoteAddress(bool includePort) const { return _socket.RemoteAddress(includePort); }
    int         TCPClient::LocalPort() const                     { return _socket.LocalPort(); }
    int         TCPClient::RemotePort() const                    { return _socket.RemotePort(); }

    // Передаёт владение внутренним сокетом в StreamSocket (сам TCPClient при этом закрывается).
    StreamSocket TCPClient::SockStream() noexcept
    {
        _boundLocally = false;
        return StreamSocket(std::move(_socket));
    }
}
