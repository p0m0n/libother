#ifndef LIBOTHER_NET_TCP_SERVER_H
#define LIBOTHER_NET_TCP_SERVER_H


#include "Socket.h"
#include "StreamSocket.h"
#include "ByteBuffer.h"
#include "SocketSelector.h"


namespace other::net::sockets
{
    /** @brief Уникальный монотонный идентификатор клиентского подключения внутри `TCPServer` (`0` = невалидный). */
    using ClientId = uint64_t;

    /** @brief Константа, обозначающая отсутствие / невалидный идентификатор клиента. */
    static constexpr ClientId InvalidClientId = 0;

    /**
     * @struct ServerConfig
     * @brief Параметры конфигурации, лимитов и опций сокетов для `TCPServer`.
     */
    struct ServerConfig
    {
        int    backlog             = SOMAXCONN;                              ///< Максимальная длина очереди `::listen()` (`<= 0` заменяется на `SOMAXCONN`).
        size_t maxClients          = 10000;                                  ///< Лимит одновременных подключений в Event Loop (`0` = без ограничения).
        size_t readChunkSize       = 65536;                                  ///< Размер общего буфера чтения за один вызов `Recv()` (64 КБ).
        size_t maxClientSendQueue  = 4U * 1024U * 1024U;                     ///< Лимит очереди отправки на одного клиента (4 МБ, защита от медленных клиентов; `0` = без лимита).
        size_t maxEventsPerPoll    = SocketSelector::DefaultMaxEventsPerPoll;///< Размер батча событий за один вызов `SocketSelector::Poll()`.
        bool   reuseAddress        = true;                                   ///< Включать `SO_REUSEADDR` для быстрого перезапуска на том же порту.
        bool   exclusiveAddressUse = false;                                  ///< Включать `SO_EXCLUSIVEADDRUSE` (только Windows; игнорируется на Linux).
        bool   dualStack           = true;                                   ///< Для `AF_INET6` включать Dual-Stack (`IPV6_V6ONLY = 0`, приём IPv4 и IPv6).
        bool   noDelay             = true;                                   ///< Включать `TCP_NODELAY` на каждом принятом клиентском сокете.
        bool   keepAlive           = false;                                  ///< Включать `SO_KEEPALIVE` на каждом принятом клиентском сокете.
    };

    /**
     * @struct ClientInfo
     * @brief Копируемый снимок метаданных и статистики одного активного клиента `TCPServer`.
     *
     * Содержит безопасную копию сетевых реквизитов, времени подключения/аптайма (из `Socket::StartTime()`
     * и `Socket::SockUptime()`) и счётчиков трафика; для прямого чтения свойств сокета используйте `GetClientSocket()`.
     */
    struct ClientInfo
    {
        ClientId              id            = InvalidClientId;             ///< Уникальный идентификатор клиента.
        socket_t              handle        = INVALID_SOCKET;              ///< Системный дескриптор сокета клиента на момент снимка.
        std::string           ip;                                          ///< Удалённый IP-адрес клиента.
        int                   port          = 0;                           ///< Удалённый порт клиента.
        other::time::DateTime connectedAt   = other::time::DateTime::Zero; ///< Время создания сокета / принятия подключения (`Socket::StartTime()`).
        other::time::DateTime uptime        = other::time::DateTime::Zero; ///< Продолжительность активной сессии клиента (`Socket::SockUptime()`).
        uint64_t              bytesSent     = 0;                           ///< Общее число байтов, отправленных клиенту.
        uint64_t              bytesReceived = 0;                           ///< Общее число байтов, принятых от клиента.
        size_t                queuedToSend  = 0;                           ///< Текущий остаток байтов в очереди отправки клиента.
        bool                  pendingClose  = false;                       ///< `true`, если клиент помечен на закрытие после доотправки очереди.
    };

    /**
     * @struct ServerStats
     * @brief Сводная телеметрия и статистика работы `TCPServer`.
     */
    struct ServerStats
    {
        size_t                activeClients      = 0;                            ///< Текущее число активных клиентов в Event Loop.
        uint64_t              totalAccepted      = 0;                            ///< Суммарное число принятых подключений с момента запуска.
        uint64_t              totalDisconnected  = 0;                            ///< Суммарное число завершённых подключений.
        uint64_t              totalRejected      = 0;                            ///< Число отклонённых подключений (например, при достижении `maxClients`).
        uint64_t              totalBytesSent     = 0;                            ///< Суммарно отправлено байтов всем клиентам (включая отключённых).
        uint64_t              totalBytesReceived = 0;                            ///< Суммарно принято байтов от всех клиентов (включая отключённых).
        other::time::DateTime startedAt          = other::time::DateTime::Zero;  ///< Время последнего успешного запуска сервера.
    };
	
	/**
     * @struct ClientEntry
     * @brief Внутреннее состояние одного подключённого клиента в событийном режиме.
     */
    struct ClientEntry
    {
        ClientId             id            = InvalidClientId; ///< Уникальный идентификатор клиента.
        Socket               socket;                          ///< Неблокирующий TCP-сокет клиента (хранит `StartTime()` и счётчики байтов).
        std::string          ip;                              ///< Кэшированный IP-адрес клиента.
        int                  port          = 0;               ///< Кэшированный порт клиента.
        std::vector<uint8_t> sendQueue;                       ///< Буфер отложенной неблокирующей отправки.
        size_t               sendOffset    = 0;               ///< Текущее смещение чтения в `sendQueue`.
        bool                 watchingWrite = false;           ///< Зарегистрирован ли флаг `watchWrite` в `_selector`.
        bool                 pendingClose  = false;           ///< Закрыть соединение сразу после опустошения `sendQueue`.

        /**
         * @brief Возвращает количество непрочитанных/неотправленных байтов в `sendQueue`.
         * @return Число байтов, ожидающих отправки.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        size_t QueuedBytes() const noexcept;
    };


    /**
     * @class TCPServer
     * @brief Кроссплатформенный TCP-сервер с поддержкой ручного `Accept` и событийного цикла (`SocketSelector`).
     *
     * Инкапсулирует слушающий `Socket`, мультиплексор `SocketSelector` и таблицу активных клиентских
     * соединений с неблокирующими очередями отправки, пакетным фреймингом `ByteBuffer`, широковещательной
     * рассылкой (`Broadcast` / `BroadcastPacket`) и сбором статистики.
     *
     * Политика владения и потокобезопасности:
     * - Эксклюзивное владение сокетами и состоянием (копирование запрещено, поддерживается перемещение).
     * - Все сетевые операции (`PollOnce`, `Send`, `Broadcast`, `DisconnectClient`, `Stop`) должны вызываться
     *   из потока Event Loop; потокобезопасно вызывать извне допускается только `RequestStop()`.
     *
     * Политика обработки ошибок:
     * Сетевые ошибки не выбрасывают исключений — методы возвращают `false` / `-1` / пустой `Socket`,
     * а точный код ошибки ОС сохраняется в `GET_NET_ERROR()` (и при наличии колбэка `OnError` передаётся туда).
     * Исключение `std::bad_alloc` возможно только при нехватке памяти для внутренних буферов/контейнеров.
     */
    class TCPServer
    {
    public:
        /** @brief Колбэк при подключении нового клиента в Event Loop: `(id, clientIp, clientPort)`. */
        using OnConnectedCallback    = std::function<void(ClientId, const std::string&, int)>;
        /** @brief Колбэк при получении порции данных от клиента: `(id, data, size)`. */
        using OnDataCallback         = std::function<void(ClientId, const uint8_t*, size_t)>;
        /** @brief Колбэк при отключении клиента: `(id, errorCode)` (`0` = штатное закрытие/EOF). */
        using OnDisconnectedCallback = std::function<void(ClientId, int)>;
        /** @brief Колбэк при системной ошибке сервера: `(errorCode, contextMessage)`. */
        using OnErrorCallback        = std::function<void(int, const std::string&)>;

    private:
        Socket                                    _socket;             ///< Базовый слушающий TCP-сокет сервера.
        SocketSelector                            _selector;           ///< Мультиплексор событий (`epoll` на Linux / `WSAPoll` на Windows).
        ServerConfig                              _config;             ///< Активная конфигурация сервера.
        bool                                      _isListening;        ///< Флаг активности прослушивания (`Bind` + `Listen` выполнены).
        bool                                      _listenerRegistered; ///< Зарегистрирован ли `_socket` в `_selector` для Event Loop.
        std::atomic<bool>                         _stopRequested;      ///< Потокобезопасный флаг запроса остановки цикла `Run()`.

        ClientId                                  _nextClientId;       ///< Монотонный генератор уникальных `ClientId`.
        std::unordered_map<ClientId, ClientEntry> _clients;            ///< Хранилище активных клиентов (адреса `ClientEntry::socket` стабильны при вставке/удалении).
        std::unordered_map<socket_t, ClientId>    _handleToClient;     ///< Карта `socket_t -> ClientId` для диспетчеризации событий за $O(1)$.
        std::vector<uint8_t>                      _readBuffer;         ///< Общий переиспользуемый буфер чтения (без аллокаций на каждый пакет).

        ServerStats                               _stats;              ///< Накопительная статистика сервера.

        OnConnectedCallback                       _onConnected;        ///< Пользовательский обработчик нового подключения.
        OnDataCallback                            _onData;             ///< Пользовательский обработчик входящих данных.
        OnDisconnectedCallback                    _onDisconnected;     ///< Пользовательский обработчик отключения клиента.
        OnErrorCallback                           _onError;            ///< Пользовательский обработчик ошибок сервера.

    protected:
        /**
         * @brief Внутренний помощник: регистрирует слушающий `_socket` в `_selector` в неблокирующем режиме.
         * @return `true` при успехе; `false` при ошибке перевода в неблокирующий режим или регистрации в `_selector`.
         * @note Исключения не выбрасывает (`noexcept`).
         */
        bool EnsureListenerInSelector() noexcept;
        /**
         * @brief Внутренний помощник: принимает все накопившиеся входящие подключения в цикле до `SOCK_EWOULDBLOCK`.
         * @throws std::bad_alloc При нехватке памяти для создания записи нового клиента.
         */
        void HandleAccept();
        /**
         * @brief Внутренний помощник: вычитывает доступные данные от клиента в `_readBuffer` и вызывает `_onData`.
         * @param [in] id Идентификатор клиента.
         * @throws std::bad_alloc При нехватке памяти для первичного выделения `_readBuffer`.
         */
        void HandleClientRead(ClientId id);
        /**
         * @brief Внутренний помощник: досылает отложенные байты из `sendQueue` клиента при готовности сокета к записи.
         * @param [in] id Идентификатор клиента.
         * @note Исключения не выбрасывает (`noexcept`, если пользовательский `_onDisconnected` не бросает исключений).
         */
        void HandleClientWrite(ClientId id) noexcept;
        /**
         * @brief Внутренний помощник: удаляет клиента из `_selector` и карт, закрывает сокет и вызывает `_onDisconnected`.
         * @param [in] id        Идентификатор закрываемого клиента.
         * @param [in] errorCode Код причины отключения (`0` = штатное закрытие/EOF; иначе системный код ошибки).
         * @note Пробрасывает исключение только если его выбросит пользовательский колбэк `_onDisconnected`.
         */
        void CloseClientInternal(ClientId id, int errorCode);
        /**
         * @brief Внутренний помощник: уведомляет `_onError` (если задан) с кодом ошибки и контекстом.
         * @param [in] errorCode Код системной ошибки (`SOCK_*`).
         * @param [in] context   Краткое описание операции, в которой произошла ошибка.
         */
        void NotifyError(int errorCode, const std::string& context) const;

    public:
        /**
         * @brief Создаёт пустой (незапущенный) экземпляр `TCPServer` с указанной конфигурацией.
         * @param [in] config Параметры сервера (`ServerConfig`, по умолчанию значения по умолчанию).
         * @throws std::bad_alloc При нехватке памяти для инициализации рабочего буфера `SocketSelector`.
         */
        explicit TCPServer(const ServerConfig& config = ServerConfig{});
        /**
         * @brief Создаёт и сразу запускает `TCPServer` на всех интерфейсах (`"0.0.0.0"` для IPv4 или `"::"` для IPv6).
         * @param [in] family Семейство адресов (`AF_INET` для IPv4 или `AF_INET6` для IPv6/Dual-Stack).
         * @param [in] port   Сетевой порт для прослушивания (`0..65535`; `0` = автовыбор свободного порта ОС).
         * @param [in] config Параметры сервера (`ServerConfig`).
         * @note При сбое запуска `IsListening()` вернёт `false`, а код ошибки доступен через `GET_NET_ERROR()`.
         * @throws std::bad_alloc При нехватке памяти для внутренних буферов.
         */
        TCPServer(int family, int port, const ServerConfig& config = ServerConfig{});
        /**
         * @brief Создаёт и сразу запускает `TCPServer` на конкретном сетевом интерфейсе (хосте) и порту.
         * @param [in] family Семейство адресов (`AF_INET` или `AF_INET6`).
         * @param [in] host   IP-адрес или имя интерфейса (например `"127.0.0.1"`, `"0.0.0.0"`, `"::1"`).
         * @param [in] port   Сетевой порт для привязки (`0..65535`).
         * @param [in] config Параметры сервера (`ServerConfig`).
         * @note При сбое запуска `IsListening()` вернёт `false`, а код ошибки доступен через `GET_NET_ERROR()`.
         * @throws std::bad_alloc При нехватке памяти для внутренних буферов.
         */
        TCPServer(int family, const std::string& host, int port, const ServerConfig& config = ServerConfig{});
        /**
         * @brief Деструктор: останавливает сервер, закрывает все клиентские соединения и слушающий сокет (RAII).
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        ~TCPServer() noexcept;

        // Копирование запрещено: сервер эксклюзивно владеет слушающим и клиентскими сокетами.
        TCPServer(const TCPServer&) = delete;
        TCPServer& operator=(const TCPServer&) = delete;
        /**
         * @brief Конструктор перемещения: переносит слушающий сокет, активных клиентов, селектор и статистику.
         * @param [in,out] other Исходный `TCPServer` (после перемещения становится остановленным и пустым).
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        TCPServer(TCPServer&& other) noexcept;
        /**
         * @brief Оператор перемещающего присваивания: останавливает текущий сервер и забирает ресурсы `other`.
         * @param [in,out] other Исходный `TCPServer` (после перемещения обнуляется).
         * @return Ссылка на текущий объект (`*this`).
         * @note Гарантированно не выбрасывает исключений (`noexcept`). Безопасен при самоприсваивании.
         */
        TCPServer& operator=(TCPServer&& other) noexcept;


        ///////////////////////////////////////////////////////////////////////
        ///            КОНФИГУРАЦИЯ, РЕЗЕРВИРОВАНИЕ И КОЛБЭКИ               ///
        ///////////////////////////////////////////////////////////////////////
        /**
         * @brief Возвращает текущую конфигурацию сервера.
         * @return Константная ссылка на `ServerConfig`.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        const ServerConfig& Config() const noexcept;
        /**
         * @brief Обновляет конфигурацию сервера и размер батча `maxEventsPerPoll` в `SocketSelector`.
         * @param [in] config Новая структура параметров `ServerConfig`.
         * @throws std::bad_alloc При нехватке памяти для буферов событий `SocketSelector`.
         */
        void SetConfig(const ServerConfig& config);
        /**
         * @brief Заранее резервирует память в `SocketSelector` и картах клиентов под `capacity` одновременных соединений.
         * @param [in] capacity Ожидаемое максимальное количество одновременных клиентов.
         * @throws std::bad_alloc При нехватке оперативной памяти.
         */
        void Reserve(size_t capacity);
        /**
         * @brief Устанавливает обработчик подключения нового клиента (вызывается в `PollOnce()` / `Run()`).
         * @param [in] callback Функция `(ClientId id, const std::string& ip, int port)`.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        void SetOnClientConnected(OnConnectedCallback callback) noexcept;
        /**
         * @brief Устанавливает обработчик входящих данных от клиента (вызывается в `PollOnce()` / `Run()`).
         * @param [in] callback Функция `(ClientId id, const uint8_t* data, size_t size)`.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        void SetOnClientData(OnDataCallback callback) noexcept;
        /**
         * @brief Устанавливает обработчик отключения клиента (вызывается при разрыве, ошибке или `DisconnectClient()`).
         * @param [in] callback Функция `(ClientId id, int errorCode)`.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        void SetOnClientDisconnected(OnDisconnectedCallback callback) noexcept;
        /**
         * @brief Устанавливает обработчик системных ошибок сервера.
         * @param [in] callback Функция `(int errorCode, const std::string& context)`.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        void SetOnError(OnErrorCallback callback) noexcept;


        ///////////////////////////////////////////////////////////////////////
        ///            ЖИЗНЕННЫЙ ЦИКЛ СЕРВЕРА (START / STOP / RUN)          ///
        ///////////////////////////////////////////////////////////////////////
        /**
         * @brief Переводит ранее созданный и привязанный внутренний сокет в режим прослушивания с очередью `_config.backlog`.
         * @return `true` при успехе; `false`, если сокет не создан, уже слушает или `Listen()` завершился ошибкой.
         * @note Исключения не выбрасывает (`noexcept`).
         */
        bool Start() noexcept;
        /**
         * @brief Переводит ранее созданный и привязанный внутренний сокет в режим прослушивания с заданным `backlog`.
         * @param [in] backlog Максимальная длина очереди ожидающих подключений (`> 0`).
         * @return `true` при успехе; `false` при ошибке или невалидном сокете.
         * @note Исключения не выбрасывает (`noexcept`).
         */
        bool Start(int backlog) noexcept;
        /**
         * @brief Универсальный запуск сервера: создаёт сокет, применяет опции из `ServerConfig`, выполняет `Bind` и `Listen`.
         *
         * Если сервер уже запущен, предварительно корректно останавливает его (`Stop()`).
         * @param [in] port    Порт прослушивания (`0..65535`; `0` = автоматический выбор свободного порта ОС).
         * @param [in] address Локальный IP-адрес интерфейса (`"0.0.0.0"` / `"::"` для всех интерфейсов, `"127.0.0.1"` для loopback).
         * @param [in] backlog Длина очереди `::listen()` (`<= 0` использует `_config.backlog`).
         * @param [in] family  Семейство адресов (`AF_INET`, `AF_INET6` или `-1` для автоопределения по наличию `':'` в `address`).
         * @return `true` при успешном запуске; `false` при ошибке создания, привязки или `Listen()` (код в `GET_NET_ERROR()`).
         * @throws std::bad_alloc При нехватке памяти внутри `Socket::Bind()`.
         */
        bool Start(int port, const std::string& address, int backlog = -1, int family = -1);
        /**
         * @brief Полностью останавливает сервер: отключает всех клиентов, очищает `SocketSelector` и закрывает слушающий сокет.
         * @return `true`, если слушающий сокет был успешно закрыт (или уже закрыт); `false` при системной ошибке закрытия.
         * @note Пробрасывает исключение только если его выбросит пользовательский колбэк `_onDisconnected`.
         */
        bool Stop();
        /**
         * @brief Потокобезопасно запрашивает мягкую остановку блокирующего цикла `Run()`.
         * @note Гарантированно не выбрасывает исключений (`noexcept`). Безопасно вызывать из другого потока.
         */
        void RequestStop() noexcept;
        /**
         * @brief Выполняет одну итерацию событийного цикла сервера (Event Loop) через `SocketSelector::Poll()`.
         *
         * При первом вызове автоматически переводит слушающий сокет в неблокирующий режим и регистрирует его в `_selector`.
         * Принимает новых клиентов, вычитывает данные, досылает очереди отправки и вызывает пользовательские колбэки.
         * @param [in] timeoutMs Максимальное время ожидания событий в миллисекундах (`0` = не блокировать, `< 0` = ждать бесконечно).
         * @return Количество обработанных событий (`>= 0`); `-1`, если сервер не запущен (`SOCK_EBADF`) или `Poll()` вернул ошибку.
         * @throws std::bad_alloc При нехватке памяти для новых клиентских структур или буферов.
         */
        int PollOnce(int timeoutMs = 10);
        /**
         * @brief Запускает непрерывный цикл обработки событий (`PollOnce`) до вызова `RequestStop()` или `Stop()`.
         * @param [in] pollTimeoutMs Таймаут каждой итерации `PollOnce()` в миллисекундах.
         * @return `true` при штатном завершении цикла; `false`, если сервер не был запущен до вызова `Run()`.
         * @throws std::bad_alloc При нехватке памяти во время обработки событий.
         */
        bool Run(int pollTimeoutMs = 50);


        ///////////////////////////////////////////////////////////////////////
        ///            РУЧНОЙ ПРИЁМ ПОДКЛЮЧЕНИЙ (РЕЖИМ СЛУШАТЕЛЯ)           ///
        ///////////////////////////////////////////////////////////////////////
        /**
         * @brief Проверяет через `Socket::PollRead()` (без лимита `FD_SETSIZE`), есть ли ожидающие клиенты в очереди `Accept`.
         * @param [in] timeoutMs Время ожидания в миллисекундах (`0` = мгновенная проверка).
         * @return `true`, если в очереди есть входящее подключение; `false` иначе.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        bool Pending(int timeoutMs = 0) const noexcept;
        /**
         * @brief Принимает входящее подключение и возвращает низкоуровневый объект `Socket`.
         * @return Валидный клиентский `Socket` при успехе, либо пустой закрытый `Socket` при ошибке/`SOCK_EWOULDBLOCK`.
         * @note Исключения не выбрасывает (`noexcept`). Применяет настройки `noDelay`/`keepAlive` из `ServerConfig`.
         */
        Socket AcceptSocket() noexcept;
        /**
         * @brief Принимает входящее подключение с одновременным извлечением IP-адреса и порта клиента.
         * @param [out] outClientIp   Сюда записывается IP-адрес подключившегося клиента (или `""` при ошибке).
         * @param [out] outClientPort Сюда записывается порт клиента (или `0` при ошибке).
         * @return Валидный клиентский `Socket` при успехе, либо пустой закрытый `Socket` при ошибке.
         * @throws std::bad_alloc При нехватке памяти для записи строки `outClientIp`.
         */
        Socket Accept(std::string& outClientIp, int& outClientPort);
        /**
         * @brief Принимает входящее подключение и сразу оборачивает его в потоковый `StreamSocket`.
         * @return Объект `StreamSocket` (проверяйте `stream.SockInternal().IsCreated()` на успех).
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        StreamSocket AcceptStream() noexcept;


        ///////////////////////////////////////////////////////////////////////
        ///            РАБОТА С КЛИЕНТАМИ (ОТПРАВКА, РАССЫЛКА, ОТКЛЮЧЕНИЕ)  ///
        ///////////////////////////////////////////////////////////////////////
        /**
         * @brief Неблокирующе отправляет сырые байты указанному клиенту (остаток ставится в очередь отправки клиента).
         * @param [in] id   Идентификатор клиента (`ClientId`).
         * @param [in] data Указатель на данные (не `nullptr`).
         * @param [in] size Количество байтов для отправки (`> 0`).
         * @return `true`, если данные отправлены или помещены в очередь отправки;
         *         `false`, если клиент не найден (`SOCK_ENOENT`), неверные аргументы (`SOCK_EINVAL`),
         *         переполнена очередь клиента (`SOCK_ENOBUFS`) или произошла критическая ошибка отправки.
         * @throws std::bad_alloc При нехватке памяти для расширения очереди отправки клиента.
         */
        bool Send(ClientId id, const char* data, size_t size);
        /**
         * @brief Неблокирующе отправляет строку `std::string` указанному клиенту (без завершающего `'\0'`).
         * @param [in] id   Идентификатор клиента.
         * @param [in] data Отправляемая строка (пустая строка считается успешной пустой операцией).
         * @return `true` при успехе; `false` при ошибке.
         * @throws std::bad_alloc При нехватке памяти для очереди отправки.
         */
        bool Send(ClientId id, const std::string& data);
        /**
         * @brief Неблокирующе отправляет сырое содержимое `ByteBuffer` указанному клиенту (без заголовка длины).
         * @param [in] id     Идентификатор клиента.
         * @param [in] buffer Буфер с данными.
         * @return `true` при успехе; `false` при ошибке.
         * @throws std::bad_alloc При нехватке памяти для очереди отправки.
         */
        bool Send(ClientId id, const ByteBuffer& buffer);
        /**
         * @brief Отправляет `ByteBuffer` с 4-байтным заголовком длины в Big-Endian (совместим с `StreamSocket::ReadPacket()`).
         * @param [in] id     Идентификатор клиента.
         * @param [in] packet Пакет для отправки (допускается пустой буфер — отправится 4-байтный ноль).
         * @return `true` при успехе; `false` при ошибке или превышении `INT_MAX` (`SOCK_EMSGSIZE`).
         * @throws std::bad_alloc При нехватке памяти для очереди отправки.
         */
        bool SendPacket(ClientId id, const ByteBuffer& packet);
        /**
         * @brief Рассылает сырые байты всем активным клиентам (с опциональным исключением одного клиента).
         * @param [in] data            Указатель на данные.
         * @param [in] size            Количество байтов.
         * @param [in] excludeClientId Идентификатор клиента, которого нужно пропустить (по умолчанию `InvalidClientId` — слать всем).
         * @return Количество клиентов, которым данные были успешно отправлены или поставлены в очередь.
         * @throws std::bad_alloc При нехватке памяти для очередей отправки.
         */
        size_t Broadcast(const char* data, size_t size, ClientId excludeClientId = InvalidClientId);
        /**
         * @brief Рассылает строку `std::string` всем активным клиентам.
         * @param [in] data            Отправляемая строка.
         * @param [in] excludeClientId Идентификатор исключаемого клиента (по умолчанию `InvalidClientId`).
         * @return Количество клиентов, получивших сообщение.
         * @throws std::bad_alloc При нехватке памяти для очередей отправки.
         */
        size_t Broadcast(const std::string& data, ClientId excludeClientId = InvalidClientId);
        /**
         * @brief Рассылает пакет `ByteBuffer` (с 4-байтным заголовком длины) всем активным клиентам.
         * @param [in] packet          Пакет для рассылки.
         * @param [in] excludeClientId Идентификатор исключаемого клиента (по умолчанию `InvalidClientId`).
         * @return Количество клиентов, которым пакет успешно отправлен или поставлен в очередь.
         * @throws std::bad_alloc При нехватке памяти для очередей отправки.
         */
        size_t BroadcastPacket(const ByteBuffer& packet, ClientId excludeClientId = InvalidClientId);
        /**
         * @brief Отключает указанного клиента от сервера.
         * @param [in] id       Идентификатор клиента.
         * @param [in] graceful Если `true` и в очереди отправки клиента есть невысланные данные — помечает клиента
         *                      на закрытие после доотправки очереди; если `false` — закрывает немедленно.
         * @return `true`, если клиент найден и отключён (или поставлен на закрытие); `false`, если клиент не найден.
         * @note Пробрасывает исключение только если его выбросит пользовательский колбэк `_onDisconnected`.
         */
        bool DisconnectClient(ClientId id, bool graceful = true);
        /**
         * @brief Немедленно отключает всех текущих клиентов в Event Loop, оставляя сервер в режиме прослушивания.
         * @note Пробрасывает исключение только если его выбросит пользовательский колбэк `_onDisconnected`.
         */
        void DisconnectAll();


        ///////////////////////////////////////////////////////////////////////
        ///            СОСТОЯНИЕ, ИНФОРМАЦИЯ О КЛИЕНТАХ И СТАТИСТИКА        ///
        ///////////////////////////////////////////////////////////////////////
        /**
         * @brief Проверяет, запущен ли сервер в режиме прослушивания.
         * @return `true`, если сервер активен и слушает порт; `false` иначе.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        bool IsListening() const noexcept;
        /**
         * @brief Проверяет состояние прослушивания на уровне ядра ОС через опцию `SO_ACCEPTCONN`.
         * @return `true`, если сокет открыт и ядро подтверждает режим прослушивания; `false` иначе.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        bool Active() const noexcept;
        /**
         * @brief Возвращает локальный IP-адрес (и опционально порт), на котором запущен сервер.
         * @param [in] includePort Если `true` — добавляет порт (`"IP:port"` / `"[IPv6]:port"`).
         * @return Строка с локальным адресом или `""`, если сервер не запущен.
         * @throws std::bad_alloc При нехватке памяти для `std::string`.
         */
        std::string LocalAddress(bool includePort = false) const;
        /**
         * @brief Возвращает локальный порт, на котором запущен сервер.
         * @return Номер порта (`0..65535`) или `-1`, если сервер не запущен.
         * @note Исключения не выбрасывает.
         */
        int LocalPort() const;
        /**
         * @brief Проверяет за $O(1)$, подключён ли клиент с указанным `ClientId`.
         * @param [in] id Идентификатор клиента.
         * @return `true`, если клиент активен; `false` иначе.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        bool HasClient(ClientId id) const noexcept;
        /**
         * @brief Возвращает константный указатель на внутренний `Socket` активного клиента.
         * @param [in] id Идентификатор клиента.
         * @return Указатель на `Socket`, если клиент найден; `nullptr`, если клиент не подключён.
         * @note Указатель действителен только пока клиент не отключён (`DisconnectClient` / `PollOnce` / `Stop`).
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        const Socket* GetClientSocket(ClientId id) const noexcept;
        /**
         * @brief Заполняет структуру `ClientInfo` актуальными метаданными, аптаймом и статистикой клиента.
         * @param [in]  id      Идентификатор клиента.
         * @param [out] outInfo Целевая структура `ClientInfo`.
         * @return `true`, если клиент найден; `false` иначе.
         * @throws std::bad_alloc При копировании строки `ip`.
         */
        bool GetClientInfo(ClientId id, ClientInfo& outInfo) const;
        /**
         * @brief Возвращает список идентификаторов всех подключённых в данный момент клиентов.
         * @return Вектор активных `ClientId`.
         * @throws std::bad_alloc При нехватке памяти для вектора.
         */
        std::vector<ClientId> GetClientIds() const;
        /**
         * @brief Возвращает текущее количество подключённых клиентов в Event Loop.
         * @return Количество активных клиентов.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        size_t ClientCount() const noexcept;
        /**
         * @brief Возвращает сводную статистику сервера (`ServerStats`).
         * @return Копия актуальной структуры `ServerStats`.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        ServerStats Stats() const noexcept;
        /**
         * @brief Возвращает время последнего запуска сервера.
         * @return Метка `DateTime` запуска (или `DateTime::Zero`, если сервер ещё не запускался).
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        other::time::DateTime StartTime() const noexcept;
        /**
         * @brief Возвращает длительность работы сервера с момента последнего запуска.
         * @return Длительность работы (`DateTime`) или `DateTime::Zero`, если сервер не запущен.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        other::time::DateTime Uptime() const noexcept;
        /**
         * @brief Устанавливает опцию `SO_EXCLUSIVEADDRUSE` на слушающем сокете (только Windows).
         * @param [in] exclusive `true` для эксклюзивного захвата порта.
         * @return `true` при успехе на Windows; `false` на Linux или при закрытом сокете.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        bool SetExclusiveAddressUse(bool exclusive) noexcept;
        /**
         * @brief Проверяет состояние опции `SO_EXCLUSIVEADDRUSE` на слушающем сокете.
         * @return `true`, если опция включена на Windows; `false` иначе.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        bool IsExclusiveAddressUse() const noexcept;
        /**
         * @brief Возвращает изменяемую ссылку на внутренний слушающий `Socket`.
         * @return Ссылка на внутренний `Socket`.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        Socket& SockInternal() noexcept;
        /**
         * @brief Возвращает константную ссылку на внутренний слушающий `Socket`.
         * @return Константная ссылка на внутренний `Socket`.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        const Socket& SockInternal() const noexcept;
		/**
         * @brief Возвращает изменяемую ссылку на внутренний `SocketSelector`.
         * @return Ссылка на внутренний `SocketSelector`.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        SocketSelector& SockSelector() noexcept;
        /**
         * @brief Возвращает константную ссылку на внутренний `SocketSelector`.
         * @return Константная ссылка на `SocketSelector`.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        const SocketSelector& SockSelector() const noexcept;
    };
}

#endif // LIBOTHER_NET_TCP_SERVER_H
