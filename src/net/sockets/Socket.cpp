#include "Socket.h"



// Статический объект: автоматически инициализирует подсистему Winsock (WSAStartup) при запуске программы до входа в main() и выполняет WSACleanup при выходе.
static other::net::sockets::WSAInitializer _wsa_init_global;


namespace other::net::sockets
{
	using namespace other::time;
	
	    // Проверяет, поддерживает ли текущая ОС IPv4-сокеты, если возвращает 'true' то подерживаеться.
		bool Socket::SupportsIPv4()
		{
			// Создаём тестовый IPv4-сокет.
			socket_t result = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

			// Проверяем, создан ли сокет.
			if (result == INVALID_SOCKET)
			{
				SET_NET_ERROR(SOCK_EBADF);
				return false;
			}
	
			// Если сокет создался успешно — сразу закрываем его, и возвращаем 'true'.
			CLOSE_SOCKET(result);
			return true;
		}

		// Проверяет, поддерживает ли текущая ОС IPv6-сокеты, если возвращает 'true' то подерживаеться.
		bool Socket::SupportsIPv6()
		{
			// Создаём тестовый IPv6-сокет.
			socket_t result = ::socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);

			// Проверяем, создан ли сокет.
			if (result == INVALID_SOCKET)
			{
				SET_NET_ERROR(SOCK_EBADF);
				return false;
			}

			// Если сокет создался успешно — сразу закрываем его, и возвращаем 'true'.
			CLOSE_SOCKET(result);
			return true;
		}

		// Создает пустой объект сокета с недействительным дескриптором (INVALID_SOCKET).
		Socket::Socket()
			: _handle(INVALID_SOCKET)
			, _family(0)
			, _type(0)
			, _protocol(0)
			, _blocking(true)
			, _start_t(other::time::DateTime::Zero)
			, _close_t(other::time::DateTime::Zero)
		{
		}

		// Конструктор-обертка для существующего системного дескриптора.
		Socket::Socket(socket_t handle)
			: _handle(INVALID_SOCKET)
			, _family(0)
			, _type(0)
			, _protocol(0)
			, _blocking(true)
			, _start_t(other::time::DateTime::Now())
			, _close_t(other::time::DateTime::Zero)
		{
			if (handle == INVALID_SOCKET)
			{
				SET_NET_ERROR(SOCK_EBADF);
				return;
			}

			// Получаем тип сокета (например, SOCK_STREAM для TCP или SOCK_DGRAM для UDP).
			socklen_t optlen = sizeof(_type);
			if (::getsockopt(handle, SOL_SOCKET, SO_TYPE, reinterpret_cast<char*>(&_type), &optlen) == SOCKET_ERROR)
			{
				_type = SOCK_STREAM; // Безопасный фоллбек по умолчанию.
			}

			// Узнаем точное семейство адресов (AF_INET или AF_INET6).
			sockaddr_storage addr = { 0 };
			socklen_t addrlen = sizeof(addr);
			if (::getsockname(handle, reinterpret_cast<sockaddr*>(&addr), &addrlen) != SOCKET_ERROR)
			{
				// Динамически определяем актуальное семейство (IPv4 или IPv6).
				_family = addr.ss_family;
			}

			// Получаем информацию о протоколе.
#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
			WSAPROTOCOL_INFO protoInfo;
			int protoLen = sizeof(protoInfo);
			if (::getsockopt(handle, SOL_SOCKET, SO_PROTOCOL_INFO, reinterpret_cast<char*>(&protoInfo), &protoLen) != SOCKET_ERROR)
			{
				_protocol = protoInfo.iProtocol;
			}
			else
#endif
			{
				// Ручной фоллбек на основе типа сокета для Linux и систем без SO_PROTOCOL_INFO.
				if (_type == SOCK_STREAM)
					_protocol = IPPROTO_TCP;
				else if (_type == SOCK_DGRAM)
					_protocol = IPPROTO_UDP;
				else
					_protocol = 0;
			}

			// Сохраняем дескриптор сокета.
			_handle = handle;
		}

		// Делегирующий конструктор: создает IPv6 dual-stack сокет по умолчанию для заданного типа и протокола.
		Socket::Socket(int type, int protocol) : Socket(AF_INET6, type, protocol)
		{
			if (!IsDualMode())
				SetDualMode(true);
		}

		// Инициализирует поля и создает новый физический сокет в операционной системе.
		Socket::Socket(int family, int type, int protocol)
			: _handle(INVALID_SOCKET)
			, _family(0)
			, _type(0)
			, _protocol(0)
			, _blocking(false)
			, _start_t(other::time::DateTime::Zero)
			, _close_t(other::time::DateTime::Zero)
		{
			// Создаём сокет.
			socket_t handle = ::socket(family, type, protocol);
			
			// Проверяем, создан ли сокет.
			if (handle != INVALID_SOCKET)
			{
				// Сохраняем дескриптор сокета.
			    _handle   = handle;
				
				// Сохраняем семейство адресов.
				_family   = family;
				
				// Сохраняем тип сокета.
			    _type     = type;
				
				// Сохраняем протокол сокета.
			    _protocol = protocol;
				
				// Заранее переводим сокет в режим блокировки.
			    _blocking = true;
				
				// Получаем локальное время для только что созданного сокета.
			    _start_t  = other::time::DateTime::Now();
			}
			else
			{
				SET_NET_ERROR(SOCK_EBADF);
			}
		}

		// Конструктор перемещения (Move Constructor).
		Socket::Socket(Socket&& other) noexcept
		{
			Dispose();

			_handle   = other._handle;
			_family   = other._family;
			_type     = other._type;
			_protocol = other._protocol;
			_blocking = other._blocking;
			_start_t  = other._start_t;
			_close_t  = other._close_t;

			// Обнуляем дескриптор источника, чтобы его деструктор не вызвал закрытие сокета.
			other._handle   = INVALID_SOCKET;
			other._family   = 0;
			other._type     = 0;
			other._protocol = 0;
			other._blocking = true;
			other._start_t  = other::time::DateTime::Zero;
			other._close_t  = other::time::DateTime::Zero;
		}

		// Оператор перемещения (Move Assignment).
		Socket& Socket::operator=(Socket&& other) noexcept
		{
			if (this != &other)
			{
				Dispose();

				_handle   = other._handle;
				_family   = other._family;
				_type     = other._type;
				_protocol = other._protocol;
				_blocking = other._blocking;
				_start_t  = other._start_t;
				_close_t  = other._close_t;
				
				// Обнуляем дескриптор источника, чтобы его деструктор не вызвал закрытие сокета.
				other._handle   = INVALID_SOCKET;
				other._family   = 0;
				other._type     = 0;
				other._protocol = 0;
				other._blocking = true;
				other._start_t  = other::time::DateTime::Zero;
				other._close_t  = other::time::DateTime::Zero;
			}

			return *this;
		}

		// Деструктор класса: автоматически вызывает Dispose(), обеспечивая соблюдение принципа RAII.
		Socket::~Socket()
		{
			Dispose();
		}

		// Упрощенный метод закрытия сокета без таймаута завершения.
		bool Socket::Close()
		{
			return Close(0);
		}

		// Полный метод закрытия сокета с опциональным таймером (linger timeout).
		bool Socket::Close(int timeout)
		{
			// Если сокет уже закрыт или не создан, повторно закрывать не нужно
			if (IsClosed())
				return false;

			// Если задан положительный таймаут, настраиваем linger-состояние для ожидания отправки данных
			if (timeout > 0)
				SetLingerState(true, (timeout / 1000));

			if (CLOSE_SOCKET(_handle) == SOCKET_ERROR)
			{
				SET_NET_ERROR(GET_NET_ERROR());
				return false;
			}

			// Помечаем дескриптор как недействительный.
			_handle = INVALID_SOCKET;

			// Фиксируем точное время закрытия сессии.
			_close_t = other::time::DateTime::Now();
			return true;
		}

		// Возвращает тип сокета (например, SOCK_STREAM).
		int Socket::SockType() const { return _type; }
		// Возвращает номер протокола (например, IPPROTO_TCP).
		int Socket::ProtocolType() const { return _protocol; }
		// Возвращает семейство адресов (например, AF_INET или AF_INET6).
		int Socket::AddressFamily() const { return _family; }
		// Возвращает сырой системный дескриптор сокета (SOCKET / socket_t).
		socket_t Socket::SockHandle() const { return _handle; }

		// Возвращает сетевой порт, на который привязан сокет (в хост-порядке байт).
		int Socket::LocalPort() const
		{
			// Проверка: если сокет не создан, получить порт невозможно
			if (!IsCreated())
				return -1;
		
			sockaddr_storage addr = { 0 };
			socklen_t addrlen = sizeof(addr);

			// Запрашиваем локальный адрес и порт через getsockname
			if (::getsockname(_handle, reinterpret_cast<sockaddr*>(&addr), &addrlen) == SOCKET_ERROR)
			{
				CheckSocketError(SOCKET_ERROR, "LocalPort: getsockname error.");
				return -1;
			}

			// Извлекаем и конвертируем порт в зависимости от семейства адресов (IPv4 / IPv6)
			if (addr.ss_family == AF_INET)
			{
				const sockaddr_in* addr4 = reinterpret_cast<const sockaddr_in*>(&addr);
				return ntohs(addr4->sin_port);
			}
			else if (addr.ss_family == AF_INET6)
			{
				const sockaddr_in6* addr6 = reinterpret_cast<const sockaddr_in6*>(&addr);
				return ntohs(addr6->sin6_port);
			}
			else
			{
				CheckSocketError(SOCKET_ERROR, "LocalPort: unknown socket family.");
				return -1;
			}

			return -1;
		}

		// Возвращает удаленный порт подключенного сокета.
		int Socket::RemotePort() const
		{
			if (!IsCreated())
				return -1;
			
			sockaddr_storage peerAddr = { 0 };
			socklen_t addrLen = sizeof(peerAddr);

			// Получаем информацию об удаленном конце сокета через getpeername.
			if (::getpeername(_handle, reinterpret_cast<sockaddr*>(&peerAddr), &addrLen) == SOCKET_ERROR)
			{
				int error = GET_NET_ERROR();

				// Проверяем ошибку "сокет не подключен".
#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
				if (error != WSAENOTCONN)
#else
				if (error != ENOTCONN)
#endif
				{
					CheckSocketError(SOCKET_ERROR, "RemotePort: getpeername error.");
				}
				return -1;
			}

			// Извлекаем порт в зависимости от семейства адресов (IPv4 или IPv6)
			if (peerAddr.ss_family == AF_INET)
			{
				const sockaddr_in* addr4 = reinterpret_cast<const sockaddr_in*>(&peerAddr);
				return ntohs(addr4->sin_port);
			}
			else if (peerAddr.ss_family == AF_INET6)
			{
				const sockaddr_in6* addr6 = reinterpret_cast<const sockaddr_in6*>(&peerAddr);
				return ntohs(addr6->sin6_port);
			}
			else
			{
				CheckSocketError(SOCKET_ERROR, "RemotePort: unknown socket family.");
				return -1;
			}

			return -1;
		}

		// Проверяет, привязан ли сокет к какому-либо адресу и порту (Bind выполнен успешно).
		bool Socket::IsBound() const
		{
			// Проверяем валидность дескриптора сокета
			if (!IsCreated())
				return false;

			sockaddr_storage addr = { 0 };
			socklen_t addrlen = sizeof(addr);

			// Вызываем getsockname для получения привязанного адреса
			int result = ::getsockname(_handle, reinterpret_cast<sockaddr*>(&addr), &addrlen);
			if (result == SOCKET_ERROR)
			{
				int error = GET_NET_ERROR();

				// Кросс-платформенная обработка ошибок невалидного сокета
#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
				if (error == WSAENOTSOCK)
					return false;
				if (error != WSAEINVAL)
#else
				if (error == EBADF)
					return false;
				if (error != EINVAL)
#endif
				{
					throw std::runtime_error("getsockname failed: " + std::to_string(error));
				}
				return false;
			}

			// Дополнительная проверка: если порт равен 0, сокет еще не привязан
			if (addr.ss_family == AF_INET)
			{
				const sockaddr_in* addr4 = reinterpret_cast<const sockaddr_in*>(&addr);
				return addr4->sin_port != 0;
			}
			else if (addr.ss_family == AF_INET6)
			{
				const sockaddr_in6* addr6 = reinterpret_cast<const sockaddr_in6*>(&addr);
				return addr6->sin6_port != 0;
			}
			else
			{
				CheckSocketError(SOCKET_ERROR, "IsBound: unknown socket family.");
				return false;
			}

			return true;
		}

		// Переводит сокет в режим прослушивания входящих подключений.
		bool Socket::Listen(int backlog)
		{
			// Проверяем, создан ли сокет перед вызовом listen
			if (!IsCreated())
				return false;

			// Проверяем корректность длины очереди подключений (backlog)
			if (backlog < 0)
			{
				SET_NET_ERROR(SOCK_EINVAL);
				return false;
			}

			// Вызываем системную функцию listen и проверяем результат на ошибку
			if (::listen(_handle, backlog) == SOCKET_ERROR)
			{
				SET_NET_ERROR(GET_NET_ERROR());
				return false;
			}

			return true;
		}

		// Принимает входящее сетевое соединение и возвращает новый высокоуровневый объект Socket для клиента.
		Socket Socket::Accept()
		{
			sockaddr_storage clientAddr = { 0 };
			socklen_t addrLen = sizeof(clientAddr);

			// Вызываем системную функцию accept для извлечения соединения из очереди прослушивания
			socket_t accepted = ::accept(_handle, reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);

			// Если принять соединение не удалось, возвращаем пустой невалидный объект Socket
			if (accepted == INVALID_SOCKET)
			{
				SET_NET_ERROR(GET_NET_ERROR());
				return Socket();
			}

			// Создаем и возвращаем новый объект Socket, передавая системный дескриптор 
			// и автоматически определенное семейство (поддерживает и IPv4, и IPv6 клиентов)
			return Socket(accepted);
		}
		
		// Привязывает сокет к порту на всех доступных интерфейсах (INADDR_ANY / in6addr_any).
		bool Socket::Bind(int port)
		{
			// Проверяем, создан ли сокет.
			if (!IsCreated())
				return false;

			// Настраиваем структуру адреса в зависимости от семейства (IPv6 с Dual Stack или чистый IPv4)
			if (_family == AF_INET6)
			{
				sockaddr_in6 service = { 0 };
				service.sin6_family = AF_INET6;
				service.sin6_addr = in6addr_any; // Прослушивание всех интерфейсов
				service.sin6_port = htons(static_cast<u_short>(port));

				// Выполняем системный bind для IPv6 / Dual Stack сокета
				if (::bind(_handle, reinterpret_cast<const sockaddr*>(&service), sizeof(service)) == SOCKET_ERROR)
				{
					CheckSocketError(SOCKET_ERROR, "Bind IPv6/DualStack failed.");
					return false;
				}
			}
			else if (_family == AF_INET)
			{
				sockaddr_in service = { 0 };
				service.sin_family = AF_INET;
				service.sin_addr.s_addr = htonl(INADDR_ANY);
				service.sin_port = htons(static_cast<u_short>(port));

				// Выполняем системный bind для IPv4 сокета
				if (::bind(_handle, reinterpret_cast<const sockaddr*>(&service), sizeof(service)) == SOCKET_ERROR)
				{
					CheckSocketError(SOCKET_ERROR, "Bind IPv4 failed.");
					return false;
				}
			}
			else
			{
				CheckSocketError(SOCKET_ERROR, "Bind: unknown socket family.");
				return false;
			}

			return true;
		}

		// Привязывает сокет к конкретному хосту (IP-адресу/домену) и порту с помощью разрешения имен (getaddrinfo).
		bool Socket::Bind(const std::string& host, int port)
		{
			if (!IsCreated())
				return false;

			addrinfo hints = { 0 };
			hints.ai_family = _family;
			hints.ai_socktype = _type;
			hints.ai_protocol = _protocol;
			hints.ai_flags = AI_PASSIVE;

			std::string portStr = std::to_string(port);
			addrinfo* res = nullptr;

			// Превращаем строковый хост и порт в структуру адресов через getaddrinfo
			if (::getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res) != 0)
			{
				return false;
			}

			bool success = false;
			// Перебираем полученные варианты адресов и пробуем привязаться к первому успешному
			for (addrinfo* ptr = res; ptr != nullptr; ptr = ptr->ai_next)
			{
				if (::bind(_handle, ptr->ai_addr, static_cast<int>(ptr->ai_addrlen)) != SOCKET_ERROR)
				{
					success = true;
					break;
				}
			}

			// Освобождаем память, выделенную под список адресов
			::freeaddrinfo(res);
			return success;
		}

		// Устанавливает таймаут для операций отправки данных (SO_SNDTIMEO).
		int Socket::SetSendTimeout(int milliseconds)
		{
			if (!IsCreated())
				return -1;

			int timeout = static_cast<int>(milliseconds);

			if (timeout < -1)
			{
				SET_NET_ERROR(SOCK_EINVAL);
				return -1;
			}

			if (timeout > 0 && timeout < 500)
				timeout = 500; // Минимальный порог (если требуется по логике)

#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
			int optlen = sizeof(int);
			if (::setsockopt(_handle, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), optlen) != SOCKET_ERROR)
				return 0;
#else
	// В Linux / POSIX требуется структура struct timeval (секунды и микросекунды)
			timeval tv;
			if (timeout == -1) 
			{
				tv.tv_sec = 0;  // 0 в POSIX timeval для SO_SNDTIMEO обычно означает отсутствие тайм-аута (бесконечность)
				tv.tv_usec = 0;
			}
			else 
			{
				tv.tv_sec = timeout / 1000;
				tv.tv_usec = (timeout % 1000) * 1000;
			}
			
			socklen_t optlen = sizeof(tv);
			if (::setsockopt(_handle, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&tv), optlen) != SOCKET_ERROR)
				return 0;
#endif
			return -1;
		}

		// Устанавливает таймаут для операций приема данных(SO_RCVTIMEO).
		int Socket::SetRecvTimeout(int milliseconds)
		{
			if (!IsCreated())
				return -1;

			int timeout = static_cast<int>(milliseconds);

			if (timeout < -1)
			{
				SET_NET_ERROR(SOCK_EINVAL);
				return -1;
			}

			if (timeout > 0 && timeout < 500)
				timeout = 500; // Минимальный порог тайм-аута (если требуется)

#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
			int optlen = sizeof(int);
			if (::setsockopt(_handle, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), optlen) != SOCKET_ERROR)
				return 0;
#else
	// В Linux / POSIX для SO_RCVTIMEO требуется структура struct timeval
			timeval tv;
			if (timeout == -1) 
			{
				tv.tv_sec = 0;  // 0 означает отсутствие тайм-аута (бесконечность)
				tv.tv_usec = 0;
			}
			else 
			{
				tv.tv_sec = timeout / 1000;
				tv.tv_usec = (timeout % 1000) * 1000;
			}

			socklen_t optlen = sizeof(tv);
			if (::setsockopt(_handle, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), optlen) != SOCKET_ERROR)
				return 0;
#endif
			return -1;
		}

		// Получает текущий таймаут отправки данных (SO_SNDTIMEO).
		int Socket::GetSendTimeout() const
		{
			if (!IsCreated())
				return -1;
			
#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
			int timeoutMs = 0;
			int optlen = sizeof(int);

			if (::getsockopt(_handle, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<char*>(&timeoutMs), &optlen) == SOCKET_ERROR)
				return -1;

			// В Windows 0 означает бесконечный таймаут, приводим к стандарту -1
			return (timeoutMs == 0) ? -1 : timeoutMs;
#else
			timeval tv = { 0 };
			socklen_t optlen = sizeof(tv);

			if (::getsockopt(_handle, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<char*>(&tv), &optlen) == SOCKET_ERROR)
				return -1;

			int64_t ms = static_cast<int64_t>(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
			// Если структура пустая (0), возвращаем -1 (бесконечность), иначе возвращаем посчитанные миллисекунды
			return (ms == 0) ? -1 : static_cast<int>(ms);
#endif
		}

		// Получает текущий таймаут получения данных (SO_RCVTIMEO).
		int Socket::GetRecvTimeout() const
		{
			if (!IsCreated())
				return -1;

#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
			int timeoutMs = 0;
			int optlen = sizeof(int);

			if (::getsockopt(_handle, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&timeoutMs), &optlen) == SOCKET_ERROR)
				return -1;

			return (timeoutMs == 0) ? -1 : timeoutMs;
#else
			timeval tv = { 0 };
			socklen_t optlen = sizeof(tv);

			if (::getsockopt(_handle, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&tv), &optlen) == SOCKET_ERROR)
				return -1;

			int64_t ms = static_cast<int64_t>(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
			return (ms == 0) ? -1 : static_cast<int>(ms);
#endif
		}

		// Устанавливает размер буфера отправки сокета (SO_SNDBUF).
		int Socket::SetSendBufferSize(int size)
		{
			if (!IsCreated())
				return -1;
			
			if (size <= 0)
			{
				SET_NET_ERROR(SOCK_EINVAL);
				return -1;
			}

			int optlen = sizeof(int);
			if (::setsockopt(_handle, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&size), optlen) != SOCKET_ERROR)
			{
				return 0;
			}

			return -1;
		}

		// Устанавливает размер буфера получения сокета (SO_RCVBUF).
		int Socket::SetRecvBufferSize(int size)
		{
			if (!IsCreated())
				return -1;

			if (size <= 0)
			{
				SET_NET_ERROR(SOCK_EINVAL);
				return -1; 
			}

			int optlen = sizeof(int);
			if (::setsockopt(_handle, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&size), optlen) != SOCKET_ERROR)
			{
				return 0;
			}

			return -1;
		}

		// Получает текущий размер буфера отправки сокета (SO_SNDBUF).
		int Socket::GetSendBufferSize() const
		{
			if (!IsCreated())
				return -1;
			
			int bufferSize = 0;
			socklen_t optlen = sizeof(bufferSize);

			int result = ::getsockopt(_handle, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<char*>(&bufferSize), &optlen);
			if (result == SOCKET_ERROR)
			{
				CheckSocketError(SOCKET_ERROR, "getsockopt(SO_SNDBUF)");
				return -1;
			}

			return bufferSize;
		}

		// Получает текущий размер буфера получения сокета (SO_RCVBUF).
		int Socket::GetRecvBufferSize() const
		{
			if (!IsCreated())
				return -1;
			

			int bufferSize = 0;
			socklen_t optlen = sizeof(bufferSize);

			int result = ::getsockopt(_handle, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char*>(&bufferSize), &optlen);
			if (result == SOCKET_ERROR)
			{
				CheckSocketError(SOCKET_ERROR, "getsockopt(SO_RCVBUF)");
				return -1;
			}

			return bufferSize; // Успех: возвращаем размер буфера напрямую
		}

		// Отключает дальнейший прием данных через сокет (shutdown SD_RECEIVE).
		void Socket::DoRecv()
		{
			#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
	const int how = SD_RECEIVE;
#else
	const int how = SHUT_RD;
#endif
	CheckSocketError(::shutdown(_handle, how), "shutdown(recv)");
		}

		// Отключает дальнейшую отправку данных через сокет (shutdown SD_SEND).
		void Socket::DoSend()
		{
			#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
	const int how = SD_SEND;
#else
	const int how = SHUT_WR;
#endif
	CheckSocketError(::shutdown(_handle, how), "shutdown(send)");
		}

		// Полностью отключает и прием, и отправку данных (shutdown SD_BOTH).
		void Socket::DoBoth()
		{
			#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
	const int how = SD_BOTH;
#else
	const int how = SHUT_RDWR;
#endif
	CheckSocketError(::shutdown(_handle, how), "shutdown(both)");
		}
		
		// Проверяет, создан ли сокет (дескриптор не равен INVALID_SOCKET).
		bool Socket::IsCreated() const 
		{ 
			if (_handle != INVALID_SOCKET)
				return true;

			SET_NET_ERROR(SOCK_EBADF);
			return false; 
		}
		// Проверяет, закрыт ли сокет (дескриптор равен INVALID_SOCKET).
		bool Socket::IsClosed() const { return _handle == INVALID_SOCKET; }

		// Проверяет активное состояние соединения путем отправки нулевого пакета (probe).
		bool Socket::IsConnected() const
		{
			if (!IsCreated())
				return false;

			// Пытаемся отправить 0 байт неблокирующим образом для проверки статуса соединения
			char dummy = 0;
			int result = ::send(_handle, &dummy, 0, 0);
			if (result == 0)
			{
				return true; // Успешно отправлено — соединение активно
			}
			else if (result == SOCKET_ERROR)
			{
				int error = GET_NET_ERROR();

				if (error == SOCK_EWOULDBLOCK || error == SOCK_EPROCLIM)
				{
					return true; // Сокет неблокирующий и операция заблокирована — соединение живое
				}
				else if (error == SOCK_ECONNRESET || error == SOCK_ECONNABORTED ||
					     error == SOCK_ENETRESET  || error == SOCK_ENETUNREACH  ||
					     error == SOCK_ESHUTDOWN  || error == SOCK_ENOTCONN)
				{
					return false; // Соединение разорвано или сброшено удаленной стороной
				}
				else
				{
					return false;
				}
			}

			return false;
		}

		// Возвращает количество байт, доступных для чтения во входном буфере сокета.
		int Socket::Available() const
		{
			if (!IsCreated())
				return -1;

#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
			u_long available = 0;
			// В Windows для сокетов используется ioctlsocket
			int ret = ::ioctlsocket(_handle, FIONREAD, &available);
#else
			int available = 0;
			// В UNIX/Linux используется стандартный ioctl из <sys/ioctl.h>
			int ret = ioctl(_handle, FIONREAD, &available);
#endif

			CheckSocketError(ret, "ioctl(FIONREAD).");
			return static_cast<int>(available);
		}

		// Возвращает локальный IP-адрес (и опционально порт) в виде строки.
		std::string Socket::LocalAddress(bool includePort) const
		{
			if (!IsCreated())
				return "";

			sockaddr_storage addr = { 0 };
			socklen_t addrlen = sizeof(addr);

			// Получаем локальные сетевые реквизиты через getsockname
			if (::getsockname(_handle, reinterpret_cast<sockaddr*>(&addr), &addrlen) == SOCKET_ERROR)
			{
				CheckSocketError(SOCKET_ERROR, "LocalAddress: getsockname.");
				return "";
			}

			if (addrlen == 0)
			{
				return "";
			}

			// Конвертируем бинарный адрес в текстовый вид для IPv4
			if (addr.ss_family == AF_INET)
			{
				const sockaddr_in* ipv4Addr = reinterpret_cast<const sockaddr_in*>(&addr);
				char ipStr[INET_ADDRSTRLEN] = { 0 };

				if (::inet_ntop(AF_INET, &(ipv4Addr->sin_addr), ipStr, INET_ADDRSTRLEN) == nullptr)
				{
					return "";
				}

				if (!includePort)
					return std::string(ipStr);
				else
					return std::string(ipStr) + ":" + std::to_string(ntohs(ipv4Addr->sin_port));
			}
			// Конвертируем бинарный адрес в текстовый вид для IPv6 (с учетом квадратных скобок)
			else if (addr.ss_family == AF_INET6)
			{
				const sockaddr_in6* ipv6Addr = reinterpret_cast<const sockaddr_in6*>(&addr);
				char ipStr[INET6_ADDRSTRLEN] = { 0 };

				if (::inet_ntop(AF_INET6, &(ipv6Addr->sin6_addr), ipStr, INET6_ADDRSTRLEN) == nullptr)
				{
					return "";
				}

				if (!includePort)
					return "[" + std::string(ipStr) + "]";
				else
					return "[" + std::string(ipStr) + "]:" + std::to_string(ntohs(ipv6Addr->sin6_port));
			}
			else
			{
				return "";
			}
		}

		// Возвращает удаленный IP-адрес (и опционально порт) подключенного узла.
		std::string Socket::RemoteAddress(bool includePort) const
		{
			if (!IsCreated())
				return "";

			sockaddr_storage addr = { 0 };
			socklen_t addrlen = sizeof(addr); // Используем socklen_t для совместимости с Linux

			// Получаем реквизиты удаленного соседа через getpeername
			if (::getpeername(_handle, reinterpret_cast<sockaddr*>(&addr), &addrlen) == SOCKET_ERROR)
			{
				CheckSocketError(SOCKET_ERROR, "RemoteAddress: getpeername.");
				//int error = GET_NET_ERROR();
				return "";
			}

			if (addrlen == 0)
			{
				return "";
			}

			// Конвертируем удаленный IPv4-адрес в строку
			if (addr.ss_family == AF_INET)
			{
				const sockaddr_in* ipv4Addr = reinterpret_cast<const sockaddr_in*>(&addr);
				char ipStr[INET_ADDRSTRLEN] = { 0 };

				if (inet_ntop(AF_INET, &(ipv4Addr->sin_addr), ipStr, INET_ADDRSTRLEN) == nullptr)
				{
					return "";
				}

				if (!includePort)
					return std::string(ipStr);
				else
					return std::string(ipStr) + ":" + std::to_string(ntohs(ipv4Addr->sin_port));
			}
			// Конвертируем удаленный IPv6-адрес в строку (в формате [IP]:port)
			else if (addr.ss_family == AF_INET6)
			{
				const sockaddr_in6* ipv6Addr = reinterpret_cast<const sockaddr_in6*>(&addr);
				char ipStr[INET6_ADDRSTRLEN] = { 0 };

				if (inet_ntop(AF_INET6, &(ipv6Addr->sin6_addr), ipStr, INET6_ADDRSTRLEN) == nullptr)
				{
					return "";
				}

				if (!includePort)
					return "[" + std::string(ipStr) + "]";
				else
					return "[" + std::string(ipStr) + "]:" + std::to_string(ntohs(ipv6Addr->sin6_port));
			}
			else
			{
				return "";
			}
		}

			
		// Управляет опцией многократного использования локального адреса и порта (SO_REUSEADDR).
		bool Socket::SetReuseAddress(bool enable)
		{
			if (!IsCreated())
				return false;

			// Используем стандартный int (1/0) вместо специфичного для Windows BOOL
			int optVal = enable ? 1 : 0;

			// Устанавливаем SO_REUSEADDR для предотвращения ошибок "Address already in use" при быстром перезапуске сервера
			if (::setsockopt(_handle, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&optVal), sizeof(optVal)) == SOCKET_ERROR)
			{
				CheckSocketError(SOCKET_ERROR, "setsockopt(SO_REUSEADDR).");
				return false;
			}

			return true;
		}

		// Проверяет, включена ли опция многократного использования адреса (SO_REUSEADDR) для сокета.
		bool Socket::IsReuseAddress() const
		{
			if (!IsCreated())
				return false;

			int optval = 0; // Значение опции теперь корректно типа int
			socklen_t optlen = sizeof(optval); // А размер — типа socklen_t

			// Запрашиваем состояние SO_REUSEADDR через getsockopt и проверяем на ошибки (исправлено имя функции в логе)
			CheckSocketError(::getsockopt(_handle, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&optval), &optlen), "getsockopt(SO_REUSEADDR).");

			// Возвращаем true, если опция активна (не равно нулю)
			return optval != 0;
		}

		// Включает или выключает отправку keep-alive пакетов для контроля соединения.
		bool Socket::SetKeepAlive(bool enable)
		{
			if (!IsCreated())
				return false;

			int optionValue = enable ? 1 : 0;
			return (::setsockopt(_handle, SOL_SOCKET, SO_KEEPALIVE, reinterpret_cast<const char*>(&optionValue), sizeof(optionValue)) != SOCKET_ERROR);
		}

		// Проверяет, включена ли опция SO_KEEPALIVE для данного сокета.
		bool Socket::IsKeepAlive() const
		{
			if (!IsCreated())
				return false;

			int optionValue = 0;
			socklen_t optionLen = sizeof(optionValue);

			if (::getsockopt(_handle, SOL_SOCKET, SO_KEEPALIVE, reinterpret_cast<char*>(&optionValue), &optionLen) == SOCKET_ERROR)
			{
				CheckSocketError(SOCKET_ERROR, "getsockopt(SO_KEEPALIVE).");
				return false;
			}

			return (optionValue != 0);
		}

		// Безопасно связывает объект с существующим системным дескриптором сокета.
		bool Socket::Attach(socket_t handle)
		{
			Dispose(); // Закрываем текущий дескриптор, если он был открыт
			_handle = handle;
			return _handle != INVALID_SOCKET;
		}

		// Отвязывает системный дескриптор от текущего объекта.
		socket_t Socket::Detach()
		{
			socket_t temp = _handle;
			_handle = INVALID_SOCKET;
			return temp;
		}

		// Переключает режим работы сокета между блокирующим и неблокирующим (FIONBIO).
		bool Socket::SetBlocking(bool enable)
		{
			if (!IsCreated())
				return false;

#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
			// В Windows используется ioctlsocket: 0 для блокирующего, 1 для неблокирующего
			u_long mode = enable ? 0 : 1;
			if (::ioctlsocket(_handle, FIONBIO, &mode) == SOCKET_ERROR)
			{
				CheckSocketError(SOCKET_ERROR, "ioctlsocket(FIONBIO) failed.");
				return false;
			}
#else
			// В UNIX-системах (Linux/macOS) неблокирующий режим настраивается через fcntl
			int flags = fcntl(_handle, F_GETFL, 0);
			if (flags == -1)
			{
				CheckSocketError(errno, "fcntl(F_GETFL) failed.");
				return false;
			}

			if (enable)
			{
				flags &= ~O_NONBLOCK; // Сбрасываем флаг — включаем блокирующий режим
			}
			else
			{
				flags |= O_NONBLOCK;  // Устанавливаем флаг — включаем неблокирующий режим
			}

			if (fcntl(_handle, F_SETFL, flags) == -1)
			{
				CheckSocketError(errno, "fcntl(F_SETFL) failed.");
				return false;
			}
#endif
			// Синхронизируем состояние.
			_blocking = enable;
			return true;
		}

		bool Socket::IsBlocking() const
		{
			if (!IsCreated())
				return false;

#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
			return _blocking;
#else
			// В UNIX-системах мы можем реально запросить текущий флаг у ядра
			int flags = fcntl(_handle, F_GETFL, 0);
			if (flags == -1)
			{
				return true;
			}
			return (flags & O_NONBLOCK) == 0;
#endif
		}

		// Установка параметров Linger (управление поведением закрытия соединения и ожиданием отправки остатков данных).
		int Socket::SetLingerState(bool enable, int seconds)
		{
			if (!IsCreated())
				return -1;

			// Настраиваем структуру linger для плавного/гарантированного закрытия (graceful close)
			linger l_opt = { 0 };
			l_opt.l_onoff = enable ? 1 : 0;
			l_opt.l_linger = static_cast<unsigned short>(seconds);

			socklen_t optlen = sizeof(l_opt); // Используем socklen_t для консистентности
			if (::setsockopt(_handle, SOL_SOCKET, SO_LINGER, reinterpret_cast<const char*>(&l_opt), optlen) == SOCKET_ERROR)
			{
				SET_NET_ERROR(GET_NET_ERROR());
				return -1;
			}

			return 0;
		}

		// Получение текущих параметров Linger сокета.
		int Socket::GetLingerState(bool& enable, int& seconds) const
		{
			if (!IsCreated())
				return -1;

			linger l_opt = { 0 };
			socklen_t optlen = sizeof(l_opt);

			// Запрашиваем состояние SO_LINGER через getsockopt
			if (::getsockopt(_handle, SOL_SOCKET, SO_LINGER, reinterpret_cast<char*>(&l_opt), &optlen) == SOCKET_ERROR)
			{
				SET_NET_ERROR(GET_NET_ERROR());
				return -1;
			}

			// Перекладываем значения в выходные параметры по ссылке
			enable = (l_opt.l_onoff != 0);
			seconds = static_cast<int>(l_opt.l_linger);

			return 0;
		}

		// Создает поверхностную копию параметров текущего сокета (создает новый сокет с теми же семейством, типом и протоколом).
		Socket Socket::SockClone()
		{
			return Socket(_family, _type, _protocol);
		}

		// Возвращает метку времени создания сокета.
		other::time::DateTime Socket::StartTime() const
		{
			return _start_t;
		}

		// Возвращает метку времени закрытия сокета.
		other::time::DateTime Socket::CloseTime() const
		{
			return _close_t;
		}

		// Возвращает время работы (аптайм) сокета с момента его создания.
		other::time::DateTime Socket::SockUptime() const
		{
			// Если сокет вообще не создавался, аптайм нулевой
			if (_start_t.Ticks() == 0)
			{
				return other::time::DateTime::Zero;
			}

			// Если сокет уже закрыт — считаем разницу: [Время закрытия] - [Время создания]
			if (!IsCreated() && _close_t.Ticks() > 0)
			{
				return _close_t - _start_t;
			}

			// Если сокет еще активен — считаем разницу: [Текущее время] - [Время создания]
			return other::time::DateTime::Now() - _start_t;
		}

		// Освобождает все ресурсы сокета, закрывает его дескриптор и сбрасывает метаданные.
		bool Socket::Dispose()
		{
			// Пытаемся закрыть системный сокет.
			bool result = Close();

			// Гарантированно сбрасываем метаданные объекта в начальное состояние, независимо от того, был ли сокет открыт прямо сейчас или уже закрыт ранее.
			_family   = 0;
			_type     = 0;
			_protocol = 0;
			_start_t  = other::time::DateTime::Zero;
			_close_t  = other::time::DateTime::Zero;
			_blocking = true;
			_handle   = INVALID_SOCKET;

			// Возвращаем true, если закрытие прошло успешно,  либо если сокет уже был закрыт/не создан (что тоже валидное состояние для dispose).
			return result || IsClosed();
		}

		// Настраивает режим Dual-Stack через опцию IPV6_V6ONLY.
		bool Socket::SetDualMode(bool enable)
		{
			// Настройка имеет смысл только для созданных сокетов типа AF_INET6
			if (!IsCreated() || _family != AF_INET6)
				return false;

			// Логика IPV6_V6ONLY инвертирована относительно абстракции DualMode:
			// Если enable = true (DualMode включен), IPV6_V6ONLY должен быть равен 0.
			// Если enable = false (только IPv6), IPV6_V6ONLY должен быть равен 1.
			int optval = enable ? 0 : 1;

			if (::setsockopt(_handle, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&optval), sizeof(optval)) == SOCKET_ERROR)
			{
				SET_NET_ERROR(GET_NET_ERROR());
				return false;
			}

			return true;
		}

		// Проверяет текущее состояние Dual-Stack режима.
		bool Socket::IsDualMode() const
		{
			// Проверка актуальна только для IPv6 сокетов
			if (!IsCreated() || _family != AF_INET6)
				return false;

			int optval = 0;
			socklen_t optlen = sizeof(optval);

			if (::getsockopt(_handle, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<char*>(&optval), &optlen) == SOCKET_ERROR)
			{
				SET_NET_ERROR(GET_NET_ERROR());
				return false;
			}

			// Если IPV6_V6ONLY равен 0, значит DualMode включен
			return (optval == 0);
		}

		// Устанавливает сетевое соединение с удаленным хостом по указанному IP-адресу/домену и порту.
		bool Socket::Connect(const std::string& host, int port)
		{
			if (!IsCreated())
				return false;

			// Настраиваем структуру hints для разрешения адреса через getaddrinfo
			addrinfo hints = { 0 };

			// Ключевой момент для Dual-Stack: если сокет IPv6, разрешаем AF_UNSPEC, чтобы getaddrinfo мог разрешить как IPv6, так и IPv4 адреса.
			hints.ai_family = (_family == AF_INET6) ? AF_UNSPEC : _family;
			hints.ai_socktype = _type;     // Тип сокета (например, SOCK_STREAM для TCP)
			hints.ai_protocol = _protocol; // Протокол (например, IPPROTO_TCP)

			std::string portStr = std::to_string(port);
			addrinfo* res = nullptr;

			// Выполняем разрешение сетевого адреса (DNS-запрос или парсинг IP)
			if (::getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res) != 0)
			{
				SET_NET_ERROR(GET_NET_ERROR());
				return false;
			}

			bool success = false;

			// Перебираем полученный список адресов
			for (addrinfo* ptr = res; ptr != nullptr; ptr = ptr->ai_next)
			{
				// Фильтрация несовместимых семейств адресов:
				// Если сокет AF_INET, мы не можем принимать IPv6 адреса (AF_INET6).
				if (_family == AF_INET && ptr->ai_family != AF_INET)
				{
					continue;
				}

				// Если сокет AF_INET6 без Dual-Stack, а прилетел IPv4 адрес — пропускаем (если требуется строгая изоляция).
				// Но для стандартного Dual-Stack сокета разрешаем IPv4-mapped или прямые адреса.

				// Пытаемся установить соединение с текущим вариантом адреса
				if (::connect(_handle, ptr->ai_addr, static_cast<int>(ptr->ai_addrlen)) != SOCKET_ERROR)
				{
					success = true;
					break; // Успешно подключились, прерываем цикл обхода
				}
			}

			// В обязательном порядке освобождаем динамическую память, выделенную getaddrinfo
			::freeaddrinfo(res);

			// Если ни один адрес не подошел, фиксируем ошибку подключения
			if (!success)
			{
				CheckSocketError(SOCKET_ERROR, "Connect failed: unable to connect to any resolved address.");
				return false;
			}

			return true;
		}

		// Закрывает текущее соединение сокета и опционально подготавливает его к повторному использованию.
		bool Socket::Disconnect(bool reuseSocket)
		{
			if (!IsCreated())
				return false;

			// Обязательный shutdown для остановки потока данных
			DoBoth();

			// Логика переиспользования
			if (reuseSocket)
			{
#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
				// --- WINDOWS (DisconnectEx) ---
				GUID guidDisconnectEx = WSAID_DISCONNECTEX;
				LPFN_DISCONNECTEX fnDisconnectEx = nullptr;
				DWORD bytes = 0;

				if (WSAIoctl(_handle, SIO_GET_EXTENSION_FUNCTION_POINTER, &guidDisconnectEx, sizeof(guidDisconnectEx), &fnDisconnectEx, sizeof(fnDisconnectEx), &bytes, nullptr, nullptr) == 0 && fnDisconnectEx)
				{
					// Пытаемся сбросить сокет с флагом повторного использования
					BOOL success = fnDisconnectEx(_handle, nullptr, TF_REUSE_SOCKET, 0);
					if (!success)
					{
						Close();
						return false; // Ошибка сброса через DisconnectEx
					}
					return true; // Успешно сброшен и готов к reuse!
				}
				else
				{
					Close();
					return false; // Функция недоступна
				}
#else
				// --- LINUX / UNIX ---
				// В Linux штатно переиспользовать TCP-сокет без закрытия нельзя, закрываем его
				Close();
				return false;
#endif
			}
			else
			{
				// Обычное полное закрытие сокета
				Close();
				return true;
			}
		}

		// Проверяет состояние сокетной опции TCP_NODELAY.
		bool Socket::IsNoDelay() const
		{
			if (!IsCreated())
				return false;

			int optval = 0;             // Значение опции — строго int
			socklen_t optlen = sizeof(optval); // Размер — socklen_t

			// Запрашиваем состояние TCP_NODELAY у стека протокола TCP
			if (::getsockopt(_handle, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char*>(&optval), &optlen) == SOCKET_ERROR)
			{
				SET_NET_ERROR(GET_NET_ERROR());
				return false;
			}

			// Возвращаем true, если Nagle алгоритм отключен (NoDelay включен)
			return (optval != 0);
		}

		// Настраивает опцию TCP_NODELAY для сокета (отключает алгоритм Нейгла для снижения задержек).
		bool Socket::SetNoDelay(bool enable)
		{
			if (!IsCreated())
				return false;

			// TCP_NODELAY принимает int (1 для отключения алгоритма Нейгла, 0 для включения)
			int optval = enable ? 1 : 0;
			if (::setsockopt(_handle, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&optval), sizeof(optval)) == SOCKET_ERROR)
			{
				SET_NET_ERROR(GET_NET_ERROR());
				return false;
			}

			return true;
		}

		// Получает значение срока жизни (TTL) для IP-пакетов.
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
				CheckSocketError(SOCKET_ERROR, "GetTtl: unknown socket family.");
				return -1;
			}

			return static_cast<short>(optval);
		}

		// Устанавливает значение срока жизни (TTL) для IP-пакетов.
		bool Socket::SetTtl(short value)
		{
			if (!IsCreated())
				return false;

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
				CheckSocketError(SOCKET_ERROR, "SetTtl: unknown socket family.");
				return false;
			}

			return true;
		}

		// Проверяет состояние запрета фрагментации IP-пакетов (Don't Fragment / DF).
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
				// В Linux для IPv4 аналогом DF является IP_MTU_DISCOVER
				if (::getsockopt(_handle, IPPROTO_IP, IP_MTU_DISCOVER, reinterpret_cast<char*>(&optval), &optlen) == SOCKET_ERROR)
				{
					SET_NET_ERROR(GET_NET_ERROR());
					return false;
				}
				// IP_PMTUDISC_WANT означает, что пакеты могут фрагментироваться (DF не установлен)
				return (optval != IP_PMTUDISC_WANT);
#endif
			}
			else if (_family == AF_INET6)
			{
				// Для IPv6 опция IPV6_DONTFRAG одинаково поддерживается и в Windows, и в Linux (RFC 3542)
				if (::getsockopt(_handle, IPPROTO_IPV6, IPV6_DONTFRAG, reinterpret_cast<char*>(&optval), &optlen) == SOCKET_ERROR)
				{
					SET_NET_ERROR(GET_NET_ERROR());
					return false;
				}
			}
			else
			{
				CheckSocketError(SOCKET_ERROR, "IsDontFragment: unknown socket family.");
				return false;
			}

			return (optval != 0);
		}


		// Настраивает флаг запрета фрагментации (Don't Fragment) для сокета.
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
				// В Linux для IPv4 управляем через Path MTU Discovery
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
				// Для IPv6 опция IPV6_DONTFRAG стандартна и одинакова везде (RFC 3542)
				int optval = value ? 1 : 0;
				if (::setsockopt(_handle, IPPROTO_IPV6, IPV6_DONTFRAG, reinterpret_cast<const char*>(&optval), sizeof(optval)) == SOCKET_ERROR)
				{
					SET_NET_ERROR(GET_NET_ERROR());
					return false;
				}
			}
			else
			{
				CheckSocketError(SOCKET_ERROR, "SetDontFragment: unknown socket family.");
				return false;
			}

			return true;
		}

		// Получает текущее состояние закольцовывания multicast-трафика.
		bool Socket::IsMulticastLoopback() const
		{
			if (!IsCreated())
				return false;

			int optval = 0;
			socklen_t optlen = sizeof(optval);
			int result = SOCKET_ERROR;

			if (_family == AF_INET)
			{
				// Запрашиваем состояние для IPv4.
				result = ::getsockopt(_handle, IPPROTO_IP, IP_MULTICAST_LOOP, reinterpret_cast<char*>(&optval), &optlen);
			}
			else if (_family == AF_INET6)
			{
				// Запрашиваем состояние для IPv6.
				result = ::getsockopt(_handle, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, reinterpret_cast<char*>(&optval), &optlen);
			}
			else
			{
				CheckSocketError(SOCKET_ERROR, "IsMulticastLoopback: unknown socket family.");
				return false;
			}

			// Если функция завершилась с ошибкой, возвращаем false по умолчанию.
			if (result == SOCKET_ERROR)
			{
				SET_NET_ERROR(GET_NET_ERROR());
				return false;
			}

			// Возвращаем true, если значение отличное от нуля.
			return (optval != 0);
		}

		// Включает или отключает эхо-возврат (loopback) исходящих multicast-пакетов обратно на этот же сокет.
		bool Socket::SetMulticastLoopback(bool enable)
		{
			if (!IsCreated())
				return false;

			int value = enable ? 1 : 0;

			if (_family == AF_INET)
			{
				// Настраиваем loopback для IPv4.
				return ::setsockopt(_handle, IPPROTO_IP, IP_MULTICAST_LOOP, reinterpret_cast<const char*>(&value), sizeof(value)) != SOCKET_ERROR;
			}
			else if (_family == AF_INET6)
			{
				// Настраиваем loopback для IPv6.
				return ::setsockopt(_handle, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, reinterpret_cast<const char*>(&value), sizeof(value)) != SOCKET_ERROR;
			}
			else
			{
				CheckSocketError(SOCKET_ERROR, "SetMulticastLoopback: unknown socket family.");
				return false;
			}

			return false;
		}

		// Включает или отключает возможность отправки широковещательных (broadcast) сообщений.
		bool Socket::SetBroadcast(bool enable)
		{
			if (!IsCreated())
				return false;

			int value = enable ? 1 : 0;
			return ::setsockopt(_handle, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&value), sizeof(value)) != SOCKET_ERROR;
		}

		// Проверяет, включена ли поддержка широковещательных рассылок (Broadcast) на сокете.
		bool Socket::IsBroadcast() const
		{
			if (!IsCreated())
				return false;

			int optval = 0;
			socklen_t optlen = sizeof(optval);

			// Запрашиваем состояние опции SO_BROADCAST на уровне сокета (SOL_SOCKET).
			// Если произошла системная ошибка, считаем, что broadcast выключен.
			if (::getsockopt(_handle, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<char*>(&optval), &optlen) == SOCKET_ERROR)
			{
				SET_NET_ERROR(GET_NET_ERROR());
				return false;
			}

			// Возвращаем true, если значение опции не равно нулю.
			return (optval != 0);
		}

		// Настраивает эксклюзивное использование порта через опцию SO_EXCLUSIVEADDRUSE.
		bool Socket::SetExclusiveAddressUse(bool exclusive)
		{
			if (!IsCreated())
				return false;

#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
			// SO_EXCLUSIVEADDRUSE поддерживается только в Windows (Winsock)
			int optval = exclusive ? 1 : 0;
			if (::setsockopt(_handle, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&optval), sizeof(optval)) == SOCKET_ERROR)
			{
				SET_NET_ERROR(GET_NET_ERROR());
				return false;
			}

			return true;
			
#else
			// В Linux/POSIX логика эксклюзивности достигается иначе (или по умолчанию), 
			// поэтому здесь просто оставляем пустым или настраиваем REUSEADDR.
			LIBOTHER_UNUSED(exclusive);
			return false;
#endif
		}

		// Проверяет статус эксклюзивного использования порта сокетом.
		bool Socket::IsExclusiveAddressUse() const
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

			return (optval != 0);
#else
			// В Linux эта опция отсутствует
			return false;
#endif
		}

		// Отправляет данные через сокет (базовая версия).
		int Socket::Send(const char* data, int size, int flags)
		{
			if (!IsCreated())
				return -1;

			// Проверяем валидность указателя и размера буфера.
			if (data == nullptr || size <= 0)
			{
				SET_NET_ERROR(SOCK_EINVAL);
				return -1;
			}

			// Вызываем системную функцию send.
			int result = ::send(_handle, data, size, flags);
			if (result == SOCKET_ERROR)
			{
				int error = GET_NET_ERROR(); // Используем кросс-платформенный макрос получения ошибки.

				// Если сокет неблокирующий и буфер заполнен, возвращаем 0.
#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
				if (error == WSAEWOULDBLOCK)
				{
					return 0;
				}
#else
				if (error == EWOULDBLOCK || error == EAGAIN)
				{
					return 0;
				}
#endif

				CheckSocketError(SOCKET_ERROR, "send socket error");
				return -1;
			}

			return result; // Возвращает количество успешно отправленных байт.
		}

		// Отправляет данные через сокет с возможностью указания смещения (offset) в буфере.
		int Socket::SendOffset(const char* data, int offset, int size, int flags)
		{
			if (!IsCreated())
				return -1;

			if (data == nullptr || size <= 0 || offset < 0)
			{
				SET_NET_ERROR(SOCK_EINVAL);
				return -1;
			}

			// Защита от переполнения при вычислении указателя с учетом смещения.
			if (offset > INT_MAX - size)
			{
				CheckSocketError(SOCKET_ERROR, "Send failed: Offset and size overflow.");
				return -1;
			}

			// Передаем в send указатель, сдвинутый на offset.
			int result = ::send(_handle, data + offset, size, flags);
			if (result == SOCKET_ERROR)
			{
				int error = GET_NET_ERROR();

#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
				if (error == WSAEWOULDBLOCK)
				{
					return 0;
				}
#else
				if (error == EWOULDBLOCK || error == EAGAIN)
				{
					return 0;
				}
#endif

				CheckSocketError(SOCKET_ERROR, "send with offset error");
				return -1;
			}

			return result;
		}

		// Отправляет датаграмму (UDP) на указанный адрес (строковый хост/IP и порт).
		int Socket::SendTo(const char* data, int size, const std::string& toHost, int port, int flags)
		{
			if (!IsCreated())
				return -1;

			if (data == nullptr && size > 0)
			{
				SET_NET_ERROR(SOCK_EINVAL);
				return -1;
			}

			if (size < 0)
			{
				CheckSocketError(SOCKET_ERROR, "SendTo failed: size cannot be negative.");
				return -1;
			}

			if (size == 0)
			{
				return 0; // Отправлять нечего.
			}

			// Настраиваем hints для разрешения целевого адреса назначения через getaddrinfo.
			addrinfo hints    = { 0 };
			hints.ai_family   = _family;       // Поддерживаем текущее семейство сокета.
			hints.ai_socktype = _type;         // Например, SOCK_DGRAM для UDP.
			hints.ai_protocol = _protocol;

			std::string portStr = std::to_string(port);
			addrinfo* res = nullptr;

			// Разрешаем адрес назначения.
			if (::getaddrinfo(toHost.c_str(), portStr.c_str(), &hints, &res) != 0)
			{
				CheckSocketError(SOCKET_ERROR, "SendTo failed: getaddrinfo resolution error.");
				return -1;
			}

			int bytesSent = SOCKET_ERROR;

			// Перебираем разрешенные адреса и пытаемся отправить пакет.
			for (addrinfo* ptr = res; ptr != nullptr; ptr = ptr->ai_next)
			{
				bytesSent = static_cast<int>(::sendto(_handle, data, size, flags, ptr->ai_addr, static_cast<int>(ptr->ai_addrlen)));
				if (bytesSent != SOCKET_ERROR)
				{
					break; // Успешно отправили, выходим из цикла.
				}
			}

			// Освобождаем память адресов.
			::freeaddrinfo(res);

			if (bytesSent == SOCKET_ERROR)
			{
				CheckSocketError(SOCKET_ERROR, "sendto failed");
			}

			return bytesSent;
		}
		
		int Socket::SendToOffset(const char* data, int offset, int size, const std::string& toHost, int port, int flags)
{
	if (data == nullptr || offset < 0 || size < 0)
	{
		SET_NET_ERROR(SOCK_EINVAL);
		return -1;
	}

	if (size > INT_MAX - offset)
	{
		CheckSocketError(SOCKET_ERROR, "SendToOffset failed: Offset and size overflow.");
		return -1;
	}

	return SendTo(data + offset, size, toHost, port, flags);
}

		// Принимает данные из сокета в предоставленный буфер (базовая версия).
		int Socket::Recv(char* data, int size, int flags)
		{
			if (!IsCreated())
				return -1;

			if (data == nullptr || size <= 0)
			{
				SET_NET_ERROR(SOCK_EINVAL);
				return -1;
			}

			// Прямой системный вызов recv без лишних аллокаций памяти.
			int result = ::recv(_handle, data, size, flags);
			if (result == SOCKET_ERROR)
			{
				int error = GET_NET_ERROR();

#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
				if (error == WSAEWOULDBLOCK)
				{
					return 0; // Неблокирующий сокет: данных для чтения пока нет.
				}
#else
				if (error == EWOULDBLOCK || error == EAGAIN)
				{
					return 0;
				}
#endif

				CheckSocketError(SOCKET_ERROR, "recv error");
				return -1;
			}

			return result; // Возвращает количество прочитанных байт (0 означает закрытие соединения клиентом).
		}

		// Принимает входящие данные из сокета со смещением в целевом буфере.
		int Socket::RecvOffset(char* data, int offset, int size, int flags)
		{
			if (!IsCreated())
				return -1;

			// Проверяем валидность указателя, размера и смещения.
			if (data == nullptr || size <= 0 || offset < 0)
			{
				SET_NET_ERROR(SOCK_EINVAL);
				return -1;
			}

			// Защита от целочисленного переполнения при вычислении смещенного указателя.
			if (offset > INT_MAX - size)
			{
				SET_NET_ERROR(SOCK_EINVAL);
				return -1;
			}

			// Вызываем системный recv, записывая данные в буфер, смещенный на `offset`.
			int result = ::recv(_handle, data + offset, size, flags);
			if (result == SOCKET_ERROR)
			{
				int error = GET_NET_ERROR();

#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
				if (error == WSAEWOULDBLOCK)
				{
					return 0;
				}
#else
				if (error == EWOULDBLOCK || error == EAGAIN)
				{
					return 0;
				}
#endif

				CheckSocketError(SOCKET_ERROR, "recv with offset error");
				return -1;
			}

			return result; // Возвращает количество принятых байт (0 — соединение закрыто).
		}

		// Принимает датаграмму (UDP) и возвращает адрес и порт отправителя.
		int Socket::RecvFrom(char* data, int size, std::string& fromHost, int& fromPort, int flags)
		{
			if (!IsCreated())
				return -1;

			if (data == nullptr || size <= 0)
			{
				SET_NET_ERROR(SOCK_EINVAL);
				return -1;
			}

			sockaddr_storage clientAddr = { 0 };
			socklen_t addrLen = sizeof(clientAddr);

			// Вызываем системный recvfrom
			int result = ::recvfrom(_handle, data, size, flags, reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
			if (result == SOCKET_ERROR)
			{
				int error = GET_NET_ERROR();

#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
				if (error == WSAEWOULDBLOCK)
				{
					return 0;
				}
#else
				if (error == EWOULDBLOCK || error == EAGAIN)
				{
					return 0;
				}
#endif

				CheckSocketError(SOCKET_ERROR, "recvfrom error");
				return -1;
			}

			// Извлекаем строковый IP-адрес и порт отправителя через getnameinfo
			char hostBuffer[NI_MAXHOST] = { 0 };
			char serviceBuffer[NI_MAXSERV] = { 0 };

			if (::getnameinfo(reinterpret_cast<sockaddr*>(&clientAddr), addrLen, hostBuffer, sizeof(hostBuffer), serviceBuffer, sizeof(serviceBuffer), NI_NUMERICHOST | NI_NUMERICSERV) == 0)
			{
				fromHost = hostBuffer;
				fromPort = std::stoi(serviceBuffer);
			}
			else
			{
				fromHost.clear();
				fromPort = 0;
			}

			return result; // Возвращает количество принятых байт датаграммы.
		}

		// Принимает датаграмму (UDP) со смещением в буфере.
		int Socket::RecvFromOffset(char* data, int offset, int size, std::string& fromHost, int& fromPort, int flags)
		{
			if (data == nullptr || offset < 0 || size < 0)
			{
				SET_NET_ERROR(SOCK_EINVAL);
				return -1;
			}

			if (size > INT_MAX - offset)
			{
				SET_NET_ERROR(SOCK_EINVAL);
				return -1;
			}

			return RecvFrom(data + offset, size, fromHost, fromPort, flags);
		}

		// Гарантированно вычитывает ровно `size` байт из TCP-потока (цикл добора данных).
		bool Socket::RecvAll(char* data, int size, int flags)
		{
			if (!IsCreated() || data == nullptr || size <= 0)
			{
				SET_NET_ERROR(SOCK_EINVAL);
				return false;
			}

			int totalReceived = 0;
			while (totalReceived < size)
			{
				int bytes = Recv(data + totalReceived, size - totalReceived, flags);

				// Если произошла ошибка (-1) или соединение разорвано клиентом (0).
				if (bytes <= 0)
				{
					return false;
				}

				totalReceived += bytes;
			}

			return true;
		}

		// Гарантированно вычитывает ровно `size` байт со смещением в буфере.
		bool Socket::RecvAllOffset(char* data, int offset, int size, int flags)
		{
			if (data == nullptr || offset < 0 || size < 0)
			{
				SET_NET_ERROR(SOCK_EINVAL);
				return false;
			}

			if (size > INT_MAX - offset)
			{
				SET_NET_ERROR(SOCK_EINVAL);
				return false;
			}

			return RecvAll(data + offset, size, flags);
		}

		// Статический метод Select для мультиплексирования группы сокетов.
		int Socket::Select(std::vector<Socket*>& readSockets, std::vector<Socket*>& writeSockets, std::vector<Socket*>& errorSockets, long timeoutMs)
		{
			fd_set readSet;
			fd_set writeSet;
			fd_set errorSet;

			FD_ZERO(&readSet);
			FD_ZERO(&writeSet);
			FD_ZERO(&errorSet);

			socket_t maxDescriptor = 0;
			bool hasDescriptors = false;

			// Заполняем наборы дескрипторов для чтения.
			for (Socket* sock : readSockets)
			{
				if (sock != nullptr && sock->IsCreated())
				{
					socket_t h = sock->SockHandle();
					FD_SET(h, &readSet);
					hasDescriptors = true;
					if (h > maxDescriptor)
						maxDescriptor = h;
				}
			}

			// Заполняем наборы дескрипторов для записи.
			for (Socket* sock : writeSockets)
			{
				if (sock != nullptr && sock->IsCreated())
				{
					socket_t h = sock->SockHandle();
					FD_SET(h, &writeSet);
					hasDescriptors = true;
					if (h > maxDescriptor)
						maxDescriptor = h;
				}
			}

			// Заполняем наборы дескрипторов для ошибок.
			for (Socket* sock : errorSockets)
			{
				if (sock != nullptr && sock->IsCreated())
				{
					socket_t h = sock->SockHandle();
					FD_SET(h, &errorSet);
					hasDescriptors = true;
					if (h > maxDescriptor)
						maxDescriptor = h;
				}
			}

			// Если валидных сокетов не оказалось вообще.
			if (!hasDescriptors)
			{
				return 0;
			}

			// Настраиваем структуру тайм-аута для select.
			timeval* pTimeout = nullptr;
			timeval tv = { 0 };

			if (timeoutMs >= 0)
			{
				tv.tv_sec = timeoutMs / 1000;
				tv.tv_usec = (timeoutMs % 1000) * 1000;
				pTimeout = &tv;
			}
			// Если timeoutMs < 0, pTimeout остается nullptr (бесконечное ожидание).

			// Кросс-платформенный вызов select:
			// Под Windows первый аргумент игнорируется, но под Linux нужен (максимальный дескриптор + 1).
#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
			int nfds = 0;
#else
			int nfds = static_cast<int>(maxDescriptor) + 1;
#endif

			int activity = ::select(nfds,
				readSockets.empty() ? nullptr : &readSet,
				writeSockets.empty() ? nullptr : &writeSet,
				errorSockets.empty() ? nullptr : &errorSet,
				pTimeout);

			if (activity == SOCKET_ERROR)
			{
				CheckSocketError(SOCKET_ERROR, "Select failed.");
				return -1;
			}

			// Фильтруем входящие векторы, оставляя только те сокеты, которые стали активными.
			if (activity > 0)
			{
				auto filterSet = [](std::vector<Socket*>& vec, fd_set& set) 
				{
					vec.erase(std::remove_if(vec.begin(), vec.end(), [&set](Socket* sock) 
						{
							return sock == nullptr || !sock->IsCreated() || !FD_ISSET(sock->SockHandle(), &set);
						}), vec.end());
				};

				if (!readSockets.empty())  
					filterSet(readSockets, readSet);

				if (!writeSockets.empty()) 
					filterSet(writeSockets, writeSet);

				if (!errorSockets.empty()) 
					filterSet(errorSockets, errorSet);
			}
			else
			{
				// Таймаут истек (activity == 0), очищаем векторы.
				readSockets.clear();
				writeSockets.clear();
				errorSockets.clear();
			}

			return activity;
		}

		// Проверка готовности текущего сокета к чтению.
		bool Socket::PollRead(long timeoutMs) const
		{
			if (!IsCreated())
				return false;

			fd_set readSet;
			FD_ZERO(&readSet);
			FD_SET(_handle, &readSet);

			timeval* pTimeout = nullptr;
			timeval tv = { 0 };
			if (timeoutMs >= 0)
			{
				tv.tv_sec = timeoutMs / 1000;
				tv.tv_usec = (timeoutMs % 1000) * 1000;
				pTimeout = &tv;
			}

#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
			int nfds = 0;
#else
			int nfds = static_cast<int>(_handle) + 1;
#endif

			int result = ::select(nfds, &readSet, nullptr, nullptr, pTimeout);
			return (result > 0 && FD_ISSET(_handle, &readSet));
		}

		// Проверка готовности текущего сокета к записи.
		bool Socket::PollWrite(long timeoutMs) const
		{
			if (!IsCreated())
				return false;

			fd_set writeSet;
			FD_ZERO(&writeSet);
			FD_SET(_handle, &writeSet);

			timeval* pTimeout = nullptr;
			timeval tv = { 0 };
			if (timeoutMs >= 0)
			{
				tv.tv_sec = timeoutMs / 1000;
				tv.tv_usec = (timeoutMs % 1000) * 1000;
				pTimeout = &tv;
			}

#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
			int nfds = 0;
#else
			int nfds = static_cast<int>(_handle) + 1;
#endif

			int result = ::select(nfds, nullptr, &writeSet, nullptr, pTimeout);
			return (result > 0 && FD_ISSET(_handle, &writeSet));
		}

		// Проверка наличия ошибок или исключительных состояний на сокете.
		bool Socket::PollError(long timeoutMs) const
		{
			if (!IsCreated())
				return false;

			fd_set errorSet;
			FD_ZERO(&errorSet);
			FD_SET(_handle, &errorSet);

			timeval* pTimeout = nullptr;
			timeval tv = { 0 };
			if (timeoutMs >= 0)
			{
				tv.tv_sec = timeoutMs / 1000;
				tv.tv_usec = (timeoutMs % 1000) * 1000;
				pTimeout = &tv;
			}

#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
			int nfds = 0;
#else
			int nfds = static_cast<int>(_handle) + 1;
#endif

			// Используем exceptfds (четвертый аргумент select).
			int result = ::select(nfds, nullptr, nullptr, &errorSet, pTimeout);
			return (result > 0 && FD_ISSET(_handle, &errorSet));
		}
}
