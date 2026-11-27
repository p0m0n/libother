#ifndef LIBOTHER_NET_TCP_CLIENT_H
#define LIBOTHER_NET_TCP_CLIENT_H


#include "Socket.h"
#include "StreamSocket.h"
#include "ByteBuffer.h"


namespace other::net::sockets
{
    /**
     * @struct ClientConfig
     * @brief Параметры настройки и поведения клиентского подключения `TCPClient`.
     */
    struct ClientConfig
    {
        int    connectTimeoutMs = 5000;                                 ///< Таймаут подключения по умолчанию в миллисекундах (`< 0` = без лимита).
        int    recvTimeoutMs    = -1;                                   ///< Таймаут приёма (`SO_RCVTIMEO`) в миллисекундах (`-1` = бесконечно).
        int    sendTimeoutMs    = -1;                                   ///< Таймаут отправки (`SO_SNDTIMEO`) в миллисекундах (`-1` = бесконечно).
        size_t maxPacketSize    = StreamSocket::DefaultMaxPacketSize;   ///< Лимит размера одного пакета `ByteBuffer` (64 МБ, защита от DoS).
        bool   dualStack        = true;                                 ///< Для `AF_INET6` включать Dual-Stack (`IPV6_V6ONLY = 0`, поддержка IPv4 и IPv6).
        bool   noDelay          = true;                                 ///< Включать `TCP_NODELAY` (отключение алгоритма Нейгла для низкой задержки).
        bool   keepAlive        = false;                                ///< Включать `SO_KEEPALIVE` на клиентском сокете.
    };

    /**
     * @class TCPClient
     * @brief Кроссплатформенный TCP-клиент.
     *
     * Инкапсулирует клиентский сокет `Socket`, поддерживает подключение с таймаутом,
     * повторное подключение по сохранённому адресу (`Reconnect`), работу через `StreamSocket`
     * и бинарный пакетный обмен (`SendPacket` / `ReadPacket` через `ByteBuffer`).
     *
     * Политика владения и потокобезопасности:
     * - Эксклюзивное владение сокетом (`Socket` move-only): копирование запрещено, поддерживается перемещение.
     * - Объект не синхронизирован внутренними мьютексами; при работе из нескольких потоков синхронизацию
     *   выполняет вызывающий код.
     *
     * Политика обработки ошибок:
     * Сетевые ошибки не выбрасывают исключений — методы возвращают `false` / `-1` / пустой `Socket`
     * или `ByteBuffer`, а точный код ошибки ОС доступен через `GET_NET_ERROR()`. 
	 * Исключение `std::bad_alloc` возможно только при нехватке памяти для строк, векторов или `ByteBuffer`.
     */
    class TCPClient
    {
    private:
        Socket       _socket;           ///< Базовый клиентский TCP-сокет под управлением RAII.
        ClientConfig _config;           ///< Активная конфигурация клиента.
        std::string  _lastHost;         ///< Последний целевой хост/IP для метода `Reconnect()`.
        int          _lastPort;         ///< Последний целевой порт для метода `Reconnect()`.
        int          _lastFamily;       ///< Семейство адресов последнего подключения (`AF_INET`, `AF_INET6` или `0`).
        bool         _hasLastEndpoint;  ///< Флаг наличия сохранённого адреса для `Reconnect()`.
        bool         _boundLocally;     ///< `true`, если текущий неиспользованный сокет привязан через `BindLocal()`.
		
	protected:
        /**
         * @brief Внутренний помощник: создаёт сокет с заданным семейством и применяет настройки из `_config`.
         * @param [in] family Семейство протоколов (`AF_INET` или `AF_INET6`).
         * @return `true` при успешном создании и настройке сокета; `false` при ошибке ОС.
         * @note Исключения не выбрасывает (`noexcept`).
         */
        bool CreateAndConfigureSocket(int family) noexcept;
        /**
         * @brief Внутренний помощник: применяет таймауты и флаги (`TCP_NODELAY`, `SO_KEEPALIVE`) после подключения.
         * @note Исключения не выбрасывает (`noexcept`).
         */
        void ApplyPostConnectOptions() noexcept;

    public:
        /**
         * @brief Создаёт неподключённый экземпляр `TCPClient` с конфигурацией по умолчанию.
         * @param [in] config Параметры клиентского соединения (`ClientConfig`).
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        explicit TCPClient(const ClientConfig& config = ClientConfig{});
        /**
         * @brief Создаёт клиент и сразу выполняет подключение к указанному хосту и порту.
         * @param [in] host   IP-адрес или доменное имя сервера (например `"127.0.0.1"`, `"localhost"`).
         * @param [in] port   Порт сервера (`0..65535`).
         * @param [in] config Параметры клиентского соединения (`ClientConfig`).
         * @note При сбое `Connected()` вернёт `false`, а код ошибки доступен через `GET_NET_ERROR()`.
         * @throws std::bad_alloc При нехватке памяти при разрешении адреса и сохранении имени хоста.
         */
        TCPClient(const std::string& host, int port, const ClientConfig& config = ClientConfig{});
        /**
         * @brief Создаёт клиент и сразу подключается с явным указанием семейства протоколов (`AF_INET` или `AF_INET6`).
         * @param [in] family Семейство адресов (`AF_INET` или `AF_INET6`).
         * @param [in] host   IP-адрес или доменное имя сервера.
         * @param [in] port   Порт сервера (`0..65535`).
         * @param [in] config Параметры клиентского соединения (`ClientConfig`).
         * @note При сбое `Connected()` вернёт `false`, а код ошибки доступен через `GET_NET_ERROR()`.
         * @throws std::bad_alloc При нехватке памяти при разрешении адреса и сохранении имени хоста.
         */
        TCPClient(int family, const std::string& host, int port, const ClientConfig& config = ClientConfig{});
        /**
         * @brief Оборачивает уже созданный/подключённый объект `Socket` (например, принятый сервером).
         * @param [in,out] socket Перемещаемый объект `Socket` (после вызова становится пустым).
         * @param [in]     config Параметры клиентского соединения (`ClientConfig`).
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        explicit TCPClient(Socket&& socket, const ClientConfig& config = ClientConfig{});
        /**
         * @brief Деструктор: закрывает соединение и освобождает системные ресурсы сокета (RAII).
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        ~TCPClient() noexcept;

        // Копирование запрещено: клиент владеет уникальным дескриптором сокета.
        TCPClient(const TCPClient&) = delete;
        TCPClient& operator=(const TCPClient&) = delete;
        /**
         * @brief Конструктор перемещения: переносит сокет, конфигурацию и параметры переподключения за $O(1)$.
         * @param [in,out] other Исходный `TCPClient` (после перемещения становится закрытым и неподключённым).
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        TCPClient(TCPClient&& other) noexcept;
        /**
         * @brief Оператор перемещающего присваивания: закрывает текущее соединение и забирает состояние `other`.
         * @param [in,out] other Исходный `TCPClient` (после перемещения обнуляется).
         * @return Ссылка на текущий объект (`*this`).
         * @note Гарантированно не выбрасывает исключений (`noexcept`). Безопасен при самоприсваивании.
         */
        TCPClient& operator=(TCPClient&& other) noexcept;


        ///////////////////////////////////////////////////////////////////////
        ///            КОНФИГУРАЦИЯ И ПОДКЛЮЧЕНИЕ                             ///
        ///////////////////////////////////////////////////////////////////////
        /**
         * @brief Возвращает текущую конфигурацию клиента.
         * @return Константная ссылка на `ClientConfig`.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        const ClientConfig& Config() const noexcept { return _config; }
        /**
         * @brief Обновляет конфигурацию клиента (таймауты и опции применяются к текущему сокету, если он открыт).
         * @param [in] config Новая конфигурация `ClientConfig`.
         * @note Исключения не выбрасывает (`noexcept`).
         */
        void SetConfig(const ClientConfig& config) noexcept;
        /**
         * @brief Подключается к удалённому серверу по хосту/IP и порту с таймаутом из `_config.connectTimeoutMs`.
         *
         * Если клиент уже был подключён, предварительно закрывает старый сокет (кроме случая, когда сокет
         * был специально привязан через `BindLocal()` перед первым подключением).
         * @param [in] host      IP-адрес или доменное имя сервера.
         * @param [in] port      Порт сервера (`0..65535`).
         * @param [in] timeoutMs Таймаут подключения в миллисекундах (`-2` = взять из `_config.connectTimeoutMs`,
         *                       `< 0` = ждать бесконечно, `>= 0` = ограничить указанным числом мс).
         * @return `true` при успешном подключении; `false` при ошибке (код доступен через `GET_NET_ERROR()`).
         * @throws std::bad_alloc При нехватке памяти при разрешении адреса и сохранении строки `host`.
         */
        bool Connect(const std::string& host, int port, int timeoutMs = -2);
        /**
         * @brief Подключается к удалённому серверу с явным выбором семейства адресов (`AF_INET` или `AF_INET6`).
         * @param [in] family    Семейство протоколов (`AF_INET` или `AF_INET6`).
         * @param [in] host      IP-адрес или доменное имя сервера.
         * @param [in] port      Порт сервера (`0..65535`).
         * @param [in] timeoutMs Таймаут подключения в мс (`-2` = из `_config.connectTimeoutMs`, `< 0` = бесконечно).
         * @return `true` при успешном подключении; `false` при ошибке.
         * @throws std::bad_alloc При нехватке памяти при разрешении адреса и сохранении строки `host`.
         */
        bool Connect(int family, const std::string& host, int port, int timeoutMs = -2);
        /**
         * @brief Повторно подключается к последнему заданному адресу (`host` и `port`).
         * @param [in] timeoutMs Таймаут подключения в мс (`-2` = из `_config.connectTimeoutMs`, `< 0` = бесконечно).
         * @return `true` при успешном переподключении; `false`, если предыдущего адреса не было (`SOCK_EDESTADDRREQ`)
         *         или подключение не удалось.
         * @throws std::bad_alloc При нехватке памяти при разрешении адреса.
         */
        bool Reconnect(int timeoutMs = -2);
        /**
         * @brief Привязывает клиентский сокет к локальному адресу и порту до вызова `Connect()`.
         * @param [in] host Локальный IP-адрес или имя интерфейса (например `"127.0.0.1"`).
         * @param [in] port Локальный порт (`0..65535`; `0` = автовыбор порта ОС).
         * @return `true` при успешной привязке; `false` при ошибке.
         * @throws std::bad_alloc При нехватке памяти при разрешении локального адреса.
         */
        bool BindLocal(const std::string& host, int port);
        /**
         * @brief Закрывает соединение с освобождением системного дескриптора.
         * @param [in] lingerTimeoutMs Таймаут `SO_LINGER` в миллисекундах (`0` = немедленное закрытие).
         * @return `true` при успешном закрытии; `false`, если сокет уже был закрыт или произошла ошибка ОС.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        bool Close(int lingerTimeoutMs = 0) noexcept;
        /**
         * @brief Полностью закрывает сокет и сбрасывает все внутренние поля и состояние объекта.
         * @return `true`, если сокет закрыт (или уже был закрыт); `false` при ошибке ОС.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        bool Dispose() noexcept;


        ///////////////////////////////////////////////////////////////////////
        ///            ОБМЕН ДАННЫМИ, СТРОКАМИ И ПАКЕТАМИ BYTEBUFFER         ///
        ///////////////////////////////////////////////////////////////////////
        /**
         * @brief Отправляет до `size` байт за один системный вызов `Socket::Send()`.
         * @param [in] data  Указатель на буфер данных (не `nullptr`).
         * @param [in] size  Количество байтов для отправки (`> 0`).
         * @param [in] flags Дополнительные флаги `::send()` (по умолчанию `0`).
         * @return Число отправленных байтов (`> 0`), `0` при `SOCK_EWOULDBLOCK` на неблокирующем сокете
         *         или `-1` при ошибке.
         * @note Исключения не выбрасывает.
         */
        int Send(const char* data, int size, int flags = 0);
        /**
         * @brief Гарантированно отправляет ровно `size` байт в цикле (защита от частичной отправки в TCP).
         * @param [in] data  Указатель на буфер данных (не `nullptr`).
         * @param [in] size  Точное число байтов для отправки (`> 0`).
         * @param [in] flags Дополнительные флаги `::send()` (по умолчанию `0`).
         * @return `true`, если отправлены все `size` байтов; `false` при ошибке.
         * @note Исключения не выбрасывает.
         */
        bool SendAll(const char* data, int size, int flags = 0);
        /**
         * @brief Гарантированно отправляет строку `std::string` (без завершающего `'\0'`).
         * @param [in] text Отправляемая строка (пустая строка возвращает `true` без отправки).
         * @return `true` при успехе; `false` при ошибке или размере `> INT_MAX`.
         * @note Исключения не выбрасывает.
         */
        bool SendString(const std::string& text);
        /**
         * @brief Гарантированно отправляет сырое содержимое `ByteBuffer` (без заголовка длины).
         * @param [in] buffer Буфер с данными (пустой буфер возвращает `true` без отправки).
         * @return `true` при успехе; `false` при ошибке или размере `> INT_MAX`.
         * @note Исключения не выбрасывает.
         */
        bool SendBuffer(const ByteBuffer& buffer);
        /**
         * @brief Отправляет `ByteBuffer` как пакет с 4-байтным заголовком длины в Big-Endian.
         *
         * Совместим с `StreamSocket::ReadPacket()` и `TCPServer::SendPacket()`.
         * @param [in] packet Пакет для отправки (допускается пустой буфер — отправится 4-байтный ноль).
         * @return `true` при успехе; `false` при ошибке или размере `> INT_MAX` (`SOCK_EMSGSIZE`).
         * @note Исключения не выбрасывает.
         */
        bool SendPacket(const ByteBuffer& packet);
        /**
         * @brief Принимает до `size` байт из соединения.
         * @param [out] buffer Приёмный буфер (не `nullptr`).
         * @param [in]  size   Максимальное число байтов для чтения (`> 0`).
         * @param [in]  flags  Дополнительные флаги `::recv()` (по умолчанию `0`).
         * @return Число принятых байтов (`> 0`); `0` при штатном закрытии соединения сервером (EOF);
         *         `-1` при ошибке или `SOCK_EWOULDBLOCK` на неблокирующем сокете.
         * @note Исключения не выбрасывает.
         */
        int Recv(char* buffer, int size, int flags = 0);
        /**
         * @brief Гарантированно вычитывает ровно `size` байт в цикле.
         * @param [out] buffer Приёмный буфер размером не менее `size` байтов.
         * @param [in]  size   Точное число байтов для чтения (`> 0`).
         * @param [in]  flags  Дополнительные флаги `::recv()` (по умолчанию `0`).
         * @return `true`, если вычитаны все `size` байтов; `false` при обрыве или ошибке.
         * @note Исключения не выбрасывает.
         */
        bool RecvAll(char* buffer, int size, int flags = 0);
        /**
         * @brief Гарантированно вычитывает ровно `size` байт и возвращает их как `std::string`.
         * @param [in] size Точное число байтов для чтения (`> 0`).
         * @return Прочитанная строка или `""` при ошибке, обрыве или `size <= 0`.
         * @throws std::bad_alloc При нехватке памяти для создания строки.
         */
        std::string RecvString(int size);
        /**
         * @brief Принимает пакет с 4-байтным заголовком длины в Big-Endian и записывает тело в `outPacket`.
         *
         * После успешного приёма автоматически вызывает `outPacket.Rewind()`, чтобы сразу можно было
         * читать поля через `outPacket >> ...` или `outPacket.Read<T>()`.
         * @param [out] outPacket     Целевой `ByteBuffer` (очищается перед чтением; при ошибке также очищается).
         * @param [in]  maxPacketSize Лимит размера пакета в байтах (`0` = взять из `_config.maxPacketSize`).
         * @return `true` при успешном приёме пакета; `false` при обрыве, ошибке или превышении лимита (`SOCK_EMSGSIZE`).
         * @throws std::bad_alloc При нехватке памяти для расширения `outPacket`.
         */
        bool RecvPacket(ByteBuffer& outPacket, uint32_t maxPacketSize = 0);


        ///////////////////////////////////////////////////////////////////////
        ///            СОСТОЯНИЕ, ОПРОС СОБЫТИЙ И ДОСТУП К ОБЁРТКАМ          ///
        ///////////////////////////////////////////////////////////////////////
        /**
         * @brief Проверяет, установлено ли и активно ли в данный момент TCP-соединение.
         * @return `true`, если сокет создан и подключён; `false` иначе.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        bool Connected() const noexcept;
        /**
         * @brief Проверяет через `Socket::PollRead()`, есть ли входящие данные или закрытие соединения.
         * @param [in] timeoutMs Время ожидания в миллисекундах (`0` = проверить мгновенно).
         * @return `true`, если сокет готов к чтению; `false` иначе.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        bool Pending(long timeoutMs = 0) const noexcept;
        /**
         * @brief Ожидает готовности сокета к чтению через `Socket::PollRead()`.
         * @param [in] timeoutMs Таймаут ожидания в миллисекундах (`0` = мгновенно, `< 0` = бесконечно).
         * @return `true`, если сокет готов к чтению; `false` при таймауте или ошибке.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        bool PollRead(long timeoutMs = 0) const noexcept;
        /**
         * @brief Ожидает готовности сокета к записи через `Socket::PollWrite()`.
         * @param [in] timeoutMs Таймаут ожидания в миллисекундах (`0` = мгновенно, `< 0` = бесконечно).
         * @return `true`, если сокет готов к записи; `false` при таймауте или ошибке.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        bool PollWrite(long timeoutMs = 0) const noexcept;
        /**
         * @brief Возвращает число байтов, уже готовых к чтению в буфере ядра ОС.
         * @return Количество байтов (`>= 0`) или `-1`, если сокет закрыт/ошибка.
         * @note Исключения не выбрасывает.
         */
        int Available() const;
        /**
         * @brief Возвращает локальный адрес клиента (и опционально порт).
         * @param [in] includePort Если `true` — добавляет порт (`"IP:port"` / `"[IPv6]:port"`).
         * @return Строка с локальным адресом или `""`, если сокет не создан/не привязан.
         * @throws std::bad_alloc При нехватке памяти для строки.
         */
        std::string LocalAddress(bool includePort = false) const;
        /**
         * @brief Возвращает адрес удалённого сервера (и опционально порт).
         * @param [in] includePort Если `true` — добавляет порт.
         * @return Строка с удалённым адресом или `""`, если клиент не подключён.
         * @throws std::bad_alloc При нехватке памяти для строки.
         */
        std::string RemoteAddress(bool includePort = false) const;
        /**
         * @brief Возвращает локальный порт клиентского сокета (`0..65535` или `-1` при ошибке).
         * @return Номер локального порта или `-1`.
         * @note Исключения не выбрасывает.
         */
        int LocalPort() const;
        /**
         * @brief Возвращает удалённый порт сервера (`0..65535` или `-1`, если не подключён).
         * @return Номер удалённого порта или `-1`.
         * @note Исключения не выбрасывает.
         */
        int RemotePort() const;
        /**
         * @brief Возвращает длительность жизни текущего сокета (`Socket::SockUptime()`).
         * @return Длительность работы сокета (`DateTime::Zero`, если сокет не создан).
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        other::time::DateTime Uptime() const noexcept { return _socket.SockUptime(); }
        /**
         * @brief Создаёт потоковую обёртку `StreamSocket`, забирая владение внутренним `Socket` через `std::move`.
         *
         * После вызова `SockStream()` внутренний `Socket` клиента становится закрытым (`Connected() == false`),
         * а весь дальнейший ввод-вывод выполняется через возвращённый `StreamSocket`.
         * @return Объект `StreamSocket`, владеющий сокетом клиента.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        StreamSocket SockStream() noexcept;
        /**
         * @brief Возвращает изменяемую ссылку на внутренний `Socket`.
         * @return Ссылка на внутренний `Socket`.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        Socket& SockInternal() noexcept { return _socket; }
        /**
         * @brief Возвращает константную ссылку на внутренний `Socket`.
         * @return Константная ссылка на внутренний `Socket`.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        const Socket& SockInternal() const noexcept { return _socket; }
    };
}

#endif // LIBOTHER_NET_TCP_CLIENT_H
