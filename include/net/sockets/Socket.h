#ifndef LIBOTHER_NET_SOCK_H
#define LIBOTHER_NET_SOCK_H


#include "SocketDefines.h"
#include "DateTime.h"


namespace other::net::sockets
{
    /**
     * @class Socket
     * @brief RAII-обёртка над системными сетевыми сокетами (Windows Winsock2 и Linux POSIX).
     *
     * Инкапсулирует системный дескриптор сокета (`socket_t`) с эксклюзивным владением (только перемещение,
     * копирование запрещено) и гарантирует его автоматическое закрытие в деструкторе.
     *
     * Основные возможности:
     * - работа с TCP (`Bind`, `Listen`, `Accept`, `Connect`, `Send`/`SendAll`, `Recv`/`RecvAll`) и UDP (`SendTo`, `RecvFrom`);
     * - поддержка IPv4, IPv6 и режима Dual-Stack (`SetDualMode`);
     * - блокирующий и неблокирующий режимы, подключение с таймаутом (`Connect(host, port, timeoutMs)`), опрос событий (`PollRead`/`PollWrite`/`PollError`, `Select`);
     * - управление системными опциями (`SO_REUSEADDR`, `TCP_NODELAY`, `SO_KEEPALIVE`, `SO_LINGER`, TTL, DF, Broadcast, Multicast);
     * - учёт времени жизни сокета (`StartTime`, `CloseTime`, `SockUptime`) и статистики трафика (`BytesSent`, `BytesRecv`).
     *
     * Политика обработки ошибок:
     * Сетевые ошибки не выбрасывают исключений — о сбое сигнализирует возвращаемое значение (`false`, `-1`, пустой `Socket` или строка),
     * а точный код ошибки ОС доступен через `GET_NET_ERROR()` или `SockError()`.
     */
    class Socket
    {
    private:
        socket_t              _handle;       ///< Системный дескриптор сокета (`INVALID_SOCKET`, если не создан или закрыт).
        int                   _family;       ///< Семейство адресов: `AF_INET` (IPv4) или `AF_INET6` (IPv6).
        int                   _type;         ///< Тип сокета: `SOCK_STREAM` (TCP) или `SOCK_DGRAM` (UDP).
        int                   _protocol;     ///< Протокол: `IPPROTO_TCP` или `IPPROTO_UDP`.
        bool                  _blocking;     ///< Кэш режима блокировки (`true` = блокирующий, `false` = неблокирующий).
        other::time::DateTime _start_t;      ///< Время создания сокета.
        other::time::DateTime _close_t;      ///< Время закрытия сокета (`DateTime::Zero`, пока сокет открыт).
        uint64_t              _bytesSent;    ///< Накопительный счётчик успешно отправленных байтов.
        uint64_t              _bytesRecv;    ///< Накопительный счётчик успешно принятых байтов.

    protected:
        /**
         * @brief Внутренний помощник: восстанавливает `_type`, `_family`, `_protocol` и `_blocking` у существующего дескриптора ОС.
         * @param [in] handle Валидный системный дескриптор сокета.
         * @return `true` при успешной инициализации; `false`, если `handle == INVALID_SOCKET` (выставляет `SOCK_EBADF`).
         * @note Исключения не выбрасывает.
         */
        bool InitFromHandle(socket_t handle) noexcept;

    public:
        /**
         * @brief Создаёт пустой (неоткрытый) объект `Socket` с дескриптором `INVALID_SOCKET`.
         * @note Системных вызовов не делает, гарантированно не выбрасывает исключений (`noexcept`).
         */
        Socket() noexcept;
        /**
         * @brief Оборачивает уже созданный системный дескриптор (например, возвращённый из `::accept()`).
         * @param [in] handle Готовый системный дескриптор сокета (переходит под управление RAII).
         * @note При передаче `INVALID_SOCKET` объект остаётся закрытым, устанавливается `SOCK_EBADF`. Исключения не выбрасывает.
         */
        explicit Socket(socket_t handle) noexcept;
        /**
         * @brief Создаёт IPv6 Dual-Stack сокет, способный работать одновременно с IPv4 и IPv6.
         * @param [in] type     Тип сокета (`SOCK_STREAM` для TCP или `SOCK_DGRAM` для UDP).
         * @param [in] protocol Протокол (`IPPROTO_TCP`, `IPPROTO_UDP` или `0` для выбора по умолчанию).
         * @note При сбое создания `IsCreated()` вернёт `false`, а код ошибки доступен через `GET_NET_ERROR()`. Исключения не выбрасывает.
         */
        Socket(int type, int protocol) noexcept;
        /**
         * @brief Создаёт новый физический сокет в операционной системе с заданными параметрами.
         * @param [in] family   Семейство адресов (`AF_INET` для IPv4 или `AF_INET6` для IPv6).
         * @param [in] type     Тип сокета (`SOCK_STREAM` для TCP или `SOCK_DGRAM` для UDP).
         * @param [in] protocol Протокол (`IPPROTO_TCP`, `IPPROTO_UDP` или `0`).
         * @note При ошибке ОС дескриптор остаётся `INVALID_SOCKET`, код ошибки сохраняется в `GET_NET_ERROR()`. Исключения не выбрасывает.
         */
        Socket(int family, int type, int protocol) noexcept;
        /**
         * @brief Конструктор перемещения: переносит владение дескриптором, метаданными и статистикой из `other`.
         * @param [in,out] other Исходный объект `Socket` (после перемещения становится закрытым и обнулённым).
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        Socket(Socket&& other) noexcept;
        /**
         * @brief Оператор перемещающего присваивания: закрывает текущий сокет (если открыт) и забирает дескриптор у `other`.
         * @param [in,out] other Исходный объект `Socket` (после перемещения обнуляется).
         * @return Ссылка на текущий объект (`*this`).
         * @note Гарантированно не выбрасывает исключений (`noexcept`). Безопасен при самоприсваивании.
         */
        Socket& operator=(Socket&& other) noexcept;

        // Копирование запрещено: одним системным дескриптором может владеть только один объект Socket (RAII).
        Socket(const Socket&) = delete;
        Socket& operator=(const Socket&) = delete;
        /**
         * @brief Деструктор: автоматически закрывает открытый сокет и освобождает дескриптор ОС.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        ~Socket() noexcept;


        ///////////////////////////////////////////////////////////////////////
        ///            ПРОВЕРКА ВОЗМОЖНОСТЕЙ ОС                             ///
        ///////////////////////////////////////////////////////////////////////
        /**
         * @brief Проверяет, поддерживает ли текущая ОС создание IPv4-сокетов.
         * @return `true`, если стек IPv4 доступен; `false` при ошибке (сохраняет код ошибки ОС).
         * @note Исключения не выбрасывает (`noexcept`).
         */
        static bool SupportsIPv4() noexcept;
        /**
         * @brief Проверяет, поддерживает ли текущая ОС создание IPv6-сокетов.
         * @return `true`, если стек IPv6 доступен; `false` при ошибке (сохраняет код ошибки ОС).
         * @note Исключения не выбрасывает (`noexcept`).
         */
        static bool SupportsIPv6() noexcept;


        ///////////////////////////////////////////////////////////////////////
        ///            ИНФОРМАЦИЯ О СОКЕТЕ, ОШИБКИ И СТАТИСТИКА             ///
        ///////////////////////////////////////////////////////////////////////
        /**
         * @brief Возвращает число байтов, уже пришедших в буфер ядра ОС и готовых к немедленному чтению (`FIONREAD`).
         * @return Количество доступных байтов (`>= 0`, ограничено `INT_MAX`), либо `-1`, если сокет не создан или произошла ошибка ОС.
         * @note Исключения не выбрасывает.
         */
        int Available() const;
        /**
         * @brief Запрашивает и сбрасывает отложенный код ошибки сокета из ядра ОС (опция `SO_ERROR`).
         * @return `0`, если ошибок на сокете нет; положительный системный код ошибки; `-1`, если сокет закрыт или `getsockopt()` не удался.
         * @note Исключения не выбрасывает (`noexcept`).
         */
        int SockError() const noexcept;
        /**
         * @brief Возвращает общее количество байтов, успешно отправленных через этот сокет с момента создания/сброса.
         * @return Накопленное число отправленных байтов.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        uint64_t BytesSent() const noexcept;
        /**
         * @brief Возвращает общее количество байтов, успешно принятых через этот сокет с момента создания/сброса.
         * @return Накопленное число принятых байтов.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        uint64_t BytesRecv() const noexcept;
        /**
         * @brief Обнуляет счётчики отправленных (`BytesSent`) и принятых (`BytesRecv`) байтов.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        void ResetStats() noexcept;
        /**
         * @brief Возвращает локальный IP-адрес сокета в текстовом виде.
         * @param [in] includePort Если `true` — добавляет порт (`"127.0.0.1:8080"` или `"[::1]:8080"`); если `false` — только IP.
         * @return Строка с адресом или пустая строка `""`, если сокет закрыт, не привязан или произошла ошибка.
         * @throws std::bad_alloc При нехватке памяти для создания `std::string`.
         */
        std::string LocalAddress(bool includePort = false) const;
        /**
         * @brief Возвращает IP-адрес удалённого собеседника, с которым установлено соединение.
         * @param [in] includePort Если `true` — добавляет порт (`"IP:port"` или `"[IPv6]:port"`); если `false` — только IP.
         * @return Строка с адресом удалённой стороны или пустая строка `""`, если сокет не подключён.
         * @throws std::bad_alloc При нехватке памяти для создания `std::string`.
         */
        std::string RemoteAddress(bool includePort = false) const;
        /**
         * @brief Возвращает сырой системный дескриптор сокета (`SOCKET` на Windows, `int` на Linux).
         * @return Дескриптор сокета или `INVALID_SOCKET`, если сокет закрыт.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        socket_t SockHandle() const noexcept;
        /**
         * @brief Возвращает семейство адресов сокета.
         * @return `AF_INET`, `AF_INET6` или `0` для пустого сокета.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        int AddressFamily() const noexcept;
        /**
         * @brief Возвращает протокол сокета.
         * @return `IPPROTO_TCP`, `IPPROTO_UDP` или `0`.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        int ProtocolType() const noexcept;
        /**
         * @brief Возвращает тип сокета.
         * @return `SOCK_STREAM`, `SOCK_DGRAM` или `0`.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        int SockType() const noexcept;
        /**
         * @brief Возвращает локальный порт, на который привязан сокет (в порядке байтов процессора).
         * @return Номер порта (`0..65535`) при успехе, либо `-1`, если сокет закрыт или произошла ошибка.
         * @note Исключения не выбрасывает.
         */
        int LocalPort() const;
        /**
         * @brief Возвращает порт удалённой стороны подключённого сокета (в порядке байтов процессора).
         * @return Номер порта (`0..65535`) при успехе, либо `-1`, если сокет не подключён или закрыт.
         * @note Исключения не выбрасывает.
         */
        int RemotePort() const;
        /**
         * @brief Проверяет, открыт ли системный дескриптор сокета.
         * @return `true`, если `_handle != INVALID_SOCKET`; `false` иначе (выставляет `SOCK_EBADF`).
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        bool IsCreated() const noexcept;
        /**
         * @brief Проверяет, закрыт ли сокет (или ещё не создавался), не изменяя код последней ошибки.
         * @return `true`, если `_handle == INVALID_SOCKET`; `false`, если дескриптор активен.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        bool IsClosed() const noexcept;
        /**
         * @brief Проверяет, привязан ли сокет к ненулевому локальному порту (после `Bind` или `Connect`).
         * @return `true`, если сокет имеет назначенный локальный порт; `false` иначе.
         * @note Исключения не выбрасывает (`noexcept`).
         */
        bool IsBound() const noexcept;
        /**
         * @brief Проверяет, активно ли соединение прямо сейчас (без риска `SIGPIPE` и без извлечения данных из очереди).
         * @return `true`, если соединение живо; `false`, если сокет закрыт, не подключён или разорван удалённой стороной.
         * @note Исключения не выбрасывает (`noexcept`).
         */
        bool IsConnected() const noexcept;
        /**
         * @brief Возвращает метку времени создания сокета.
         * @return Объект `DateTime` момента создания (или `DateTime::Zero` для пустого сокета).
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        other::time::DateTime StartTime() const noexcept;
        /**
         * @brief Возвращает метку времени закрытия сокета.
         * @return Объект `DateTime` момента закрытия (или `DateTime::Zero`, если сокет ещё открыт).
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        other::time::DateTime CloseTime() const noexcept;
        /**
         * @brief Возвращает длительность работы (аптайм) сокета.
         * @return Разницу между временем закрытия (или текущим временем) и временем создания, либо `DateTime::Zero`.
         * @note Исключения не выбрасывает (`noexcept`).
         */
        other::time::DateTime SockUptime() const noexcept;


        ///////////////////////////////////////////////////////////////////////
        ///            НАСТРОЙКИ, ОПЦИИ И MULTICAST                         ///
        ///////////////////////////////////////////////////////////////////////
        /**
         * @brief Переключает сокет между блокирующим и неблокирующим режимом.
         * @param [in] enable `true` — включить блокирующий режим; `false` — перевести в неблокирующий режим.
         * @return `true` при успехе; `false`, если сокет закрыт или системный вызов (`ioctlsocket`/`fcntl`) завершился ошибкой.
         * @note Исключения не выбрасывает.
         */
        bool SetBlocking(bool enable = false);
        /**
         * @brief Проверяет, работает ли сокет в блокирующем режиме.
         * @return `true`, если сокет блокирующий; `false`, если неблокирующий или не создан.
         * @note Исключения не выбрасывает (`noexcept`).
         */
        bool IsBlocking() const noexcept;
        /**
         * @brief Включает или отключает опцию `SO_REUSEADDR` (позволяет серверу быстро перезапускаться на том же порту).
         * @param [in] enable `true` — разрешить повторное использование адреса; `false` — запретить.
         * @return `true` при успехе; `false` при ошибке или закрытом сокете.
         * @note Исключения не выбрасывает.
         */
        bool SetReuseAddress(bool enable = true);
        /**
         * @brief Проверяет, включена ли опция `SO_REUSEADDR`.
         * @return `true`, если опция активна; `false`, если выключена или произошла ошибка.
         * @note Исключения не выбрасывает.
         */
        bool IsReuseAddress() const;
        /**
         * @brief Запрещает другим процессам привязываться к тому же порту (опция `SO_EXCLUSIVEADDRUSE`).
         * @param [in] exclusive `true` — включить эксклюзивный захват порта; `false` — отключить.
         * @return `true` при успехе на Windows; на Linux всегда возвращает `false` (опция отсутствует).
         * @note Исключения не выбрасывает (`noexcept`).
         */
        bool SetExclusiveAddressUse(bool exclusive) noexcept;
        /**
         * @brief Проверяет статус опции `SO_EXCLUSIVEADDRUSE`.
         * @return `true`, если опция включена на Windows; `false` на Linux или при ошибке.
         * @note Исключения не выбрасывает (`noexcept`).
         */
        bool IsExclusiveAddressUse() const noexcept;
        /**
         * @brief Включает или отключает системный механизм TCP Keep-Alive (`SO_KEEPALIVE`).
         * @param [in] enable `true` — включить Keep-Alive; `false` — отключить.
         * @return `true` при успехе; `false` при ошибке или закрытом сокете.
         * @note Исключения не выбрасывает (`noexcept`).
         */
        bool SetKeepAlive(bool enable) noexcept;
        /**
         * @brief Проверяет, включён ли механизм `SO_KEEPALIVE`.
         * @return `true`, если Keep-Alive активен; `false` иначе.
         * @note Исключения не выбрасывает.
         */
        bool IsKeepAlive() const;
        /**
         * @brief Задаёт максимальное время ожидания отправки данных (`SO_SNDTIMEO`).
         * @param [in] timeout Таймаут в миллисекундах (`-1` или `0` = бесконечное ожидание; значения `1..499` округляются до `500` мс).
         * @return `0` при успехе; `-1` при ошибке или недопустимом значении (`timeout < -1` выставляет `SOCK_EINVAL`).
         * @note Исключения не выбрасывает (`noexcept`).
         */
        int SetSendTimeout(int timeout) noexcept;
        /**
         * @brief Задаёт максимальное время ожидания приёма данных (`SO_RCVTIMEO`).
         * @param [in] timeout Таймаут в миллисекундах (`-1` или `0` = бесконечное ожидание; значения `1..499` округляются до `500` мс).
         * @return `0` при успехе; `-1` при ошибке или недопустимом значении (`timeout < -1` выставляет `SOCK_EINVAL`).
         * @note Исключения не выбрасывает (`noexcept`).
         */
        int SetRecvTimeout(int timeout) noexcept;
        /**
         * @brief Возвращает текущий таймаут отправки (`SO_SNDTIMEO`).
         * @return Таймаут в миллисекундах (`> 0`), либо `-1`, если таймаут не ограничен или произошла ошибка.
         * @note Исключения не выбрасывает (`noexcept`).
         */
        int GetSendTimeout() const noexcept;
        /**
         * @brief Возвращает текущий таймаут приёма (`SO_RCVTIMEO`).
         * @return Таймаут в миллисекундах (`> 0`), либо `-1`, если таймаут не ограничен или произошла ошибка.
         * @note Исключения не выбрасывает (`noexcept`).
         */
        int GetRecvTimeout() const noexcept;
        /**
         * @brief Устанавливает размер системного буфера отправки ядра ОС (`SO_SNDBUF`).
         * @param [in] size Желаемый размер буфера в байтах (должен быть `> 0`).
         * @return `0` при успехе; `-1` при ошибке или `size <= 0` (выставляет `SOCK_EINVAL`).
         * @note Исключения не выбрасывает (`noexcept`).
         */
        int SetSendBufferSize(int size) noexcept;
        /**
         * @brief Устанавливает размер системного буфера приёма ядра ОС (`SO_RCVBUF`).
         * @param [in] size Желаемый размер буфера в байтах (должен быть `> 0`).
         * @return `0` при успехе; `-1` при ошибке или `size <= 0` (выставляет `SOCK_EINVAL`).
         * @note Исключения не выбрасывает (`noexcept`).
         */
        int SetRecvBufferSize(int size) noexcept;
        /**
         * @brief Возвращает фактический размер системного буфера отправки ядра ОС (`SO_SNDBUF`).
         * @return Размер в байтах (`>= 0`) при успехе; `-1` при ошибке или закрытом сокете.
         * @note Исключения не выбрасывает.
         */
        int GetSendBufferSize() const;
        /**
         * @brief Возвращает фактический размер системного буфера приёма ядра ОС (`SO_RCVBUF`).
         * @return Размер в байтах (`>= 0`) при успехе; `-1` при ошибке или закрытом сокете.
         * @note Исключения не выбрасывает.
         */
        int GetRecvBufferSize() const;
        /**
         * @brief Настраивает опцию `SO_LINGER` (управляет ожиданием отправки оставшихся байтов при закрытии сокета).
         * @param [in] enable  `true` — включить `SO_LINGER`; `false` — отключить.
         * @param [in] seconds Время ожидания в секундах (отрицательные приводятся к `0`, значения `> 65535` ограничиваются `65535`).
         * @return `0` при успехе; `-1` при ошибке или закрытом сокете.
         * @note Исключения не выбрасывает (`noexcept`).
         */
        int SetLingerState(bool enable, int seconds) noexcept;
        /**
         * @brief Читает текущие настройки `SO_LINGER`.
         * @param [out] enable  Сюда записывается `true`, если `SO_LINGER` включён, иначе `false`.
         * @param [out] seconds Сюда записывается время ожидания в секундах.
         * @return `0` при успехе; `-1` при ошибке или закрытом сокете.
         * @note Исключения не выбрасывает (`noexcept`).
         */
        int GetLingerState(bool& enable, int& seconds) const noexcept;
        /**
         * @brief Проверяет, отключён ли алгоритм Нейгла (`TCP_NODELAY`).
         * @return `true`, если `TCP_NODELAY` включён; `false` иначе или при ошибке.
         * @note Исключения не выбрасывает (`noexcept`).
         */
        bool IsNoDelay() const noexcept;
        /**
         * @brief Включает или отключает `TCP_NODELAY` (отключает буферизацию мелких пакетов алгоритмом Нейгла).
         * @param [in] enable `true` — отправлять мелкие пакеты немедленно; `false` — разрешить объединение пакетов.
         * @return `true` при успехе; `false` при ошибке или закрытом сокете.
         * @note Исключения не выбрасывает (`noexcept`).
         */
        bool SetNoDelay(bool enable = true) noexcept;
        /**
         * @brief Возвращает текущее значение TTL (`IP_TTL` для IPv4 / `IPV6_UNICAST_HOPS` для IPv6).
         * @return Значение TTL (`0..255`) при успехе; `-1` при ошибке или закрытом сокете.
         * @note Исключения не выбрасывает.
         */
        short GetTtl() const;
        /**
         * @brief Устанавливает TTL (максимальное число маршрутизаторов на пути пакета) для исходящих пакетов.
         * @param [in] value Новое значение TTL (`0..255`; выход за диапазон выставляет `SOCK_EINVAL`).
         * @return `true` при успехе; `false` при ошибке, неверном значении или закрытом сокете.
         * @note Исключения не выбрасывает.
         */
        bool SetTtl(short value);
        /**
         * @brief Проверяет флаг запрета фрагментации IP-пакетов (Don't Fragment).
         * @return `true`, если фрагментация запрещена; `false`, если разрешена или произошла ошибка.
         * @note Исключения не выбрасывает.
         */
        bool IsDontFragment() const;
        /**
         * @brief Включает или отключает флаг запрета фрагментации IP-пакетов (Don't Fragment).
         * @param [in] enable `true` — запретить фрагментацию пакетов; `false` — разрешить.
         * @return `true` при успехе; `false` при ошибке или закрытом сокете.
         * @note Исключения не выбрасывает.
         */
        bool SetDontFragment(bool enable = true);
        /**
         * @brief Проверяет, возвращаются ли исходящие multicast-пакеты обратно на локальный сокет.
         * @return `true`, если multicast loopback включён; `false` иначе или при ошибке.
         * @note Исключения не выбрасывает.
         */
        bool IsMulticastLoopback() const;
        /**
         * @brief Включает или отключает эхо-возврат исходящих multicast-пакетов на локальной машине.
         * @param [in] enable `true` — разрешить локальный возврат; `false` — запретить.
         * @return `true` при успехе; `false` при ошибке или закрытом сокете.
         * @note Исключения не выбрасывает.
         */
        bool SetMulticastLoopback(bool enable = true);
        /**
         * @brief Подписывает UDP-сокет на получение пакетов multicast-группы (IPv4 или IPv6).
         * @param [in] multicastAddress IP-адрес multicast-группы (например `"239.0.0.1"` для IPv4 или `"ff02::1"` для IPv6).
         * @return `true` при успешной подписке; `false` при неверном адресе (`SOCK_EINVAL`) или ошибке ОС.
         * @note Исключения не выбрасывает (`noexcept`).
         */
        bool JoinMulticastGroup(const std::string& multicastAddress) noexcept;
        /**
         * @brief Отписывает UDP-сокет от ранее подключённой multicast-группы.
         * @param [in] multicastAddress IP-адрес multicast-группы, из которой нужно выйти.
         * @return `true` при успешной отписке; `false` при неверном адресе (`SOCK_EINVAL`) или ошибке ОС.
         * @note Исключения не выбрасывает (`noexcept`).
         */
        bool LeaveMulticastGroup(const std::string& multicastAddress) noexcept;
        /**
         * @brief Разрешает или запрещает отправку широковещательных UDP-пакетов (`SO_BROADCAST`).
         * @param [in] enable `true` — разрешить broadcast; `false` — запретить.
         * @return `true` при успехе; `false` при ошибке или закрытом сокете.
         * @note Исключения не выбрасывает (`noexcept`).
         */
        bool SetBroadcast(bool enable) noexcept;
        /**
         * @brief Проверяет, разрешена ли отправка широковещательных пакетов (`SO_BROADCAST`).
         * @return `true`, если broadcast разрешён; `false` иначе или при ошибке.
         * @note Исключения не выбрасывает (`noexcept`).
         */
        bool IsBroadcast() const noexcept;
        /**
         * @brief Включает или отключает режим Dual-Stack (IPv4+IPv6 одновременно) на IPv6-сокете.
         * @param [in] enable `true` — включить Dual-Stack (`IPV6_V6ONLY = 0`); `false` — только IPv6 (`IPV6_V6ONLY = 1`).
         * @return `true` при успехе; `false`, если сокет закрыт, не относится к `AF_INET6` или произошла ошибка ОС.
         * @note Исключения не выбрасывает (`noexcept`).
         */
        bool SetDualMode(bool enable) noexcept;
        /**
         * @brief Проверяет, активен ли режим Dual-Stack на IPv6-сокете.
         * @return `true`, если `IPV6_V6ONLY == 0`; `false` для IPv4, закрытого сокета или при ошибке.
         * @note Исключения не выбрасывает (`noexcept`).
         */
        bool IsDualMode() const noexcept;


        ///////////////////////////////////////////////////////////////////////
        ///            УПРАВЛЕНИЕ ЖИЗНЕННЫМ ЦИКЛОМ И СОЕДИНЕНИЕМ            ///
        ///////////////////////////////////////////////////////////////////////
        /**
         * @brief Закрывает текущий сокет (если открыт) и берёт под управление RAII готовый системный дескриптор.
         * @param [in] handle Валидный системный дескриптор сокета (повторная передача текущего дескриптора безопасна).
         * @return `true` при успехе; `false`, если `handle == INVALID_SOCKET` (выставляет `SOCK_EBADF`).
         * @note Исключения не выбрасывает (`noexcept`).
         */
        bool Attach(socket_t handle) noexcept;
        /**
         * @brief Отдаёт владение системным дескриптором наружу, НЕ закрывая его, и сбрасывает объект в пустое состояние.
         * @return Прежний системный дескриптор (или `INVALID_SOCKET`, если сокет был закрыт).
         * @note Гарантированно не выбрасывает исключений (`noexcept`). После вызова за закрытие дескриптора отвечает вызывающий код.
         */
        socket_t Detach() noexcept;
        /**
         * @brief Создаёт новый независимый сокет с теми же параметрами (`_family`, `_type`, `_protocol`).
         * @return Новый объект `Socket` (не привязанный и не подключённый; при закрытом исходном сокете — пустой `Socket`).
         * @note Исключения не выбрасывает (`noexcept`).
         */
        Socket SockClone() const noexcept;
        /**
         * @brief Привязывает сокет к порту на всех локальных сетевых интерфейсах (`0.0.0.0` для IPv4 или `::` для IPv6).
         * @param [in] port Номер порта (`0..65535`; значение `0` просит ОС автоматически назначить свободный порт).
         * @return `true` при успешной привязке; `false` при неверном порте (`SOCK_EINVAL`), закрытом сокете или ошибке ОС.
         * @note Исключения не выбрасывает.
         */
        bool Bind(int port);
        /**
         * @brief Привязывает сокет к конкретному локальному IP-адресу/хосту и порту (поддерживает IPv4 на Dual-Stack).
         * @param [in] host Локальный IP-адрес или имя хоста (например `"127.0.0.1"`, `"::1"` или `""` для всех интерфейсов).
         * @param [in] port Номер порта (`0..65535`; `0` = автоматический выбор свободного порта).
         * @return `true` при успешной привязке; `false` при ошибке DNS, неверном порте или сбое `::bind()`.
         * @throws std::bad_alloc При нехватке памяти для временной строки порта.
         */
        bool Bind(const std::string& host, int port);
        /**
         * @brief Переводит серверный TCP-сокет в режим прослушивания входящих соединений.
         * @param [in] backlog Максимальный размер очереди ожидающих клиентов (`>= 0`, например `SOMAXCONN`).
         * @return `true` при успехе; `false` при `backlog < 0` (`SOCK_EINVAL`), закрытом сокете или ошибке ОС.
         * @note Исключения не выбрасывает (`noexcept`).
         */
        bool Listen(int backlog) noexcept;
        /**
         * @brief Извлекает одного входящего клиента из очереди подключений и возвращает новый `Socket` для общения с ним.
         * @return Валидный клиентский `Socket` при успехе, либо пустой закрытый `Socket` (`IsCreated() == false`) при ошибке.
         * @note Исключения не выбрасывает (`noexcept`). В блокирующем режиме ждёт прихода клиента.
         */
        Socket Accept() noexcept;
        /**
         * @brief Выполняет блокирующее подключение к удалённому серверу по IP или доменному имени (поддерживает IPv4 на Dual-Stack).
         * @param [in] host IP-адрес или доменное имя сервера (например `"127.0.0.1"`, `"example.com"`).
         * @param [in] port Порт сервера (`0..65535`).
         * @return `true` при успешном подключении; `false` при ошибке DNS, неверном порте или отказе соединения.
         * @throws std::bad_alloc При нехватке памяти для временной строки порта.
         */
        bool Connect(const std::string& host, int port);
        /**
         * @brief Подключается к удалённому серверу с жёстким общим ограничением времени ожидания.
         *
         * Внутри временно переводит сокет в неблокирующий режим, ждёт завершения TCP-рукопожатия через `poll`/`WSAPoll`
         * (с общим дедлайном на все адреса из DNS и защитой от `EINTR`) и проверяет `SO_ERROR`, после чего восстанавливает
         * исходный режим блокировки.
         * @param [in] host      IP-адрес или доменное имя сервера.
         * @param [in] port      Порт назначения (`0..65535`).
         * @param [in] timeoutMs Максимальное суммарное время ожидания в миллисекундах (`< 0` — обычное блокирующее подключение).
         * @return `true` при успешном подключении; `false` при таймауте (`SOCK_ETIMEDOUT`) или другой сетевой/DNS-ошибке.
         * @throws std::bad_alloc При нехватке памяти для временной строки порта.
         */
        bool Connect(const std::string& host, int port, int timeoutMs);
        /**
         * @brief Разрывает соединение (вызывает `shutdown` и закрывает сокет; на Windows может переиспользовать дескриптор).
         * @param [in] reuseSocket Если `true` — на Windows пытается сбросить сокет через `DisconnectEx` для повторного использования;
         *                         на Linux всегда закрывает сокет и возвращает `false`.
         * @return `true` при успешном разрыве; `false` при ошибке или если `reuseSocket == true` на Linux.
         * @note Исключения не выбрасывает.
         */
        bool Disconnect(bool reuseSocket);
        /**
         * @brief Закрывает сокет немедленно без ожидания `SO_LINGER`.
         * @return `true`, если сокет был успешно закрыт; `false`, если он уже был закрыт или произошла ошибка ОС.
         * @note Исключения не выбрасывает (`noexcept`).
         */
        bool Close() noexcept;
        /**
         * @brief Закрывает сокет; при `timeout > 0` включает `SO_LINGER`, чтобы ядро успело дослать остаток буфера.
         * @param [in] timeout Время ожидания доотправки в миллисекундах (`<= 0` = немедленное закрытие; `> 0` округляется вверх до секунд).
         * @return `true` при успешном закрытии; `false`, если сокет уже закрыт или произошла ошибка ОС.
         * @note Исключения не выбрасывает (`noexcept`).
         */
        bool Close(int timeout) noexcept;
        /**
         * @brief Полностью закрывает сокет и сбрасывает все поля, метки времени и счётчики трафика.
         * @return `true`, если сокет закрыт (или уже был закрыт); `false` при сбое закрытия ОС.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        bool Dispose() noexcept;
        /**
         * @brief Запрещает дальнейший приём данных на сокете (`shutdown` `SD_RECEIVE` / `SHUT_RD`).
         * @note Исключения не выбрасывает.
         */
        void DoRecv();
        /**
         * @brief Запрещает дальнейшую отправку данных (отправляет удалённой стороне TCP FIN: `shutdown` `SD_SEND` / `SHUT_WR`).
         * @note Исключения не выбрасывает.
         */
        void DoSend();
        /**
         * @brief Запрещает и приём, и отправку данных (`shutdown` `SD_BOTH` / `SHUT_RDWR`).
         * @note Исключения не выбрасывает.
         */
        void DoBoth();


		///////////////////////////////////////////////////////////////////////
        ///         ПРИЁМ И ОТПРАВКА ДАННЫХ (TCP И UDP)                     ///
        ///////////////////////////////////////////////////////////////////////
        /**
         * @brief Отправляет до `size` байт через подключённый сокет (на Linux автоматически добавляет `MSG_NOSIGNAL`).
         * @param [in] data  Указатель на буфер с данными (не может быть `nullptr`).
         * @param [in] size  Количество байт для отправки (`> 0`).
         * @param [in] flags Дополнительные флаги системного `::send()` (по умолчанию `0`).
         * @return Число реально отправленных байт (`> 0`); `0` в неблокирующем режиме, если буфер отправки полон
         *         (устанавливает `SOCK_EWOULDBLOCK`); `-1` при ошибке (код доступен через `GET_NET_ERROR()`).
         * @note Исключения не выбрасывает. Увеличивает счётчик `BytesSent()`.
         */
        int Send(const char* data, int size, int flags = 0);
        /**
         * @brief Отправляет до `size` байт, начиная со смещения `offset` в массиве `data`.
         * @param [in] data   Указатель на начало буфера.
         * @param [in] offset Смещение в байтах от начала буфера (`>= 0`).
         * @param [in] size   Количество байт для отправки (`> 0`).
         * @param [in] flags  Дополнительные флаги `::send()` (по умолчанию `0`).
         * @return Число отправленных байт (`> 0`), `0` при `EWOULDBLOCK` или `-1` при ошибке/неверных аргументах.
         * @note Исключения не выбрасывает.
         */
        int SendOffset(const char* data, int offset, int size, int flags = 0);
        /**
         * @brief Гарантированно отправляет ровно `size` байт в цикле (защита от частичной отправки в TCP и прерываний `EINTR`).
         * @param [in] data  Указатель на буфер с данными.
         * @param [in] size  Точное число байт для отправки (`> 0`).
         * @param [in] flags Дополнительные флаги `::send()` (по умолчанию `0`).
         * @return `true`, если отправлены все `size` байт; `false` при ошибке или `EWOULDBLOCK` на неблокирующем сокете.
         * @note Исключения не выбрасывает.
         */
        bool SendAll(const char* data, int size, int flags = 0);
        /**
         * @brief Гарантированно отправляет ровно `size` байт со смещения `offset`.
         * @param [in] data   Указатель на начало буфера.
         * @param [in] offset Смещение в байтах (`>= 0`).
         * @param [in] size   Точное число байт для отправки (`> 0`).
         * @param [in] flags  Дополнительные флаги `::send()` (по умолчанию `0`).
         * @return `true`, если отправлены все `size` байт; `false` при ошибке или неверных аргументах.
         * @note Исключения не выбрасывает.
         */
        bool SendAllOffset(const char* data, int offset, int size, int flags = 0);
        /**
         * @brief Отправляет UDP-датаграмму на указанный хост и порт без предварительного `Connect()` (поддерживает IPv4 на Dual-Stack).
         * @param [in] data   Указатель на данные для отправки.
         * @param [in] size   Размер датаграммы в байтах (`>= 0`).
         * @param [in] toHost IP-адрес или доменное имя получателя.
         * @param [in] port   Порт получателя (`0..65535`).
         * @param [in] flags  Дополнительные флаги `::sendto()` (по умолчанию `0`).
         * @return Число отправленных байт (`>= 0`) при успехе; `-1` при ошибке (в т.ч. DNS или `EWOULDBLOCK`).
         * @throws std::bad_alloc При нехватке памяти для временной строки порта.
         */
        int SendTo(const char* data, int size, const std::string& toHost, int port, int flags = 0);
        /**
         * @brief Отправляет UDP-датаграмму со смещением `offset` в массиве `data`.
         * @param [in] data   Указатель на начало буфера.
         * @param [in] offset Смещение в байтах (`>= 0`).
         * @param [in] size   Размер датаграммы в байтах (`>= 0`).
         * @param [in] toHost IP-адрес или доменное имя получателя.
         * @param [in] port   Порт получателя (`0..65535`).
         * @param [in] flags  Дополнительные флаги `::sendto()` (по умолчанию `0`).
         * @return Число отправленных байт (`>= 0`) при успехе; `-1` при ошибке.
         * @throws std::bad_alloc При нехватке памяти для временной строки порта.
         */
        int SendToOffset(const char* data, int offset, int size, const std::string& toHost, int port, int flags = 0);
        /**
         * @brief Принимает до `size` байт из подключённого сокета.
         * @param [out] data  Буфер для записи принятых данных (не может быть `nullptr`).
         * @param [in]  size  Максимальное число байт, которое вмещает буфер (`> 0`).
         * @param [in]  flags Дополнительные флаги `::recv()` (например `MSG_PEEK`; по умолчанию `0`).
         * @return Число принятых байт (`> 0`); `0`, если собеседник штатно закрыл соединение (EOF);
         *         `-1` при ошибке или в неблокирующем режиме, если данных ещё нет (`GET_NET_ERROR() == SOCK_EWOULDBLOCK`).
         * @note Исключения не выбрасывает. Увеличивает счётчик `BytesRecv()`.
         */
        int Recv(char* data, int size, int flags = 0);
        /**
         * @brief Принимает до `size` байт, записывая их в буфер `data` начиная со смещения `offset`.
         * @param [out] data   Указатель на начало буфера.
         * @param [in]  offset Смещение в байтах (`>= 0`).
         * @param [in]  size   Максимальное число байт для чтения (`> 0`).
         * @param [in]  flags  Дополнительные флаги `::recv()` (по умолчанию `0`).
         * @return Число принятых байт (`> 0`), `0` при штатном закрытии соединения или `-1` при ошибке / `SOCK_EWOULDBLOCK`.
         * @note Исключения не выбрасывает.
         */
        int RecvOffset(char* data, int offset, int size, int flags = 0);
        /**
         * @brief Принимает UDP-датаграмму и записывает IP-адрес и порт отправителя.
         * @param [out] data     Буфер для записи принятой датаграммы.
         * @param [in]  size     Размер буфера в байтах (`> 0`).
         * @param [out] fromHost Сюда записывается числовой IP-адрес отправителя.
         * @param [out] fromPort Сюда записывается порт отправителя (`0..65535`).
         * @param [in]  flags    Дополнительные флаги `::recvfrom()` (по умолчанию `0`).
         * @return Число принятых байт (`>= 0`, включая 0-байтную датаграмму); `-1` при ошибке или `SOCK_EWOULDBLOCK`.
         * @throws std::bad_alloc При нехватке памяти для записи строки `fromHost`.
         */
        int RecvFrom(char* data, int size, std::string& fromHost, int& fromPort, int flags = 0);
        /**
         * @brief Принимает UDP-датаграмму, записывая её со смещения `offset` в буфере `data`.
         * @param [out] data     Указатель на начало буфера.
         * @param [in]  offset   Смещение в байтах (`>= 0`).
         * @param [in]  size     Максимальный размер датаграммы (`> 0`).
         * @param [out] fromHost Сюда записывается IP-адрес отправителя.
         * @param [out] fromPort Сюда записывается порт отправителя.
         * @param [in]  flags    Дополнительные флаги `::recvfrom()` (по умолчанию `0`).
         * @return Число принятых байт (`>= 0`); `-1` при ошибке или `SOCK_EWOULDBLOCK`.
         * @throws std::bad_alloc При нехватке памяти для записи строки `fromHost`.
         */
        int RecvFromOffset(char* data, int offset, int size, std::string& fromHost, int& fromPort, int flags = 0);
        /**
         * @brief Гарантированно вычитывает ровно `size` байт в цикле (добирает фрагментированные TCP-пакеты и повторяет при `EINTR`).
         * @param [out] data  Буфер, вмещающий не менее `size` байт.
         * @param [in]  size  Точное число байт для чтения (`> 0`).
         * @param [in]  flags Дополнительные флаги `::recv()` (по умолчанию `0`).
         * @return `true`, если успешно вычитаны все `size` байт; `false` при обрыве соединения, `EWOULDBLOCK` или ошибке.
         * @note Исключения не выбрасывает.
         */
        bool RecvAll(char* data, int size, int flags = 0);
        /**
         * @brief Гарантированно вычитывает ровно `size` байт, записывая их со смещения `offset`.
         * @param [out] data   Указатель на начало буфера.
         * @param [in]  offset Смещение в байтах (`>= 0`).
         * @param [in]  size   Точное число байт для чтения (`> 0`).
         * @param [in]  flags  Дополнительные флаги `::recv()` (по умолчанию `0`).
         * @return `true`, если успешно вычитаны все `size` байт; `false` при ошибке, обрыве или неверных аргументах.
         * @note Исключения не выбрасывает.
         */
        bool RecvAllOffset(char* data, int offset, int size, int flags = 0);

        
		///////////////////////////////////////////////////////////////////////
        ///           ОЖИДАНИЕ СОБЫТИЙ (SELECT / POLL)                      ///
        ///////////////////////////////////////////////////////////////////////
        /**
         * @brief Отслеживает события сразу на группе сокетов через системный `::select()`.
         *
         * После возврата в переданных векторах остаются ТОЛЬКО те сокеты, на которых сработало соответствующее событие
         * (при истечении таймаута или отсутствии валидных дескрипторов все три вектора очищаются).
         * @param [in,out] readSock  Список сокетов для проверки готовности к чтению / `Accept`.
         * @param [in,out] writeSock Список сокетов для проверки готовности к записи.
         * @param [in,out] errorSock Список сокетов для проверки исключительных событий/ошибок.
         * @param [in]     timeoutMs    Таймаут ожидания в миллисекундах (`0` = мгновенная проверка; `< 0` = ждать бесконечно).
         * @return Количество готовых сокетов (`> 0`); `0` при таймауте или пустом наборе; `-1` при ошибке (в т.ч. при превышении `FD_SETSIZE`).
         * @note Исключения не выбрасывает.
         */
        static int Select(std::vector<Socket*>& readSock, std::vector<Socket*>& writeSock, std::vector<Socket*>& errorSock, long timeoutMs = 0);
        /**
         * @brief Проверяет через `poll`/`WSAPoll`, готов ли сокет к чтению (пришли данные или закрытие соединения).
         * @param [in] timeoutMs Таймаут ожидания в миллисекундах (`0` = проверить сразу; `< 0` = ждать бесконечно).
         * @return `true`, если сокет готов к чтению (`POLLIN` или `POLLHUP`); `false` при таймауте или ошибке.
         * @note Исключения не выбрасывает (`noexcept`). Не имеет ограничения `FD_SETSIZE` (1024) на Linux.
         */
        bool PollRead(long timeoutMs = 0) const noexcept;
        /**
         * @brief Проверяет через `poll`/`WSAPoll`, готов ли сокет к отправке данных без блокировки.
         * @param [in] timeoutMs Таймаут ожидания в миллисекундах (`0` = проверить сразу; `< 0` = ждать бесконечно).
         * @return `true`, если в буфере отправки ОС есть свободное место (`POLLOUT`); `false` при таймауте или ошибке.
         * @note Исключения не выбрасывает (`noexcept`).
         */
        bool PollWrite(long timeoutMs = 0) const noexcept;
        /**
         * @brief Проверяет через `poll`/`WSAPoll`, произошла ли ошибка или разрыв соединения на сокете.
         * @param [in] timeoutMs Таймаут ожидания в миллисекундах (`0` = проверить сразу; `< 0` = ждать бесконечно).
         * @return `true`, если обнаружен флаг ошибки/разрыва (`POLLERR`/`POLLHUP`/`POLLNVAL`); `false` иначе.
         * @note Исключения не выбрасывает (`noexcept`).
         */
        bool PollError(long timeoutMs = 0) const noexcept;
    };
}

#endif // LIBOTHER_NET_SOCK_H
