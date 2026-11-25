#include "Socket.h"
#include "private\PrivateNet.hpp"


// Глобальный статический объект инициализатора Winsock:
static other::net::sockets::WSAInitializer _wsa_init_global;


namespace other::net::sockets
{
    using namespace other::time;

    // Проверяет, поддерживает ли операционная система создание IPv4-сокетов.
    bool Socket::SupportsIPv4() noexcept
    {
        // Пробуем создать тестовый TCP-сокет семейства AF_INET (IPv4).
        socket_t result = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (result == INVALID_SOCKET)
        {
            // Сохраняем реальный код ошибки ОС (например, EAFNOSUPPORT).
            SET_NET_ERROR(GET_NET_ERROR());
            return false;
        }
        // Сразу закрываем тестовый сокет, чтобы не допустить утечки дескрипторов ОС.
        CLOSE_SOCKET(result);
        return true;
    }

    // Проверяет, поддерживает ли операционная система создание IPv6-сокетов.
    bool Socket::SupportsIPv6() noexcept
    {
        // Пробуем создать тестовый TCP-сокет семейства AF_INET6 (IPv6).
        socket_t result = ::socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
        if (result == INVALID_SOCKET)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return false;
        }
        // Немедленно освобождаем тестовый дескриптор.
        CLOSE_SOCKET(result);
        return true;
    }

    // Восстанавливает метаданные (_type, _family, _protocol, _blocking) по уже существующему дескриптору ОС.
    bool Socket::InitFromHandle(socket_t handle) noexcept
    {
        // Нельзя привязать пустой/невалидный дескриптор.
        if (handle == INVALID_SOCKET)
        {
            SET_NET_ERROR(SOCK_EBADF);
            return false;
        }

        // Запрашиваем у ядра ОС тип сокета (SOCK_STREAM или SOCK_DGRAM) через опцию SO_TYPE.
        socklen_t optlen = sizeof(_type);
        if (::getsockopt(handle, SOL_SOCKET, SO_TYPE, reinterpret_cast<char*>(&_type), &optlen) == SOCKET_ERROR)
            _type = SOCK_STREAM;

        // Запрашиваем локальный адрес сокета, чтобы определить реальное семейство (AF_INET или AF_INET6).
        sockaddr_storage addr = {};
        socklen_t addrlen = sizeof(addr);
        if (::getsockname(handle, reinterpret_cast<sockaddr*>(&addr), &addrlen) != SOCKET_ERROR)
            _family = addr.ss_family;
        else
            _family = AF_INET;

#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
        // На Windows можно запросить точную структуру WSAPROTOCOL_INFO у Winsock.
        WSAPROTOCOL_INFO protoInfo = {};
        int protoLen = sizeof(protoInfo);
        if (::getsockopt(handle, SOL_SOCKET, SO_PROTOCOL_INFO, reinterpret_cast<char*>(&protoInfo), &protoLen) != SOCKET_ERROR)
        {
            _protocol = protoInfo.iProtocol;
        }
        else
#endif
        {
            // На Linux (и как запасной вариант на Windows) определяем протокол по типу сокета.
            if (_type == SOCK_STREAM)
                _protocol = IPPROTO_TCP;
            else if (_type == SOCK_DGRAM)
                _protocol = IPPROTO_UDP;
            else
                _protocol = 0;
        }

#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
        // В Windows нельзя прочитать режим FIONBIO обратно, считаем сокет блокирующим по умолчанию.
        _blocking = true;
#else
        // В Linux читаем фактическое состояние флага O_NONBLOCK из ядра ОС.
        int flags = ::fcntl(handle, F_GETFL, 0);
        _blocking = (flags == -1) ? true : ((flags & O_NONBLOCK) == 0);
#endif

        // Сохраняем системный дескриптор под управление RAII и инициализируем телеметрию.
        _handle     = handle;
        _start_t    = other::time::DateTime::Now();
        _close_t    = other::time::DateTime::Zero;
        _bytesSent  = 0;
        _bytesRecv  = 0;
        return true;
    }

    // Конструктор по умолчанию: создаёт пустой (закрытый) объект Socket без системных вызовов.
    Socket::Socket() noexcept
        : _handle(INVALID_SOCKET)
        , _family(0)
        , _type(0)
        , _protocol(0)
        , _blocking(true)
        , _start_t(other::time::DateTime::Zero)
        , _close_t(other::time::DateTime::Zero)
        , _bytesSent(0)
        , _bytesRecv(0)
    {
    }

    // Конструктор-обёртка: принимает готовый системный дескриптор (например, из Accept) и восстанавливает его поля.
    Socket::Socket(socket_t handle) noexcept
        : Socket() // Делегируем конструктору по умолчанию для гарантированной инициализации всех полей.
    {
        InitFromHandle(handle);
    }

    // Конструктор Dual-Stack: создаёт IPv6-сокет, который умеет работать одновременно с IPv4 и IPv6.
    Socket::Socket(int type, int protocol) noexcept
        : Socket(AF_INET6, type, protocol)
    {
        // Если сокет создался, но режим Dual-Stack выключен — включаем его (IPV6_V6ONLY = 0).
        if (!IsClosed() && !IsDualMode())
            SetDualMode(true);
    }

    // Основной конструктор: запрашивает у ОС новый физический сокет с указанными параметрами.
    Socket::Socket(int family, int type, int protocol) noexcept
        : Socket()
    {
        socket_t handle = ::socket(family, type, protocol);
        if (handle != INVALID_SOCKET)
        {
            _handle   = handle;
            _family   = family;
            _type     = type;
            _protocol = protocol;
            _blocking = true;
            _start_t  = other::time::DateTime::Now();
        }
        else
        {
            SET_NET_ERROR(GET_NET_ERROR());
        }
    }

    // Конструктор перемещения: переносит владение сокетом из other в новый объект без закрытия соединения.
    Socket::Socket(Socket&& other) noexcept
        : _handle(other._handle)
        , _family(other._family)
        , _type(other._type)
        , _protocol(other._protocol)
        , _blocking(other._blocking)
        , _start_t(other._start_t)
        , _close_t(other._close_t)
        , _bytesSent(other._bytesSent)
        , _bytesRecv(other._bytesRecv)
    {
        // Обязательно обнуляем источник, иначе его деструктор закроет перенесённый дескриптор.
        other._handle    = INVALID_SOCKET;
        other._family    = 0;
        other._type      = 0;
        other._protocol  = 0;
        other._blocking  = true;
        other._start_t   = other::time::DateTime::Zero;
        other._close_t   = other::time::DateTime::Zero;
        other._bytesSent = 0;
        other._bytesRecv = 0;
    }

    // Оператор перемещающего присваивания: закрывает текущий сокет и забирает дескриптор у other.
    Socket& Socket::operator=(Socket&& other) noexcept
    {
        if (this != &other)
        {
            // Сначала закрываем наш текущий сокет во избежание утечки дескриптора.
            Dispose();

            _handle    = other._handle;
            _family    = other._family;
            _type      = other._type;
            _protocol  = other._protocol;
            _blocking  = other._blocking;
            _start_t   = other._start_t;
            _close_t   = other._close_t;
            _bytesSent = other._bytesSent;
            _bytesRecv = other._bytesRecv;

            // Сбрасываем источник в безопасное пустое состояние.
            other._handle    = INVALID_SOCKET;
            other._family    = 0;
            other._type      = 0;
            other._protocol  = 0;
            other._blocking  = true;
            other._start_t   = other::time::DateTime::Zero;
            other._close_t   = other::time::DateTime::Zero;
            other._bytesSent = 0;
            other._bytesRecv = 0;
        }
        return *this;
    }

    // Деструктор: гарантирует автоматическое закрытие сокета при выходе из области видимости.
    Socket::~Socket() noexcept
    {
        Dispose();
    }

    // Немедленное закрытие сокета без ожидания.
    bool Socket::Close() noexcept
    {
        return Close(0);
    }

    // Закрывает сокет; при timeout > 0 (в мс) включает SO_LINGER, чтобы ядро успело дослать остаток буфера.
    bool Socket::Close(int timeout) noexcept
    {
        if (IsClosed())
        {
            SET_NET_ERROR(SOCK_EBADF);
            return false;
        }

        if (timeout > 0)
        {
            // Округляем миллисекунды вверх до целых секунд без риска переполнения (timeout + 999):
            // важно, чтобы l_linger >= 1 при включённом SO_LINGER, иначе TCP разорвёт связь жёстким RST.
            int lingerSeconds = (timeout / 1000) + ((timeout % 1000) != 0 ? 1 : 0);
            SetLingerState(true, lingerSeconds);
        }

        if (CLOSE_SOCKET(_handle) == SOCKET_ERROR)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return false;
        }

        _handle  = INVALID_SOCKET;
        _close_t = other::time::DateTime::Now();
        return true;
    }

    // Простые геттеры метаданных и статистики.
    int Socket::SockType() const noexcept          { return _type; }
    int Socket::ProtocolType() const noexcept      { return _protocol; }
    int Socket::AddressFamily() const noexcept     { return _family; }
    socket_t Socket::SockHandle() const noexcept   { return _handle; }
    uint64_t Socket::BytesSent() const noexcept    { return _bytesSent; }
    uint64_t Socket::BytesRecv() const noexcept    { return _bytesRecv; }
    void Socket::ResetStats() noexcept             { _bytesSent = 0; _bytesRecv = 0; }

    // Запрашивает и очищает отложенный код ошибки сокета из ядра ОС (опция SO_ERROR).
    int Socket::SockError() const noexcept
    {
        if (!IsCreated())
            return -1;

        int err = 0;
        socklen_t len = sizeof(err);
        if (::getsockopt(_handle, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len) == SOCKET_ERROR)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return -1;
        }

        return err;
    }

    // Запрашивает у ядра ОС состояние опции SO_ACCEPTCONN (находится ли сокет в режиме listen).
    int Socket::SockAccept() const noexcept
    {
        if (!IsCreated())
            return -1;

        int acc = 0;
        socklen_t len = sizeof(acc);
        if (::getsockopt(_handle, SOL_SOCKET, SO_ACCEPTCONN, reinterpret_cast<char*>(&acc), &len) == SOCKET_ERROR)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return -1;
        }

        return acc;
    }

    // Возвращает локальный порт, на который привязан сокет (в порядке байтов процессора).
    int Socket::LocalPort() const
    {
        if (!IsCreated())
            return -1;

        sockaddr_storage addr = {};
        socklen_t addrlen = sizeof(addr);
        if (::getsockname(_handle, reinterpret_cast<sockaddr*>(&addr), &addrlen) == SOCKET_ERROR)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return -1;
        }

        if (addr.ss_family == AF_INET)
            return ntohs(reinterpret_cast<const sockaddr_in*>(&addr)->sin_port);

        if (addr.ss_family == AF_INET6)
            return ntohs(reinterpret_cast<const sockaddr_in6*>(&addr)->sin6_port);

        SET_NET_ERROR(SOCK_EAFNOSUPPORT);
        return -1;
    }

    // Возвращает порт удалённого собеседника (работает только на подключённом сокете).
    int Socket::RemotePort() const
    {
        if (!IsCreated())
            return -1;

        sockaddr_storage peerAddr = {};
        socklen_t addrLen = sizeof(peerAddr);
        if (::getpeername(_handle, reinterpret_cast<sockaddr*>(&peerAddr), &addrLen) == SOCKET_ERROR)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return -1;
        }

        if (peerAddr.ss_family == AF_INET)
            return ntohs(reinterpret_cast<const sockaddr_in*>(&peerAddr)->sin_port);

        if (peerAddr.ss_family == AF_INET6)
            return ntohs(reinterpret_cast<const sockaddr_in6*>(&peerAddr)->sin6_port);

        SET_NET_ERROR(SOCK_EAFNOSUPPORT);
        return -1;
    }

    // Проверяет, привязан ли сокет к ненулевому локальному порту.
    bool Socket::IsBound() const noexcept
    {
        if (!IsCreated())
            return false;

        sockaddr_storage addr = {};
        socklen_t addrlen = sizeof(addr);
        if (::getsockname(_handle, reinterpret_cast<sockaddr*>(&addr), &addrlen) == SOCKET_ERROR)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return false;
        }

        if (addr.ss_family == AF_INET)
            return reinterpret_cast<const sockaddr_in*>(&addr)->sin_port != 0;

        if (addr.ss_family == AF_INET6)
            return reinterpret_cast<const sockaddr_in6*>(&addr)->sin6_port != 0;

        SET_NET_ERROR(SOCK_EAFNOSUPPORT);
        return false;
    }

    // Переводит серверный TCP-сокет в режим прослушивания входящих соединений.
    bool Socket::Listen(int backlog) noexcept
    {
        if (!IsCreated())
            return false;

        if (backlog < 0)
        {
            SET_NET_ERROR(SOCK_EINVAL);
            return false;
        }

        if (::listen(_handle, backlog) == SOCKET_ERROR)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return false;
        }
        return true;
    }

    // Извлекает одного входящего клиента из очереди и возвращает отдельный Socket для общения с ним.
    Socket Socket::Accept() noexcept
    {
        if (!IsCreated())
            return Socket();

        sockaddr_storage clientAddr = {};
        socklen_t addrLen = sizeof(clientAddr);

        socket_t accepted = ::accept(_handle, reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
        if (accepted == INVALID_SOCKET)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return Socket();
        }

        return Socket(accepted);
    }

    // Привязывает сокет к порту на всех локальных интерфейсах (0.0.0.0 для IPv4 или :: для IPv6).
    bool Socket::Bind(int port)
    {
        if (!IsCreated())
            return false;

        if (port < 0 || port > 65535)
        {
            SET_NET_ERROR(SOCK_EINVAL);
            return false;
        }

        if (_family == AF_INET)
        {
            sockaddr_in service = {};
            service.sin_family = AF_INET;
            service.sin_addr.s_addr = htonl(INADDR_ANY);
            service.sin_port = htons(static_cast<uint16_t>(port));

            if (::bind(_handle, reinterpret_cast<const sockaddr*>(&service), sizeof(service)) == SOCKET_ERROR)
            {
                SET_NET_ERROR(GET_NET_ERROR());
                return false;
            }
        }
        else if (_family == AF_INET6)
        {
            sockaddr_in6 service = {};
            service.sin6_family = AF_INET6;
            service.sin6_addr = in6addr_any;
            service.sin6_port = htons(static_cast<uint16_t>(port));

            if (::bind(_handle, reinterpret_cast<const sockaddr*>(&service), sizeof(service)) == SOCKET_ERROR)
            {
                SET_NET_ERROR(GET_NET_ERROR());
                return false;
            }
        }
        else
        {
            SET_NET_ERROR(SOCK_EAFNOSUPPORT);
            return false;
        }

        return true;
    }

    // Привязывает сокет к конкретному локальному адресу и порту (поддерживает IPv4 на Dual-Stack сокете).
    bool Socket::Bind(const std::string& host, int port)
    {
        if (!IsCreated())
            return false;

        if (port < 0 || port > 65535)
        {
            SET_NET_ERROR(SOCK_EINVAL);
            return false;
        }

        const bool dualMode = (_family == AF_INET6) && IsDualMode();

        // Готовим подсказки для getaddrinfo(); для Dual-Stack разрешаем AF_UNSPEC, чтобы принять и IPv4-строку.
        addrinfo hints    = {};
        hints.ai_family   = dualMode ? AF_UNSPEC : _family;
        hints.ai_socktype = _type;
        hints.ai_protocol = _protocol;
        hints.ai_flags    = AI_PASSIVE;

        std::string portStr = std::to_string(port);
        addrinfo* res = nullptr;

        // Пустой host передаём как nullptr, чтобы сработал AI_PASSIVE (wildcard-адрес).
        const char* node = host.empty() ? nullptr : host.c_str();
        int gaiErr = ::getaddrinfo(node, portStr.c_str(), &hints, &res);
        if (gaiErr != 0)
        {
            SaveAddrInfoError(gaiErr);
            return false;
        }

        bool success = false;
        int lastErr  = SOCK_EADDRNOTAVAIL;

        for (addrinfo* ptr = res; ptr != nullptr; ptr = ptr->ai_next)
        {
            sockaddr_storage targetAddr = {};
            socklen_t targetLen = 0;
            if (!PrepareTargetAddress(_family, dualMode, ptr->ai_addr, static_cast<socklen_t>(ptr->ai_addrlen), targetAddr, targetLen))
                continue;

            if (::bind(_handle, reinterpret_cast<const sockaddr*>(&targetAddr), targetLen) != SOCKET_ERROR)
            {
                success = true;
                break;
            }

            lastErr = GET_NET_ERROR();
        }

        ::freeaddrinfo(res);

        if (!success)
            SET_NET_ERROR(lastErr);

        return success;
    }

    // Задаёт максимальное время ожидания отправки (SO_SNDTIMEO) в миллисекундах (-1 или 0 = бесконечно).
    int Socket::SetSendTimeout(int milliseconds) noexcept
    {
        if (!IsCreated())
            return -1;

        int timeout = milliseconds;
        if (timeout < -1)
        {
            SET_NET_ERROR(SOCK_EINVAL);
            return -1;
        }

        // Для положительных значений держим минимальный порог 500 мс для стабильности сетевых операций.
        if (timeout > 0 && timeout < 500)
            timeout = 500;

#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
        // В Windows SO_SNDTIMEO принимает DWORD/int в миллисекундах (0 означает бесконечное ожидание).
        int winTimeout = (timeout == -1) ? 0 : timeout;
        int optlen = sizeof(int);
        if (::setsockopt(_handle, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&winTimeout), optlen) != SOCKET_ERROR)
            return 0;
#else
        // В Linux SO_SNDTIMEO принимает структуру timeval { сек, мкс } (0, 0 = бесконечно).
        timeval tv = {};
        if (timeout > 0)
        {
            tv.tv_sec  = timeout / 1000;
            tv.tv_usec = (timeout % 1000) * 1000;
        }

        socklen_t optlen = sizeof(tv);
        if (::setsockopt(_handle, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&tv), optlen) != SOCKET_ERROR)
            return 0;
#endif
        SET_NET_ERROR(GET_NET_ERROR());
        return -1;
    }

    // Задаёт максимальное время ожидания приёма (SO_RCVTIMEO) в миллисекундах (-1 или 0 = бесконечно).
    int Socket::SetRecvTimeout(int milliseconds) noexcept
    {
        if (!IsCreated())
            return -1;

        int timeout = milliseconds;
        if (timeout < -1)
        {
            SET_NET_ERROR(SOCK_EINVAL);
            return -1;
        }

        // Для положительных значений держим минимальный порог 500 мс для стабильности сетевых операций.
        if (timeout > 0 && timeout < 500)
            timeout = 500;

#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
        int winTimeout = (timeout == -1) ? 0 : timeout;
        int optlen = sizeof(int);
        if (::setsockopt(_handle, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&winTimeout), optlen) != SOCKET_ERROR)
            return 0;
#else
        timeval tv = {};
        if (timeout > 0)
        {
            tv.tv_sec  = timeout / 1000;
            tv.tv_usec = (timeout % 1000) * 1000;
        }

        socklen_t optlen = sizeof(tv);
        if (::setsockopt(_handle, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), optlen) != SOCKET_ERROR)
            return 0;
#endif
        SET_NET_ERROR(GET_NET_ERROR());
        return -1;
    }

    // Получает текущий таймаут отправки в миллисекундах (-1 означает бесконечное ожидание).
    int Socket::GetSendTimeout() const noexcept
    {
        if (!IsCreated())
            return -1;

#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
        int timeoutMs = 0;
        int optlen = sizeof(int);
        if (::getsockopt(_handle, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<char*>(&timeoutMs), &optlen) == SOCKET_ERROR)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return -1;
        }

        return (timeoutMs == 0) ? -1 : timeoutMs;
#else
        timeval tv = {};
        socklen_t optlen = sizeof(tv);
        if (::getsockopt(_handle, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<char*>(&tv), &optlen) == SOCKET_ERROR)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return -1;
        }

        if (tv.tv_sec == 0 && tv.tv_usec == 0)
            return -1;

        // Округляем остаток микросекунд вверх, чтобы не получить 0 (бесконечность) для малых таймаутов.
        int64_t ms = static_cast<int64_t>(tv.tv_sec) * 1000 + (tv.tv_usec + 999) / 1000;
        return (ms > std::numeric_limits<int>::max()) ? std::numeric_limits<int>::max() : static_cast<int>(ms);
#endif
    }

    // Получает текущий таймаут приёма в миллисекундах (-1 означает бесконечное ожидание).
    int Socket::GetRecvTimeout() const noexcept
    {
        if (!IsCreated())
            return -1;

#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
        int timeoutMs = 0;
        int optlen = sizeof(int);
        if (::getsockopt(_handle, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&timeoutMs), &optlen) == SOCKET_ERROR)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return -1;
        }

        return (timeoutMs == 0) ? -1 : timeoutMs;
#else
        timeval tv = {};
        socklen_t optlen = sizeof(tv);
        if (::getsockopt(_handle, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&tv), &optlen) == SOCKET_ERROR)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return -1;
        }

        if (tv.tv_sec == 0 && tv.tv_usec == 0)
            return -1;

        int64_t ms = static_cast<int64_t>(tv.tv_sec) * 1000 + (tv.tv_usec + 999) / 1000;
        return (ms > std::numeric_limits<int>::max()) ? std::numeric_limits<int>::max() : static_cast<int>(ms);
#endif
    }

    // Задаёт размер системного буфера отправки ядра ОС (SO_SNDBUF).
    int Socket::SetSendBufferSize(int size) noexcept
    {
        if (!IsCreated())
            return -1;

        if (size <= 0)
        {
            SET_NET_ERROR(SOCK_EINVAL);
            return -1;
        }

        socklen_t optlen = sizeof(int);
        if (::setsockopt(_handle, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&size), optlen) == SOCKET_ERROR)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return -1;
        }

        return 0;
    }

    // Задаёт размер системного буфера приёма ядра ОС (SO_RCVBUF).
    int Socket::SetRecvBufferSize(int size) noexcept
    {
        if (!IsCreated())
            return -1;

        if (size <= 0)
        {
            SET_NET_ERROR(SOCK_EINVAL);
            return -1;
        }

        socklen_t optlen = sizeof(int);
        if (::setsockopt(_handle, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&size), optlen) == SOCKET_ERROR)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return -1;
        }

        return 0;
    }

    // Возвращает фактический размер системного буфера отправки ядра ОС (SO_SNDBUF).
    int Socket::GetSendBufferSize() const
    {
        if (!IsCreated())
            return -1;

        int bufferSize = 0;
        socklen_t optlen = sizeof(bufferSize);
        if (::getsockopt(_handle, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<char*>(&bufferSize), &optlen) == SOCKET_ERROR)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return -1;
        }

        return bufferSize;
    }

    // Возвращает фактический размер системного буфера приёма ядра ОС (SO_RCVBUF).
    int Socket::GetRecvBufferSize() const
    {
        if (!IsCreated())
            return -1;

        int bufferSize = 0;
        socklen_t optlen = sizeof(bufferSize);
        if (::getsockopt(_handle, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char*>(&bufferSize), &optlen) == SOCKET_ERROR)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return -1;
        }

        return bufferSize;
    }

    // Запрещает дальнейшее чтение из сокета (полузакрытие канала приёма).
    void Socket::DoRecv()
    {
        if (!IsCreated())
            return;
#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
        const int how = SD_RECEIVE;
#else
        const int how = SHUT_RD;
#endif
        if (::shutdown(_handle, how) == SOCKET_ERROR)
            SET_NET_ERROR(GET_NET_ERROR());
    }

    // Запрещает дальнейшую отправку (посылает удалённой стороне TCP FIN).
    void Socket::DoSend()
    {
        if (!IsCreated())
            return;
#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
        const int how = SD_SEND;
#else
        const int how = SHUT_WR;
#endif
        if (::shutdown(_handle, how) == SOCKET_ERROR)
            SET_NET_ERROR(GET_NET_ERROR());
    }

    // Запрещает и приём, и отправку данных (полное полузакрытие перед Close).
    void Socket::DoBoth()
    {
        if (!IsCreated())
            return;
#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
        const int how = SD_BOTH;
#else
        const int how = SHUT_RDWR;
#endif
        if (::shutdown(_handle, how) == SOCKET_ERROR)
            SET_NET_ERROR(GET_NET_ERROR());
    }

    // Проверяет, валиден ли дескриптор сокета (если закрыт — выставляет SOCK_EBADF).
    bool Socket::IsCreated() const noexcept
    {
        if (_handle != INVALID_SOCKET)
            return true;

        SET_NET_ERROR(SOCK_EBADF);
        return false;
    }

    // Проверяет, закрыт ли сокет (не изменяет GET_NET_ERROR()).
    bool Socket::IsClosed() const noexcept { return _handle == INVALID_SOCKET; }

    // Проверяет, активно ли соединение прямо сейчас без риска SIGPIPE и без извлечения данных из буфера ядра.
    bool Socket::IsConnected() const noexcept
    {
        if (!IsCreated())
            return false;

        // Проверяем, привязан ли сокет к удалённому адресу (peer).
        sockaddr_storage peer = {};
        socklen_t peerLen = sizeof(peer);
        if (::getpeername(_handle, reinterpret_cast<sockaddr*>(&peer), &peerLen) == SOCKET_ERROR)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return false;
        }

        // Мгновенный опрос состояния сокета через poll/WSAPoll (timeout = 0).
        short revents = 0;
        int ret = PollSingle(_handle, POLLIN, 0, revents);
        if (ret < 0)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return false;
        }

        if (ret > 0)
        {
            // Флаги ошибки, обрыва или невалидного дескриптора означают потерю соединения.
            if (revents & (POLLERR | POLLHUP | POLLNVAL))
            {
                const int sockErr = SockError();
                if (sockErr > 0)
                    SET_NET_ERROR(sockErr);
                return false;
            }

            // Для TCP при POLLIN подглядываем 1 байт с MSG_PEEK: возврат 0 означает TCP FIN от пира.
            // (Для UDP 0-байтная датаграмма валидна, поэтому проверка n == 0 применяется только к SOCK_STREAM.)
            if (_type == SOCK_STREAM && (revents & POLLIN))
            {
                char peekByte = 0;
                int n = static_cast<int>(::recv(_handle, &peekByte, 1, MSG_PEEK));
                if (n == 0)
                    return false;

                if (n == SOCKET_ERROR)
                {
                    const int err = GET_NET_ERROR();
                    SET_NET_ERROR(err);
                    if (!IsWouldBlockError(err))
                        return false;
                }
            }
        }

        return true;
    }

    // Возвращает число байтов, уже находящихся в буфере приёма ядра ОС (FIONREAD).
    int Socket::Available() const
    {
        if (!IsCreated())
            return -1;

#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
        u_long available = 0;
        int ret = ::ioctlsocket(_handle, FIONREAD, &available);
        if (ret == SOCKET_ERROR)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return -1;
        }

        // Защита от переполнения u_long -> int на 32/64-битных платформах.
        constexpr int maxInt = std::numeric_limits<int>::max();
        return static_cast<int>(available > maxInt ? maxInt : available);
#else
        int available = 0;
        int ret = ::ioctl(_handle, FIONREAD, &available);
        if (ret == SOCKET_ERROR)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return -1;
        }

        return available;
#endif
    }

    // Возвращает локальный IP-адрес (и опционально порт) сокета в текстовом виде.
    std::string Socket::LocalAddress(bool includePort) const
    {
        if (!IsCreated())
            return "";

        sockaddr_storage addr = {};
        socklen_t addrlen = sizeof(addr);
        if (::getsockname(_handle, reinterpret_cast<sockaddr*>(&addr), &addrlen) == SOCKET_ERROR)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return "";
        }

        if (addrlen == 0)
        {
            SET_NET_ERROR(SOCK_EINVAL);
            return "";
        }

        if (addr.ss_family == AF_INET)
        {
            const sockaddr_in* ipv4Addr = reinterpret_cast<const sockaddr_in*>(&addr);
            char ipStr[INET_ADDRSTRLEN] = {};

            if (::inet_ntop(AF_INET, &(ipv4Addr->sin_addr), ipStr, INET_ADDRSTRLEN) == nullptr)
            {
                SET_NET_ERROR(GET_NET_ERROR());
                return "";
            }

            return includePort ? (std::string(ipStr) + ":" + std::to_string(ntohs(ipv4Addr->sin_port))) : std::string(ipStr);
        }
        else if (addr.ss_family == AF_INET6)
        {
            const sockaddr_in6* ipv6Addr = reinterpret_cast<const sockaddr_in6*>(&addr);
            char ipStr[INET6_ADDRSTRLEN] = {};

            if (::inet_ntop(AF_INET6, &(ipv6Addr->sin6_addr), ipStr, INET6_ADDRSTRLEN) == nullptr)
            {
                SET_NET_ERROR(GET_NET_ERROR());
                return "";
            }

            return includePort ? ("[" + std::string(ipStr) + "]:" + std::to_string(ntohs(ipv6Addr->sin6_port))) : ("[" + std::string(ipStr) + "]");
        }

        SET_NET_ERROR(SOCK_EAFNOSUPPORT);
        return "";
    }

    // Возвращает IP-адрес (и опционально порт) удалённой стороны соединения.
    std::string Socket::RemoteAddress(bool includePort) const
    {
        if (!IsCreated())
            return "";

        sockaddr_storage addr = {};
        socklen_t addrlen = sizeof(addr);
        if (::getpeername(_handle, reinterpret_cast<sockaddr*>(&addr), &addrlen) == SOCKET_ERROR)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return "";
        }

        if (addrlen == 0)
        {
            SET_NET_ERROR(SOCK_EINVAL);
            return "";
        }

        if (addr.ss_family == AF_INET)
        {
            const sockaddr_in* ipv4Addr = reinterpret_cast<const sockaddr_in*>(&addr);
            char ipStr[INET_ADDRSTRLEN] = {};

            if (::inet_ntop(AF_INET, &(ipv4Addr->sin_addr), ipStr, INET_ADDRSTRLEN) == nullptr)
            {
                SET_NET_ERROR(GET_NET_ERROR());
                return "";
            }

            return includePort ? (std::string(ipStr) + ":" + std::to_string(ntohs(ipv4Addr->sin_port))) : std::string(ipStr);
        }
        else if (addr.ss_family == AF_INET6)
        {
            const sockaddr_in6* ipv6Addr = reinterpret_cast<const sockaddr_in6*>(&addr);
            char ipStr[INET6_ADDRSTRLEN] = {};

            if (::inet_ntop(AF_INET6, &(ipv6Addr->sin6_addr), ipStr, INET6_ADDRSTRLEN) == nullptr)
            {
                SET_NET_ERROR(GET_NET_ERROR());
                return "";
            }

            return includePort ? ("[" + std::string(ipStr) + "]:" + std::to_string(ntohs(ipv6Addr->sin6_port))) : ("[" + std::string(ipStr) + "]");
        }

        SET_NET_ERROR(SOCK_EAFNOSUPPORT);
        return "";
    }

    // =========================================================================
    // ОПЦИИ СОКЕТА, ATTACH/DETACH, ВРЕМЯ ЖИЗНИ
    // =========================================================================

    // Включает/выключает SO_REUSEADDR (позволяет серверу быстро перезапускаться на том же порту).
    bool Socket::SetReuseAddress(bool enable)
    {
        if (!IsCreated())
            return false;

        int optVal = enable ? 1 : 0;
        if (::setsockopt(_handle, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&optVal), sizeof(optVal)) == SOCKET_ERROR)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return false;
        }

        return true;
    }

    // Проверяет, включена ли опция SO_REUSEADDR.
    bool Socket::IsReuseAddress() const
    {
        if (!IsCreated())
            return false;

        int optval = 0;
        socklen_t optlen = sizeof(optval);
        if (::getsockopt(_handle, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&optval), &optlen) == SOCKET_ERROR)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return false;
        }

        return optval != 0;
    }

    // Включает/выключает TCP Keep-Alive (SO_KEEPALIVE).
    bool Socket::SetKeepAlive(bool enable) noexcept
    {
        if (!IsCreated())
            return false;

        int optionValue = enable ? 1 : 0;
        if (::setsockopt(_handle, SOL_SOCKET, SO_KEEPALIVE, reinterpret_cast<const char*>(&optionValue), sizeof(optionValue)) == SOCKET_ERROR)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return false;
        }
        return true;
    }

    // Проверяет, включён ли TCP Keep-Alive.
    bool Socket::IsKeepAlive() const
    {
        if (!IsCreated())
            return false;

        int optionValue = 0;
        socklen_t optionLen = sizeof(optionValue);
        if (::getsockopt(_handle, SOL_SOCKET, SO_KEEPALIVE, reinterpret_cast<char*>(&optionValue), &optionLen) == SOCKET_ERROR)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return false;
        }

        return optionValue != 0;
    }

    // Закрывает текущий сокет (если открыт) и берёт под управление RAII готовый дескриптор.
    bool Socket::Attach(socket_t handle) noexcept
    {
        if (handle == INVALID_SOCKET)
        {
            SET_NET_ERROR(SOCK_EBADF);
            return false;
        }

        // Защита от самопривязки: не закрываем дескриптор, которым уже владеем.
        if (handle == _handle)
            return true;

        Dispose();
        return InitFromHandle(handle);
    }

    // Отвязывает системный дескриптор от объекта, НЕ закрывая его, и сбрасывает все поля.
    socket_t Socket::Detach() noexcept
    {
        socket_t temp   = _handle;
        _handle         = INVALID_SOCKET;
        _family         = 0;
        _type           = 0;
        _protocol       = 0;
        _blocking       = true;
        _start_t        = other::time::DateTime::Zero;
        _close_t        = other::time::DateTime::Zero;
        _bytesSent      = 0;
        _bytesRecv      = 0;
        return temp;
    }

    // Переключает сокет между блокирующим (enable=true) и неблокирующим (enable=false) режимом.
    bool Socket::SetBlocking(bool enable)
    {
        if (!IsCreated())
            return false;

#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
        // В Windows ioctlsocket(FIONBIO): 0 = блокирующий, 1 = неблокирующий.
        u_long mode = enable ? 0 : 1;
        if (::ioctlsocket(_handle, FIONBIO, &mode) == SOCKET_ERROR)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return false;
        }
#else
        // В Linux изменяем только бит O_NONBLOCK, сохраняя остальные флаги дескриптора.
        int flags = ::fcntl(_handle, F_GETFL, 0);
        if (flags == -1)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return false;
        }

        if (enable)
            flags &= ~O_NONBLOCK;
        else
            flags |= O_NONBLOCK;

        if (::fcntl(_handle, F_SETFL, flags) == -1)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return false;
        }
#endif
        _blocking = enable;
        return true;
    }

    // Проверяет, работает ли сокет сейчас в блокирующем режиме.
    bool Socket::IsBlocking() const noexcept
    {
        if (!IsCreated())
            return false;

#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
        return _blocking;
#else
        int flags = ::fcntl(_handle, F_GETFL, 0);
        if (flags == -1)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return _blocking;
        }

        return (flags & O_NONBLOCK) == 0;
#endif
    }

    // Настраивает опцию SO_LINGER (управление поведением при закрытии сокета с неотправленными данными).
    int Socket::SetLingerState(bool enable, int seconds) noexcept
    {
        if (!IsCreated())
            return -1;

        // Ограничиваем время ожидания диапазоном unsigned short (0..65535), чтобы исключить переполнение.
        constexpr int maxLinger = static_cast<int>(std::numeric_limits<unsigned short>::max());
        int clampedSeconds = (seconds < 0) ? 0 : (seconds > maxLinger ? maxLinger : seconds);

        linger l_opt   = {};
        l_opt.l_onoff  = enable ? 1 : 0;
        l_opt.l_linger = static_cast<unsigned short>(clampedSeconds);

        socklen_t optlen = sizeof(l_opt);
        if (::setsockopt(_handle, SOL_SOCKET, SO_LINGER, reinterpret_cast<const char*>(&l_opt), optlen) == SOCKET_ERROR)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return -1;
        }

        return 0;
    }

    // Читает текущие настройки SO_LINGER.
    int Socket::GetLingerState(bool& enable, int& seconds) const noexcept
    {
        if (!IsCreated())
            return -1;

        linger l_opt = {};
        socklen_t optlen = sizeof(l_opt);
        if (::getsockopt(_handle, SOL_SOCKET, SO_LINGER, reinterpret_cast<char*>(&l_opt), &optlen) == SOCKET_ERROR)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return -1;
        }

        enable  = (l_opt.l_onoff != 0);
        seconds = static_cast<int>(l_opt.l_linger);
        return 0;
    }

    // Создаёт новый независимый сокет с теми же параметрами (_family, _type, _protocol).
    Socket Socket::SockClone() const noexcept
    {
        if (IsClosed())
        {
            SET_NET_ERROR(SOCK_EBADF);
            return Socket();
        }

        return Socket(_family, _type, _protocol);
    }

    // Возвращает метки времени создания и закрытия сокета.
    other::time::DateTime Socket::StartTime() const noexcept { return _start_t; }
    other::time::DateTime Socket::CloseTime() const noexcept { return _close_t; }

    // Возвращает длительность работы сокета (не модифицирует GET_NET_ERROR()).
    other::time::DateTime Socket::SockUptime() const noexcept
    {
        if (_start_t.Ticks() == 0)
            return other::time::DateTime::Zero;

        // Используем IsClosed() вместо !IsCreated(), чтобы не перезаписывать errno/LastError кодом SOCK_EBADF.
        if (IsClosed() && _close_t.Ticks() > 0)
            return _close_t - _start_t;

        return other::time::DateTime::Now() - _start_t;
    }

    // Полностью закрывает сокет и сбрасывает все поля, метки времени и счётчики объекта.
    bool Socket::Dispose() noexcept
    {
        bool result = Close();

        _family    = 0;
        _type      = 0;
        _protocol  = 0;
        _start_t   = other::time::DateTime::Zero;
        _close_t   = other::time::DateTime::Zero;
        _blocking  = true;
        _handle    = INVALID_SOCKET;
        _bytesSent = 0;
        _bytesRecv = 0;

        return result || IsClosed();
    }

    // Включает/выключает режим Dual-Stack на IPv6-сокете (IPV6_V6ONLY = 0 разрешает IPv4+IPv6).
    bool Socket::SetDualMode(bool enable) noexcept
    {
        if (!IsCreated())
            return false;

        if (_family != AF_INET6)
        {
            SET_NET_ERROR(SOCK_EAFNOSUPPORT);
            return false;
        }

        int optval = enable ? 0 : 1;
        if (::setsockopt(_handle, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&optval), sizeof(optval)) == SOCKET_ERROR)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return false;
        }

        return true;
    }

    // Проверяет, включён ли режим Dual-Stack на IPv6-сокете.
    bool Socket::IsDualMode() const noexcept
    {
        if (!IsCreated())
            return false;

        if (_family != AF_INET6)
        {
            SET_NET_ERROR(SOCK_EAFNOSUPPORT);
            return false;
        }

        int optval = 0;
        socklen_t optlen = sizeof(optval);
        if (::getsockopt(_handle, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<char*>(&optval), &optlen) == SOCKET_ERROR)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return false;
        }

        return optval == 0;
    }

    // Настраивает тонкие параметры TCP Keep-Alive.
    bool Socket::SetKeepAliveOptions(int idleSec, int intervalSec, int retryCount) noexcept
    {
        if (!IsCreated())
            return false;

        if (idleSec <= 0 || intervalSec <= 0 || retryCount <= 0)
        {
            SET_NET_ERROR(SOCK_EINVAL);
            return false;
        }

        const int idleVal     = ToPlatformKeepAliveInterval(idleSec);
        const int intervalVal = ToPlatformKeepAliveInterval(intervalSec);
        if (idleVal <= 0 || intervalVal <= 0)
        {
            SET_NET_ERROR(SOCK_EINVAL);
            return false;
        }

        auto setOpt = [this](int optName, int val) noexcept -> bool
        {
            return ::setsockopt(_handle, IPPROTO_TCP, optName, reinterpret_cast<const char*>(&val), sizeof(val)) != SOCKET_ERROR;
        };

        if (!setOpt(TCP_KEEPIDLE, idleVal) || !setOpt(TCP_KEEPINTVL, intervalVal) || !setOpt(TCP_KEEPCNT, retryCount))
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return false;
        }

        return true;
    }

    // Читает текущие параметры TCP Keep-Alive.
    bool Socket::GetKeepAliveOptions(int& idleSec, int& intervalSec, int& retryCount) const noexcept
    {
        idleSec     = 0;
        intervalSec = 0;
        retryCount  = 0;

        if (!IsCreated())
            return false;

        auto getOpt = [this](int optName, int& outVal) noexcept -> bool
        {
            socklen_t len = sizeof(outVal);
            return ::getsockopt(_handle, IPPROTO_TCP, optName, reinterpret_cast<char*>(&outVal), &len) != SOCKET_ERROR;
        };

        int rawIdle     = 0;
        int rawInterval = 0;
        int rawCount    = 0;

        if (!getOpt(TCP_KEEPIDLE, rawIdle) || !getOpt(TCP_KEEPINTVL, rawInterval) || !getOpt(TCP_KEEPCNT, rawCount))
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return false;
        }

        idleSec     = FromPlatformKeepAliveInterval(rawIdle);
        intervalSec = FromPlatformKeepAliveInterval(rawInterval);
        retryCount  = rawCount;
        return true;
    }

    // Задаёт TTL / Hop Limit для исходящих multicast-пакетов.
    bool Socket::SetMulticastTtl(int ttl) noexcept
    {
        if (!IsCreated())
            return false;

        if (ttl < 0 || ttl > 255)
        {
            SET_NET_ERROR(SOCK_EINVAL);
            return false;
        }

        int optval = ttl;
        int result = SOCKET_ERROR;

        if (_family == AF_INET)
            result = ::setsockopt(_handle, IPPROTO_IP, IP_MULTICAST_TTL, reinterpret_cast<const char*>(&optval), sizeof(optval));
        else if (_family == AF_INET6)
            result = ::setsockopt(_handle, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, reinterpret_cast<const char*>(&optval), sizeof(optval));
        else
        {
            SET_NET_ERROR(SOCK_EAFNOSUPPORT);
            return false;
        }

        if (result == SOCKET_ERROR)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return false;
        }

        return true;
    }

    // Возвращает TTL / Hop Limit для исходящих multicast-пакетов.
    int Socket::GetMulticastTtl() const noexcept
    {
        if (!IsCreated())
            return -1;

        int optval = 0;
        socklen_t optlen = sizeof(optval);
        int result = SOCKET_ERROR;

        if (_family == AF_INET)
            result = ::getsockopt(_handle, IPPROTO_IP, IP_MULTICAST_TTL, reinterpret_cast<char*>(&optval), &optlen);
        else if (_family == AF_INET6)
            result = ::getsockopt(_handle, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, reinterpret_cast<char*>(&optval), &optlen);
        else
        {
            SET_NET_ERROR(SOCK_EAFNOSUPPORT);
            return -1;
        }

        if (result == SOCKET_ERROR)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return -1;
        }

        return optval;
    }

    // Задаёт исходящий сетевой интерфейс для multicast (IP_MULTICAST_IF / IPV6_MULTICAST_IF).
    bool Socket::SetMulticastInterface(const std::string& interfaceAddress) noexcept
    {
        if (!IsCreated())
            return false;

        if (_family == AF_INET)
        {
            in_addr addr = {};
            if (interfaceAddress.empty())
            {
                addr.s_addr = htonl(INADDR_ANY);
            }
            else if (::inet_pton(AF_INET, interfaceAddress.c_str(), &addr) != 1)
            {
                SET_NET_ERROR(SOCK_EINVAL);
                return false;
            }

            if (::setsockopt(_handle, IPPROTO_IP, IP_MULTICAST_IF, reinterpret_cast<const char*>(&addr), sizeof(addr)) == SOCKET_ERROR)
            {
                SET_NET_ERROR(GET_NET_ERROR());
                return false;
            }

            return true;
        }
        else if (_family == AF_INET6)
        {
            unsigned int ifIndex = 0;
            if (!interfaceAddress.empty())
            {
                try
                {
                    ifIndex = static_cast<unsigned int>(std::stoul(interfaceAddress));
                }
                catch (...)
                {
                    SET_NET_ERROR(SOCK_EINVAL);
                    return false;
                }
            }

            if (::setsockopt(_handle, IPPROTO_IPV6, IPV6_MULTICAST_IF, reinterpret_cast<const char*>(&ifIndex), sizeof(ifIndex)) == SOCKET_ERROR)
            {
                SET_NET_ERROR(GET_NET_ERROR());
                return false;
            }

            return true;
        }

        SET_NET_ERROR(SOCK_EAFNOSUPPORT);
        return false;
    }

    // Подписывает сокет на multicast-группу с явным указанием интерфейса/индекса.
    bool Socket::JoinMulticastGroup(const std::string& multicastAddress, const std::string& interfaceAddress) noexcept
    {
        if (!IsCreated())
            return false;

        if (_family == AF_INET)
        {
            ip_mreq mreq = {};
            if (::inet_pton(AF_INET, multicastAddress.c_str(), &mreq.imr_multiaddr) != 1)
            {
                SET_NET_ERROR(SOCK_EINVAL);
                return false;
            }

            if (interfaceAddress.empty())
            {
                mreq.imr_interface.s_addr = htonl(INADDR_ANY);
            }
            else if (::inet_pton(AF_INET, interfaceAddress.c_str(), &mreq.imr_interface) != 1)
            {
                SET_NET_ERROR(SOCK_EINVAL);
                return false;
            }

            if (::setsockopt(_handle, IPPROTO_IP, IP_ADD_MEMBERSHIP, reinterpret_cast<const char*>(&mreq), sizeof(mreq)) == SOCKET_ERROR)
            {
                SET_NET_ERROR(GET_NET_ERROR());
                return false;
            }

            return true;
        }
        else if (_family == AF_INET6)
        {
            ipv6_mreq mreq = {};
            if (::inet_pton(AF_INET6, multicastAddress.c_str(), &mreq.ipv6mr_multiaddr) != 1)
            {
                SET_NET_ERROR(SOCK_EINVAL);
                return false;
            }

            if (!interfaceAddress.empty())
            {
                try
                {
                    mreq.ipv6mr_interface = static_cast<unsigned int>(std::stoul(interfaceAddress));
                }
                catch (...)
                {
                    SET_NET_ERROR(SOCK_EINVAL);
                    return false;
                }
            }

            if (::setsockopt(_handle, IPPROTO_IPV6, IPV6_JOIN_GROUP, reinterpret_cast<const char*>(&mreq), sizeof(mreq)) == SOCKET_ERROR)
            {
                SET_NET_ERROR(GET_NET_ERROR());
                return false;
            }

            return true;
        }

        SET_NET_ERROR(SOCK_EAFNOSUPPORT);
        return false;
    }

    // Отписывает сокет от multicast-группы с явным указанием интерфейса/индекса.
    bool Socket::LeaveMulticastGroup(const std::string& multicastAddress, const std::string& interfaceAddress) noexcept
    {
        if (!IsCreated())
            return false;

        if (_family == AF_INET)
        {
            ip_mreq mreq = {};
            if (::inet_pton(AF_INET, multicastAddress.c_str(), &mreq.imr_multiaddr) != 1)
            {
                SET_NET_ERROR(SOCK_EINVAL);
                return false;
            }

            if (interfaceAddress.empty())
            {
                mreq.imr_interface.s_addr = htonl(INADDR_ANY);
            }
            else if (::inet_pton(AF_INET, interfaceAddress.c_str(), &mreq.imr_interface) != 1)
            {
                SET_NET_ERROR(SOCK_EINVAL);
                return false;
            }

            if (::setsockopt(_handle, IPPROTO_IP, IP_DROP_MEMBERSHIP, reinterpret_cast<const char*>(&mreq), sizeof(mreq)) == SOCKET_ERROR)
            {
                SET_NET_ERROR(GET_NET_ERROR());
                return false;
            }

            return true;
        }
        else if (_family == AF_INET6)
        {
            ipv6_mreq mreq = {};
            if (::inet_pton(AF_INET6, multicastAddress.c_str(), &mreq.ipv6mr_multiaddr) != 1)
            {
                SET_NET_ERROR(SOCK_EINVAL);
                return false;
            }

            if (!interfaceAddress.empty())
            {
                try
                {
                    mreq.ipv6mr_interface = static_cast<unsigned int>(std::stoul(interfaceAddress));
                }
                catch (...)
                {
                    SET_NET_ERROR(SOCK_EINVAL);
                    return false;
                }
            }

            if (::setsockopt(_handle, IPPROTO_IPV6, IPV6_LEAVE_GROUP, reinterpret_cast<const char*>(&mreq), sizeof(mreq)) == SOCKET_ERROR)
            {
                SET_NET_ERROR(GET_NET_ERROR());
                return false;
            }

            return true;
        }

        SET_NET_ERROR(SOCK_EAFNOSUPPORT);
        return false;
    }

    // Задаёт TOS / DSCP (IP_TOS / IPV6_TCLASS).
    bool Socket::SetTypeOfService(int tos) noexcept
    {
        if (!IsCreated())
            return false;

        if (tos < 0 || tos > 255)
        {
            SET_NET_ERROR(SOCK_EINVAL);
            return false;
        }

        int optval = tos;
        int result = SOCKET_ERROR;

        if (_family == AF_INET)
            result = ::setsockopt(_handle, IPPROTO_IP, IP_TOS, reinterpret_cast<const char*>(&optval), sizeof(optval));
        else if (_family == AF_INET6)
            result = ::setsockopt(_handle, IPPROTO_IPV6, IPV6_TCLASS, reinterpret_cast<const char*>(&optval), sizeof(optval));
        else
        {
            SET_NET_ERROR(SOCK_EAFNOSUPPORT);
            return false;
        }

        if (result == SOCKET_ERROR)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return false;
        }

        return true;
    }

    // Читает TOS / DSCP (IP_TOS / IPV6_TCLASS).
    int Socket::GetTypeOfService() const noexcept
    {
        if (!IsCreated())
            return -1;

        int optval = 0;
        socklen_t optlen = sizeof(optval);
        int result = SOCKET_ERROR;

        if (_family == AF_INET)
            result = ::getsockopt(_handle, IPPROTO_IP, IP_TOS, reinterpret_cast<char*>(&optval), &optlen);
        else if (_family == AF_INET6)
            result = ::getsockopt(_handle, IPPROTO_IPV6, IPV6_TCLASS, reinterpret_cast<char*>(&optval), &optlen);
        else
        {
            SET_NET_ERROR(SOCK_EAFNOSUPPORT);
            return -1;
        }

        if (result == SOCKET_ERROR)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return -1;
        }

        return optval & 0xFF;
    }

    // Включает/выключает SO_REUSEPORT (на Linux).
    bool Socket::SetReusePort(bool enable) noexcept
    {
        if (!IsCreated())
            return false;

#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
        LIBOTHER_UNUSED(enable);
        SET_NET_ERROR(SOCK_ENOPROTOOPT);
        return false;
#else
        int optval = enable ? 1 : 0;
        if (::setsockopt(_handle, SOL_SOCKET, SO_REUSEPORT, reinterpret_cast<const char*>(&optval), sizeof(optval)) == SOCKET_ERROR)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return false;
        }

        return true;
#endif
    }

    // Проверяет SO_REUSEPORT.
    bool Socket::IsReusePort() const noexcept
    {
        if (!IsCreated())
            return false;

#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
        SET_NET_ERROR(SOCK_ENOPROTOOPT);
        return false;
#else
        int optval = 0;
        socklen_t optlen = sizeof(optval);
        if (::getsockopt(_handle, SOL_SOCKET, SO_REUSEPORT, reinterpret_cast<char*>(&optval), &optlen) == SOCKET_ERROR)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return false;
        }

        return optval != 0;
#endif
    }

    // Включает/выключает TCP_QUICKACK (на Linux).
    bool Socket::SetQuickAck(bool enable) noexcept
    {
        if (!IsCreated())
            return false;

#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
        LIBOTHER_UNUSED(enable);
        SET_NET_ERROR(SOCK_ENOPROTOOPT);
        return false;
#else
        int optval = enable ? 1 : 0;
        if (::setsockopt(_handle, IPPROTO_TCP, TCP_QUICKACK, reinterpret_cast<const char*>(&optval), sizeof(optval)) == SOCKET_ERROR)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return false;
        }

        return true;
#endif
    }

    // Проверяет TCP_QUICKACK.
    bool Socket::IsQuickAck() const noexcept
    {
        if (!IsCreated())
            return false;

#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
        SET_NET_ERROR(SOCK_ENOPROTOOPT);
        return false;
#else
        int optval = 0;
        socklen_t optlen = sizeof(optval);
        if (::getsockopt(_handle, IPPROTO_TCP, TCP_QUICKACK, reinterpret_cast<char*>(&optval), &optlen) == SOCKET_ERROR)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return false;
        }

        return optval != 0;
#endif
    }

    // Блокирующее подключение к удалённому хосту (IP или доменное имя) и порту.
    bool Socket::Connect(const std::string& host, int port)
    {
        if (!IsCreated())
            return false;

        if (port < 0 || port > 65535)
        {
            SET_NET_ERROR(SOCK_EINVAL);
            return false;
        }

        const bool dualMode = (_family == AF_INET6) && IsDualMode();

        addrinfo hints    = {};
        hints.ai_family   = dualMode ? AF_UNSPEC : _family;
        hints.ai_socktype = _type;
        hints.ai_protocol = _protocol;

        std::string portStr = std::to_string(port);
        addrinfo* res = nullptr;

        int gaiErr = ::getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res);
        if (gaiErr != 0)
        {
            SaveAddrInfoError(gaiErr);
            return false;
        }

        bool success = false;
        int lastErr  = SOCK_EHOSTUNREACH;

        // Перебираем все найденные адреса; для Dual-Stack IPv4 автоматически конвертируется в ::ffff:x.x.x.x.
        for (addrinfo* ptr = res; ptr != nullptr; ptr = ptr->ai_next)
        {
            sockaddr_storage targetAddr = {};
            socklen_t targetLen = 0;
            if (!PrepareTargetAddress(_family, dualMode, ptr->ai_addr, static_cast<socklen_t>(ptr->ai_addrlen), targetAddr, targetLen))
                continue;

            if (::connect(_handle, reinterpret_cast<const sockaddr*>(&targetAddr), targetLen) != SOCKET_ERROR)
            {
                success = true;
                break;
            }

            lastErr = GET_NET_ERROR();
        }

        ::freeaddrinfo(res);

        if (!success)
        {
            SET_NET_ERROR(lastErr);
            return false;
        }

        return true;
    }

    // Подключение с общим ограничением по времени (timeoutMs в мс) на все адреса из DNS.
    bool Socket::Connect(const std::string& host, int port, int timeoutMs)
    {
        if (!IsCreated())
            return false;

        // Отрицательный таймаут означает обычное блокирующее подключение без лимита времени.
        if (timeoutMs < 0)
            return Connect(host, port);

        if (port < 0 || port > 65535)
        {
            SET_NET_ERROR(SOCK_EINVAL);
            return false;
        }

        const bool dualMode = (_family == AF_INET6) && IsDualMode();

        addrinfo hints    = {};
        hints.ai_family   = dualMode ? AF_UNSPEC : _family;
        hints.ai_socktype = _type;
        hints.ai_protocol = _protocol;

        std::string portStr = std::to_string(port);
        addrinfo* res = nullptr;
        int gaiErr = ::getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res);
        if (gaiErr != 0)
        {
            SaveAddrInfoError(gaiErr);
            return false;
        }

        // Запоминаем исходный режим блокировки и временно переводим сокет в неблокирующий режим.
        const bool wasBlocking = IsBlocking();
        if (wasBlocking)
            SetBlocking(false);

        using Clock = std::chrono::steady_clock;
        const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);

        bool success = false;
        int lastErr  = SOCK_ETIMEDOUT;

        for (addrinfo* ptr = res; ptr != nullptr; ptr = ptr->ai_next)
        {
            sockaddr_storage targetAddr = {};
            socklen_t targetLen = 0;
            if (!PrepareTargetAddress(_family, dualMode, ptr->ai_addr, static_cast<socklen_t>(ptr->ai_addrlen), targetAddr, targetLen))
                continue;

            int ret = ::connect(_handle, reinterpret_cast<const sockaddr*>(&targetAddr), targetLen);
            if (ret == 0)
            {
                success = true;
                break;
            }

            int err = GET_NET_ERROR();
            const bool inProgress =
#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
                (err == WSAEWOULDBLOCK || err == WSAEINPROGRESS);
#else
                (err == EINPROGRESS || err == EWOULDBLOCK);
#endif
            // Мгновенный отказ (например, ECONNREFUSED/ENETUNREACH) — пробуем следующий адрес из списка.
            if (!inProgress)
            {
                lastErr = err;
                continue;
            }

            // Вычисляем остаток времени до общего дедлайна.
            long remainingMs = 0;
            if (timeoutMs > 0)
            {
                const auto now = Clock::now();
                if (now >= deadline)
                {
                    lastErr = SOCK_ETIMEDOUT;
                    break;
                }

                remainingMs = static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
            }

            // Ждём завершения TCP-рукопожатия через poll/WSAPoll (готовность к записи POLLOUT).
            short revents = 0;
            int pollRes = PollSingle(_handle, POLLOUT, remainingMs, revents);
            if (pollRes > 0)
            {
                // Проверяем SO_ERROR: poll просыпается как при успехе, так и при отказе сервера.
                int sockErr = SockError();
                if (sockErr == 0 && (revents & (POLLERR | POLLHUP | POLLNVAL)) == 0)
                {
                    success = true;
                    break;
                }

                lastErr = (sockErr > 0) ? sockErr : SOCK_ECONNREFUSED;
            }
            else if (pollRes == 0)
            {
                // Общий таймаут истёк — прекращаем дальнейшие попытки.
                lastErr = SOCK_ETIMEDOUT;
                break;
            }
            else
            {
                lastErr = GET_NET_ERROR();
                break;
            }
        }

        ::freeaddrinfo(res);

        // Восстанавливаем исходный блокирующий режим сокета.
        if (wasBlocking)
            SetBlocking(true);

        // Записываем итоговый код ошибки после восстановления режима блокировки.
        if (!success)
            SET_NET_ERROR(lastErr);

        return success;
    }

    // Разрывает соединение (shutdown + close; на Windows при reuseSocket=true пробует DisconnectEx).
    bool Socket::Disconnect(bool reuseSocket)
    {
        if (!IsCreated())
            return false;

        // Вежливо останавливаем приём и отправку.
        DoBoth();

        if (reuseSocket)
        {
#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
            GUID guidDisconnectEx = WSAID_DISCONNECTEX;
            LPFN_DISCONNECTEX fnDisconnectEx = nullptr;
            DWORD bytes = 0;

            if (::WSAIoctl(_handle, SIO_GET_EXTENSION_FUNCTION_POINTER, &guidDisconnectEx, sizeof(guidDisconnectEx), &fnDisconnectEx, sizeof(fnDisconnectEx), &bytes, nullptr, nullptr) == 0 && fnDisconnectEx)
            {
                if (!fnDisconnectEx(_handle, nullptr, TF_REUSE_SOCKET, 0))
                {
                    SET_NET_ERROR(GET_NET_ERROR());
                    Close();
                    return false;
                }
                return true;
            }
            SET_NET_ERROR(GET_NET_ERROR());
            Close();
            return false;
#else
            // На Linux TCP-сокет нельзя переиспользовать без закрытия.
            Close();
            SET_NET_ERROR(SOCK_ENOTSUP);
            return false;
#endif
        }

        Close();
        return true;
    }

    // Проверяет, включён ли TCP_NODELAY (отключение алгоритма Нейгла).
    bool Socket::IsNoDelay() const noexcept
    {
        if (!IsCreated())
            return false;

        int optval = 0;
        socklen_t optlen = sizeof(optval);
        if (::getsockopt(_handle, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char*>(&optval), &optlen) == SOCKET_ERROR)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return false;
        }

        return optval != 0;
    }

    // Включает/выключает TCP_NODELAY.
    bool Socket::SetNoDelay(bool enable) noexcept
    {
        if (!IsCreated())
            return false;

        int optval = enable ? 1 : 0;
        if (::setsockopt(_handle, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&optval), sizeof(optval)) == SOCKET_ERROR)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return false;
        }

        return true;
    }

    // Получает TTL (Time To Live / Hop Limit) для исходящих пакетов.
    short Socket::GetTtl() const
    {
        if (!IsCreated())
            return -1;

        int optval = 0;
        socklen_t optlen = sizeof(optval);

        if (_family == AF_INET)
        {
            if (::getsockopt(_handle, IPPROTO_IP, IP_TTL, reinterpret_cast<char*>(&optval), &optlen) == SOCKET_ERROR)
            {
                SET_NET_ERROR(GET_NET_ERROR());
                return -1;
            }
        }
        else if (_family == AF_INET6)
        {
            if (::getsockopt(_handle, IPPROTO_IPV6, IPV6_UNICAST_HOPS, reinterpret_cast<char*>(&optval), &optlen) == SOCKET_ERROR)
            {
                SET_NET_ERROR(GET_NET_ERROR());
                return -1;
            }
        }
        else
        {
            SET_NET_ERROR(SOCK_EAFNOSUPPORT);
            return -1;
        }

        return static_cast<short>(optval);
    }

    // Устанавливает TTL (0..255) для исходящих пакетов.
    bool Socket::SetTtl(short value)
    {
        if (!IsCreated())
            return false;

        if (value < 0 || value > 255)
        {
            SET_NET_ERROR(SOCK_EINVAL);
            return false;
        }

        int optval = static_cast<int>(value);

        if (_family == AF_INET)
        {
            if (::setsockopt(_handle, IPPROTO_IP, IP_TTL, reinterpret_cast<const char*>(&optval), sizeof(optval)) == SOCKET_ERROR)
            {
                SET_NET_ERROR(GET_NET_ERROR());
                return false;
            }
        }
        else if (_family == AF_INET6)
        {
            if (::setsockopt(_handle, IPPROTO_IPV6, IPV6_UNICAST_HOPS, reinterpret_cast<const char*>(&optval), sizeof(optval)) == SOCKET_ERROR)
            {
                SET_NET_ERROR(GET_NET_ERROR());
                return false;
            }
        }
        else
        {
            SET_NET_ERROR(SOCK_EAFNOSUPPORT);
            return false;
        }

        return true;
    }

    // Проверяет флаг запрета фрагментации IP-пакетов (Don't Fragment).
    bool Socket::IsDontFragment() const
    {
        if (!IsCreated())
            return false;

        int optval = 0;
        socklen_t optlen = sizeof(optval);

        if (_family == AF_INET)
        {
#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
            if (::getsockopt(_handle, IPPROTO_IP, IP_DONTFRAGMENT, reinterpret_cast<char*>(&optval), &optlen) == SOCKET_ERROR)
            {
                SET_NET_ERROR(GET_NET_ERROR());
                return false;
            }
#else
            if (::getsockopt(_handle, IPPROTO_IP, IP_MTU_DISCOVER, reinterpret_cast<char*>(&optval), &optlen) == SOCKET_ERROR)
            {
                SET_NET_ERROR(GET_NET_ERROR());
                return false;
            }

            return (optval == IP_PMTUDISC_DO || optval == IP_PMTUDISC_PROBE);
#endif
        }
        else if (_family == AF_INET6)
        {
            if (::getsockopt(_handle, IPPROTO_IPV6, IPV6_DONTFRAG, reinterpret_cast<char*>(&optval), &optlen) == SOCKET_ERROR)
            {
                SET_NET_ERROR(GET_NET_ERROR());
                return false;
            }
        }
        else
        {
            SET_NET_ERROR(SOCK_EAFNOSUPPORT);
            return false;
        }

        return (optval != 0);
    }

    // Включает или отключает флаг Don't Fragment.
    bool Socket::SetDontFragment(bool value)
    {
        if (!IsCreated())
            return false;

        if (_family == AF_INET)
        {
#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
            int optval = value ? 1 : 0;
            if (::setsockopt(_handle, IPPROTO_IP, IP_DONTFRAGMENT, reinterpret_cast<const char*>(&optval), sizeof(optval)) == SOCKET_ERROR)
            {
                SET_NET_ERROR(GET_NET_ERROR());
                return false;
            }
#else
            int optval = value ? IP_PMTUDISC_DO : IP_PMTUDISC_WANT;
            if (::setsockopt(_handle, IPPROTO_IP, IP_MTU_DISCOVER, reinterpret_cast<const char*>(&optval), sizeof(optval)) == SOCKET_ERROR)
            {
                SET_NET_ERROR(GET_NET_ERROR());
                return false;
            }
#endif
        }
        else if (_family == AF_INET6)
        {
            int optval = value ? 1 : 0;
            if (::setsockopt(_handle, IPPROTO_IPV6, IPV6_DONTFRAG, reinterpret_cast<const char*>(&optval), sizeof(optval)) == SOCKET_ERROR)
            {
                SET_NET_ERROR(GET_NET_ERROR());
                return false;
            }
        }
        else
        {
            SET_NET_ERROR(SOCK_EAFNOSUPPORT);
            return false;
        }

        return true;
    }

    // Проверяет, возвращаются ли исходящие multicast-пакеты на локальный сокет.
    bool Socket::IsMulticastLoopback() const
    {
        if (!IsCreated())
            return false;

        int optval = 0;
        socklen_t optlen = sizeof(optval);
        int result = SOCKET_ERROR;

        if (_family == AF_INET)
            result = ::getsockopt(_handle, IPPROTO_IP, IP_MULTICAST_LOOP, reinterpret_cast<char*>(&optval), &optlen);
        else if (_family == AF_INET6)
            result = ::getsockopt(_handle, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, reinterpret_cast<char*>(&optval), &optlen);
        else
        {
            SET_NET_ERROR(SOCK_EAFNOSUPPORT);
            return false;
        }

        if (result == SOCKET_ERROR)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return false;
        }

        return optval != 0;
    }

    // Включает/выключает эхо-возврат multicast-пакетов на локальной машине.
    bool Socket::SetMulticastLoopback(bool enable)
    {
        if (!IsCreated())
            return false;

        int value  = enable ? 1 : 0;
        int result = SOCKET_ERROR;

        if (_family == AF_INET)
            result = ::setsockopt(_handle, IPPROTO_IP, IP_MULTICAST_LOOP, reinterpret_cast<const char*>(&value), sizeof(value));
        else if (_family == AF_INET6)
            result = ::setsockopt(_handle, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, reinterpret_cast<const char*>(&value), sizeof(value));
        else
        {
            SET_NET_ERROR(SOCK_EAFNOSUPPORT);
            return false;
        }

        if (result == SOCKET_ERROR)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return false;
        }

        return true;
    }

    // Подписывает UDP-сокет на multicast-группу (IPv4 или IPv6).
    bool Socket::JoinMulticastGroup(const std::string& multicastAddress) noexcept
    {
        if (!IsCreated())
            return false;

        if (_family == AF_INET)
        {
            ip_mreq mreq = {};
            if (::inet_pton(AF_INET, multicastAddress.c_str(), &mreq.imr_multiaddr) != 1)
            {
                SET_NET_ERROR(SOCK_EINVAL);
                return false;
            }

            mreq.imr_interface.s_addr = htonl(INADDR_ANY);
            if (::setsockopt(_handle, IPPROTO_IP, IP_ADD_MEMBERSHIP, reinterpret_cast<const char*>(&mreq), sizeof(mreq)) == SOCKET_ERROR)
            {
                SET_NET_ERROR(GET_NET_ERROR());
                return false;
            }

            return true;
        }
        else if (_family == AF_INET6)
        {
            ipv6_mreq mreq = {};
            if (::inet_pton(AF_INET6, multicastAddress.c_str(), &mreq.ipv6mr_multiaddr) != 1)
            {
                SET_NET_ERROR(SOCK_EINVAL);
                return false;
            }

            mreq.ipv6mr_interface = 0;
            if (::setsockopt(_handle, IPPROTO_IPV6, IPV6_JOIN_GROUP, reinterpret_cast<const char*>(&mreq), sizeof(mreq)) == SOCKET_ERROR)
            {
                SET_NET_ERROR(GET_NET_ERROR());
                return false;
            }

            return true;
        }

        SET_NET_ERROR(SOCK_EAFNOSUPPORT);
        return false;
    }

    // Отписывает UDP-сокет от multicast-группы.
    bool Socket::LeaveMulticastGroup(const std::string& multicastAddress) noexcept
    {
        if (!IsCreated())
            return false;

        if (_family == AF_INET)
        {
            ip_mreq mreq = {};
            if (::inet_pton(AF_INET, multicastAddress.c_str(), &mreq.imr_multiaddr) != 1)
            {
                SET_NET_ERROR(SOCK_EINVAL);
                return false;
            }

            mreq.imr_interface.s_addr = htonl(INADDR_ANY);
            if (::setsockopt(_handle, IPPROTO_IP, IP_DROP_MEMBERSHIP, reinterpret_cast<const char*>(&mreq), sizeof(mreq)) == SOCKET_ERROR)
            {
                SET_NET_ERROR(GET_NET_ERROR());
                return false;
            }

            return true;
        }
        else if (_family == AF_INET6)
        {
            ipv6_mreq mreq = {};
            if (::inet_pton(AF_INET6, multicastAddress.c_str(), &mreq.ipv6mr_multiaddr) != 1)
            {
                SET_NET_ERROR(SOCK_EINVAL);
                return false;
            }

            mreq.ipv6mr_interface = 0;
            if (::setsockopt(_handle, IPPROTO_IPV6, IPV6_LEAVE_GROUP, reinterpret_cast<const char*>(&mreq), sizeof(mreq)) == SOCKET_ERROR)
            {
                SET_NET_ERROR(GET_NET_ERROR());
                return false;
            }

            return true;
        }

        SET_NET_ERROR(SOCK_EAFNOSUPPORT);
        return false;
    }

    // Разрешает/запрещает отправку широковещательных UDP-пакетов (SO_BROADCAST).
    bool Socket::SetBroadcast(bool enable) noexcept
    {
        if (!IsCreated())
            return false;

        int value = enable ? 1 : 0;
        if (::setsockopt(_handle, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&value), sizeof(value)) == SOCKET_ERROR)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return false;
        }

        return true;
    }

    // Проверяет, разрешён ли broadcast на этом сокете.
    bool Socket::IsBroadcast() const noexcept
    {
        if (!IsCreated())
            return false;

        int optval = 0;
        socklen_t optlen = sizeof(optval);
        if (::getsockopt(_handle, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<char*>(&optval), &optlen) == SOCKET_ERROR)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return false;
        }

        return optval != 0;
    }

    // Запрещает другим процессам привязываться к тому же порту (поддерживается только на Windows).
    bool Socket::SetExclusiveAddressUse(bool exclusive) noexcept
    {
        if (!IsCreated())
            return false;

#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
        int optval = exclusive ? 1 : 0;
        if (::setsockopt(_handle, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&optval), sizeof(optval)) == SOCKET_ERROR)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return false;
        }

        return true;
#else
        LIBOTHER_UNUSED(exclusive);
        SET_NET_ERROR(SOCK_ENOPROTOOPT);
        return false;
#endif
    }

    // Проверяет состояние SO_EXCLUSIVEADDRUSE (на Linux выставляет SOCK_ENOPROTOOPT и возвращает false).
    bool Socket::IsExclusiveAddressUse() const noexcept
    {
        if (!IsCreated())
            return false;

#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
        int optval = 0;
        socklen_t optlen = sizeof(optval);
        if (::getsockopt(_handle, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<char*>(&optval), &optlen) == SOCKET_ERROR)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return false;
        }

        return optval != 0;
#else
        SET_NET_ERROR(SOCK_ENOPROTOOPT);
        return false;
#endif
    }

    // Отправляет порцию данных через подключённый сокет и учитывает отправленные байты в _bytesSent.
    int Socket::Send(const char* data, int size, int flags)
    {
        if (!IsCreated())
            return -1;

        if (data == nullptr || size <= 0)
        {
            SET_NET_ERROR(SOCK_EINVAL);
            return -1;
        }

        // Отправляем данные через ::send() с автоматическим MSG_NOSIGNAL на Linux.
        int result = static_cast<int>(::send(_handle, data, size, ApplyNoSignalFlag(flags)));
        if (result == SOCKET_ERROR)
        {
            int error = GET_NET_ERROR();

            // Для неблокирующего сокета переполненный буфер отправки — штатная ситуация, возвращаем 0.
            if (IsWouldBlockError(error))
            {
                SET_NET_ERROR(SOCK_EWOULDBLOCK);
                return 0;
            }

            SET_NET_ERROR(error);
            return -1;
        }

        if (result > 0)
            _bytesSent += static_cast<uint64_t>(result);

        return result;
    }

    // Отправляет данные со смещением offset в массиве data.
    int Socket::SendOffset(const char* data, int offset, int size, int flags)
    {
        if (data == nullptr || size <= 0 || offset < 0 || offset > INT_MAX - size)
        {
            SET_NET_ERROR(SOCK_EINVAL);
            return -1;
        }

        return Send(data + offset, size, flags);
    }

    // Гарантированно отправляет ровно size байт в цикле (повторяет вызов при EINTR).
    bool Socket::SendAll(const char* data, int size, int flags)
    {
        if (!IsCreated())
            return false;

        if (data == nullptr || size <= 0)
        {
            SET_NET_ERROR(SOCK_EINVAL);
            return false;
        }

        int totalSent = 0;
        while (totalSent < size)
        {
            int bytes = Send(data + totalSent, size - totalSent, flags);
            if (bytes < 0)
            {
                // На Linux повторяем отправку, если системный вызов прерван сигналом ОС.
                if (GET_NET_ERROR() == SOCK_EINTR)
                    continue;

                return false;
            }

            if (bytes == 0)
                return false;

            totalSent += bytes;
        }
        return true;
    }

    // Гарантированно отправляет ровно size байт со смещения offset.
    bool Socket::SendAllOffset(const char* data, int offset, int size, int flags)
    {
        if (data == nullptr || offset < 0 || size <= 0 || size > INT_MAX - offset)
        {
            SET_NET_ERROR(SOCK_EINVAL);
            return false;
        }

        return SendAll(data + offset, size, flags);
    }

    // Отправляет UDP-датаграмму на указанный адрес и порт без предварительного Connect().
    int Socket::SendTo(const char* data, int size, const std::string& toHost, int port, int flags)
    {
        if (!IsCreated())
            return -1;

        if ((data == nullptr && size > 0) || size < 0 || port < 0 || port > 65535)
        {
            SET_NET_ERROR(SOCK_EINVAL);
            return -1;
        }

        if (size == 0)
            return 0;

        const bool dualMode = (_family == AF_INET6) && IsDualMode();

        addrinfo hints    = {};
        hints.ai_family   = dualMode ? AF_UNSPEC : _family;
        hints.ai_socktype = _type;
        hints.ai_protocol = _protocol;

        std::string portStr = std::to_string(port);
        addrinfo* res = nullptr;

        int gaiErr = ::getaddrinfo(toHost.c_str(), portStr.c_str(), &hints, &res);
        if (gaiErr != 0)
        {
            SaveAddrInfoError(gaiErr);
            return -1;
        }

        int bytesSent = SOCKET_ERROR;
        int lastErr   = SOCK_EHOSTUNREACH;

        for (addrinfo* ptr = res; ptr != nullptr; ptr = ptr->ai_next)
        {
            sockaddr_storage targetAddr = {};
            socklen_t targetLen = 0;
            if (!PrepareTargetAddress(_family, dualMode, ptr->ai_addr, static_cast<socklen_t>(ptr->ai_addrlen), targetAddr, targetLen))
                continue;

            bytesSent = static_cast<int>(::sendto(_handle, data, size, ApplyNoSignalFlag(flags), reinterpret_cast<const sockaddr*>(&targetAddr), targetLen));
            if (bytesSent != SOCKET_ERROR)
                break;

            lastErr = GET_NET_ERROR();
            if (IsWouldBlockError(lastErr) || lastErr == SOCK_EINTR)
                break;
        }

        ::freeaddrinfo(res);

        if (bytesSent == SOCKET_ERROR)
        {
            if (IsWouldBlockError(lastErr))
            {
                SET_NET_ERROR(SOCK_EWOULDBLOCK);
                return -1;
            }

            SET_NET_ERROR(lastErr);
            return -1;
        }

        if (bytesSent > 0)
            _bytesSent += static_cast<uint64_t>(bytesSent);

        return bytesSent;
    }

    // Отправляет UDP-датаграмму со смещением offset внутри буфера data.
    int Socket::SendToOffset(const char* data, int offset, int size, const std::string& toHost, int port, int flags)
    {
        if (data == nullptr || offset < 0 || size < 0 || size > INT_MAX - offset)
        {
            SET_NET_ERROR(SOCK_EINVAL);
            return -1;
        }

        return SendTo(data + offset, size, toHost, port, flags);
    }

    // Принимает данные из подключённого сокета (0 = EOF, -1 с SOCK_EWOULDBLOCK = нет данных на неблокирующем сокете).
    int Socket::Recv(char* data, int size, int flags)
    {
        if (!IsCreated())
            return -1;

        if (data == nullptr || size <= 0)
        {
            SET_NET_ERROR(SOCK_EINVAL);
            return -1;
        }

        int result = static_cast<int>(::recv(_handle, data, size, flags));
        if (result == SOCKET_ERROR)
        {
            int error = GET_NET_ERROR();
            // Для неблокирующего сокета возвращаем -1 и выставляем SOCK_EWOULDBLOCK,
            // чтобы вызывающий код мог однозначно отличать его от штатного закрытия соединения (0).
            if (IsWouldBlockError(error))
            {
                SET_NET_ERROR(SOCK_EWOULDBLOCK);
                return -1;
            }

            SET_NET_ERROR(error);
            return -1;
        }

        if (result > 0)
            _bytesRecv += static_cast<uint64_t>(result);

        return result;
    }

    // Принимает данные в буфер, начиная со смещения offset.
    int Socket::RecvOffset(char* data, int offset, int size, int flags)
    {
        if (data == nullptr || size <= 0 || offset < 0 || offset > INT_MAX - size)
        {
            SET_NET_ERROR(SOCK_EINVAL);
            return -1;
        }

        return Recv(data + offset, size, flags);
    }

    // Принимает UDP-датаграмму и записывает IP и порт отправителя в fromHost и fromPort.
    int Socket::RecvFrom(char* data, int size, std::string& fromHost, int& fromPort, int flags)
    {
        if (!IsCreated())
            return -1;

        if (data == nullptr || size <= 0)
        {
            SET_NET_ERROR(SOCK_EINVAL);
            return -1;
        }

        sockaddr_storage clientAddr = {};
        socklen_t addrLen = sizeof(clientAddr);

        int result = static_cast<int>(::recvfrom(_handle, data, size, flags, reinterpret_cast<sockaddr*>(&clientAddr), &addrLen));
        if (result == SOCKET_ERROR)
        {
            int error = GET_NET_ERROR();
            if (IsWouldBlockError(error))
            {
                SET_NET_ERROR(SOCK_EWOULDBLOCK);
                return -1;
            }

            SET_NET_ERROR(error);
            return -1;
        }

        if (result > 0)
            _bytesRecv += static_cast<uint64_t>(result);

        // Преобразуем IP отправителя в числовую строку без DNS-запросов (NI_NUMERICHOST).
        char hostBuffer[NI_MAXHOST] = {};
        if (::getnameinfo(reinterpret_cast<sockaddr*>(&clientAddr), addrLen, hostBuffer, sizeof(hostBuffer), nullptr, 0, NI_NUMERICHOST) == 0)
        {
            fromHost = hostBuffer;
        }
        else
        {
            fromHost.clear();
        }

        // Извлекаем порт напрямую из бинарной структуры без исключений и парсинга строк.
        if (clientAddr.ss_family == AF_INET)
            fromPort = ntohs(reinterpret_cast<const sockaddr_in*>(&clientAddr)->sin_port);
        else if (clientAddr.ss_family == AF_INET6)
            fromPort = ntohs(reinterpret_cast<const sockaddr_in6*>(&clientAddr)->sin6_port);
        else
            fromPort = 0;

        return result;
    }

    // Принимает UDP-датаграмму со смещением offset в буфере data.
    int Socket::RecvFromOffset(char* data, int offset, int size, std::string& fromHost, int& fromPort, int flags)
    {
        if (data == nullptr || offset < 0 || size <= 0 || size > INT_MAX - offset)
        {
            SET_NET_ERROR(SOCK_EINVAL);
            return -1;
        }

        return RecvFrom(data + offset, size, fromHost, fromPort, flags);
    }

    // Гарантированно вычитывает ровно size байт в цикле (повторяет вызов при EINTR).
    bool Socket::RecvAll(char* data, int size, int flags)
    {
        if (!IsCreated())
            return false;

        if (data == nullptr || size <= 0)
        {
            SET_NET_ERROR(SOCK_EINVAL);
            return false;
        }

        int totalReceived = 0;
        while (totalReceived < size)
        {
            int bytes = Recv(data + totalReceived, size - totalReceived, flags);
            if (bytes < 0)
            {
                if (GET_NET_ERROR() == SOCK_EINTR)
                    continue;

                return false;
            }

            // 0 означает, что собеседник закрыл соединение раньше, чем передал все size байт.
            if (bytes == 0)
            {
                SET_NET_ERROR(SOCK_ENOTCONN);
                return false;
            }

            totalReceived += bytes;
        }

        return true;
    }

    // Гарантированно вычитывает ровно size байт со смещения offset.
    bool Socket::RecvAllOffset(char* data, int offset, int size, int flags)
    {
        if (data == nullptr || offset < 0 || size <= 0 || size > INT_MAX - offset)
        {
            SET_NET_ERROR(SOCK_EINVAL);
            return false;
        }

        return RecvAll(data + offset, size, flags);
    }

    // Отслеживает готовность группы сокетов через ::select(); оставляет в векторах только сработавшие сокеты.
    int Socket::Select(std::vector<Socket*>& readSock, std::vector<Socket*>& writeSock, std::vector<Socket*>& errorSock, long timeoutMs)
    {
        fd_set readSet;
        fd_set writeSet;
        fd_set errorSet;

        FD_ZERO(&readSet);
        FD_ZERO(&writeSet);
        FD_ZERO(&errorSet);

        socket_t maxDescriptor = 0;
        bool hasDescriptors    = false;

        // Переносит валидные дескрипторы из вектора в набор fd_set с проверкой лимита FD_SETSIZE.
        auto addSockets = [&](const std::vector<Socket*>& list, fd_set& set) -> bool
        {
            for (Socket* sock : list)
            {
                if (sock != nullptr && sock->IsCreated())
                {
                    socket_t h = sock->SockHandle();
#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
                    // В Winsock fd_set вмещает не более FD_SETSIZE элементов (по умолчанию 64).
                    if (!FD_ISSET(h, &set) && set.fd_count >= FD_SETSIZE)
                    {
                        SET_NET_ERROR(SOCK_EINVAL);
                        return false;
                    }
#else
                    // На Linux fd_set является битовой маской [0, FD_SETSIZE); выход за границу портит стек.
                    if (h < 0 || h >= FD_SETSIZE)
                    {
                        SET_NET_ERROR(SOCK_EINVAL);
                        return false;
                    }
#endif
                    FD_SET(h, &set);
                    hasDescriptors = true;
                    if (h > maxDescriptor)
                        maxDescriptor = h;
                }
            }

            return true;
        };

        if (!addSockets(readSock, readSet) || !addSockets(writeSock, writeSet) || !addSockets(errorSock, errorSet))
        {
            return -1;
        }

        // Если валидных сокетов нет — очищаем списки и возвращаем 0.
        if (!hasDescriptors)
        {
            readSock.clear();
            writeSock.clear();
            errorSock.clear();
            return 0;
        }

        // Готовим таймаут: nullptr означает бесконечное ожидание (timeoutMs < 0).
        timeval* pTimeout = nullptr;
        timeval tv = {};
        if (timeoutMs >= 0)
        {
            tv.tv_sec  = static_cast<decltype(tv.tv_sec)>(timeoutMs / 1000);
            tv.tv_usec = static_cast<decltype(tv.tv_usec)>((timeoutMs % 1000) * 1000);
            pTimeout   = &tv;
        }

#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
        int nfds = 0;
#else
        int nfds = static_cast<int>(maxDescriptor) + 1;
#endif

        int activity = ::select(nfds,
            readSock.empty()  ? nullptr : &readSet,
            writeSock.empty() ? nullptr : &writeSet,
            errorSock.empty() ? nullptr : &errorSet,
            pTimeout);

        if (activity == SOCKET_ERROR)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return -1;
        }

        if (activity > 0)
        {
            // Оставляем в каждом векторе только те сокеты, на которых реально сработало событие.
            auto filterSet = [](std::vector<Socket*>& vec, fd_set& set)
            {
                vec.erase(std::remove_if(vec.begin(), vec.end(), [&set](Socket* sock)
                    {
                        return sock == nullptr || sock->IsClosed() || !FD_ISSET(sock->SockHandle(), &set);
                    }), vec.end());
            };

            if (!readSock.empty())  filterSet(readSock, readSet);
            if (!writeSock.empty()) filterSet(writeSock, writeSet);
            if (!errorSock.empty()) filterSet(errorSock, errorSet);
        }
        else
        {
            // Таймаут истёк — очищаем все три списка.
            readSock.clear();
            writeSock.clear();
            errorSock.clear();
        }

        return activity;
    }

    // Опрос готовности одного сокета к чтению через poll/WSAPoll.
    bool Socket::PollRead(long timeoutMs) const noexcept
    {
        if (!IsCreated())
            return false;

        short revents = 0;
        int result = PollSingle(_handle, POLLIN, timeoutMs, revents);
        if (result < 0)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return false;
        }

        return (result > 0 && (revents & (POLLIN | POLLHUP)) != 0);
    }

    // Опрос готовности одного сокета к отправке данных без блокировки.
    bool Socket::PollWrite(long timeoutMs) const noexcept
    {
        if (!IsCreated())
            return false;

        short revents = 0;
        int result = PollSingle(_handle, POLLOUT, timeoutMs, revents);
        if (result < 0)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return false;
        }

        return (result > 0 && (revents & POLLOUT) != 0);
    }

    // Опрос наличия ошибки или разрыва соединения на сокете.
    bool Socket::PollError(long timeoutMs) const noexcept
    {
        if (!IsCreated())
            return false;

        // Ошибки (POLLERR/POLLHUP/POLLNVAL) возвращаются в revents автоматически даже при events == 0.
        short revents = 0;
        int result = PollSingle(_handle, 0, timeoutMs, revents);
        if (result < 0)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return false;
        }

        return (result > 0 && (revents & (POLLERR | POLLHUP | POLLNVAL)) != 0);
    }
}
