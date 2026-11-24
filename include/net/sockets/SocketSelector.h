#ifndef LIBOTHER_NET_SOCK_SELECTOR_H
#define LIBOTHER_NET_SOCK_SELECTOR_H


#include "Socket.h"


namespace other::net::sockets
{
    /**
     * @struct SelectorEvent
     * @brief Описание одного сработавшего события сокета, возвращаемого из `SocketSelector::ReadyEvents()`.
     *
     * Содержит указатель на отслеживаемый объект `Socket`, его системный дескриптор и унифицированную
     * битовую маску произошедших событий (`events`), независимую от платформенных констант `POLL*` / `EPOLL*`.
     */
    struct SelectorEvent
    {
        /** @brief Нет активных событий. */
        static constexpr uint32_t None    = 0U;
        /** @brief В буфере сокета есть входящие данные (или входящее подключение на слушающем TCP-сокете). */
        static constexpr uint32_t Read    = 1U << 0;
        /** @brief Буфер отправки ОС готов принять данные (либо завершился неблокирующий `Connect()`). */
        static constexpr uint32_t Write   = 1U << 1;
        /** @brief Критическая ошибка сокета (`POLLERR` / `EPOLLERR`, например `RST` или отказ `Connect()`). */
        static constexpr uint32_t Error   = 1U << 2;
        /** @brief Удалённая сторона закрыла соединение (`POLLHUP` / `EPOLLHUP` / `EPOLLRDHUP`). */
        static constexpr uint32_t HangUp  = 1U << 3;
        /** @brief Недействительный системный дескриптор (`POLLNVAL`, обычно закрыт до вызова `Remove()`). */
        static constexpr uint32_t Invalid = 1U << 4;

        const Socket* socket = nullptr;        ///< Указатель на объект `Socket`, зарегистрированный в селекторе.
        socket_t      handle = INVALID_SOCKET; ///< Системный дескриптор сокета на момент события.
        uint32_t      events = None;           ///< Битовая маска сработавших флагов (`Read | Write | Error | HangUp | Invalid`).

        /**
         * @brief Проверяет, требуется ли обработка чтения (пришли данные, получено закрытие `HangUp` или ошибка `Error`).
         * @return `true`, если можно вызывать `Recv()` / `Accept()` (либо обработать EOF/ошибку); `false` иначе.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        bool IsReadable() const noexcept;
        /**
         * @brief Проверяет, готов ли сокет к записи данных без блокировки (либо завершился неблокирующий `Connect()`).
         * @return `true`, если выставлен флаг `Write` или `Error`; `false` иначе.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        bool IsWritable() const noexcept;
        /**
         * @brief Проверяет наличие любой ошибки или разрыва соединения (`Error | HangUp | Invalid`).
         * @return `true`, если на сокете зафиксирована ошибка, разрыв связи или невалидный дескриптор; `false` иначе.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        bool HasError() const noexcept;
        /**
         * @brief Проверяет наличие только критической ошибки (`Error | Invalid`), исключая штатное закрытие `HangUp`.
         *
         * Полезно, когда при `HangUp` нужно сначала дочитать оставшиеся данные из буфера ядра через `Recv()`.
         * @return `true`, если выставлен флаг `Error` или `Invalid`; `false` иначе.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        bool HasCriticalError() const noexcept;
        /**
         * @brief Проверяет, получено ли уведомление о закрытии соединения удалённой стороной (`HangUp`).
         * @return `true`, если выставлен флаг `HangUp`; `false` иначе.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        bool IsHangUp() const noexcept;
    };

    /**
     * @class SocketSelector
     * @brief Высокопроизводительный кроссплатформенный мультиплексор сокетов (`epoll` на Linux / `WSAPoll` на Windows).
     *
     * Позволяет в одном потоке отслеживать от единиц до сотен тысяч и миллионов (на Linux) сетевых
     * соединений в неблокирующем режиме (Reactor Pattern).
     *
     * Политика владения и памяти:
     * - Селектор НЕ владеет добавленными объектами `Socket` и не закрывает их в деструкторе.
     * - Все указатели в `_sockets` и `ReadyEvents()` действительны, пока сами объекты `Socket` живы
     *   и не перемещены в памяти (для большого числа соединений рекомендуется хранить `Socket`
     *   в `std::unordered_map<socket_t, Socket>`, `std::deque<Socket>` или через `std::unique_ptr<Socket>`).
     *
     * Политика обработки ошибок:
     * Сетевые и системные ошибки не выбрасывают исключений — методы возвращают `false` / `-1`,
     * а точный код ошибки ОС сохраняется в `GET_NET_ERROR()`. Единственное возможное исключение —
     * `std::bad_alloc` при расширении внутренних контейнеров (`Add`, `Reserve`, `SetMaxEventsPerPoll`, `Poll`).
     */
    class SocketSelector
    {
    private:
        /**
         * @struct SlotState
         * @brief Внутреннее состояние результатов последнего опроса для одного зарегистрированного сокета.
         */
        struct SlotState
        {
            uint64_t lastEpoch   = 0;                   ///< Номер вызова `Poll()`, на котором сработал этот сокет.
            uint32_t lastEvents  = SelectorEvent::None; ///< Унифицированная маска событий на эпохе `lastEpoch`.
        };

        std::vector<const Socket*>           _sockets;          ///< Главный плотный массив указателей на отслеживаемые объекты `Socket`.
        std::vector<SlotState>               _slotStates;       ///< Параллельный массив результатов опроса с меткой эпохи (для $O(1)$ без очистки всего массива).
        std::unordered_map<socket_t, size_t> _indexMap;         ///< Карта (`socket_t` -> индекс в `_sockets`) для доступа и удаления за $O(1)$.
        std::vector<SelectorEvent>           _readyEvents;      ///< Буфер событий, сработавших на последнем вызове `Poll()` (обход за $O(K)$).
        uint64_t                             _pollEpoch;        ///< Монотонно растущий счётчик вызовов `Poll()`.
        size_t                               _maxEventsPerPoll; ///< Максимальный размер батча событий за один вызов `Poll()`.

#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
        std::vector<pollfd>                  _pollFds;          ///< Плотный массив системных структур `pollfd` для `WSAPoll` (синхронен с `_sockets`).
#else
        int                                  _epollFd;          ///< Системный файловый дескриптор экземпляра `epoll` (`-1`, если не создан).
        std::vector<socket_t>                _handles;          ///< Параллельный массив системных дескрипторов (синхронен с `_sockets`).
        std::vector<epoll_event>             _epollEvents;      ///< Рабочий буфер событий для `::epoll_wait()` (размером `_maxEventsPerPoll`).
#endif

    protected:
        /**
         * @brief Помощник: удаляет элемент по индексу `idx` за $O(1)$ (swap-and-pop) из всех массивов и ядра ОС.
         * @param [in] idx Индекс удаляемого элемента в `_sockets` (должен быть `< _sockets.size()`).
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        void RemoveSlot(size_t idx) noexcept;
        /**
         * @brief Помощник: безопасно увеличивает `_pollEpoch` с обработкой теоретического переполнения `uint64_t`.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        void AdvanceEpoch() noexcept;

    public:
	    /** @brief Максимальное число событий, считываемых за один вызов `Poll()` по умолчанию (4096). */
        static constexpr size_t DefaultMaxEventsPerPoll = 4096;
	
        /**
         * @brief Создаёт пустой селектор и инициализирует системный бэкенд (`epoll_create1` на Linux).
         * @param [in] initialCapacity  Начальная резервируемая ёмкость под количество соединений (по умолчанию `0`).
         * @param [in] maxEventsPerPoll Максимальное число событий, извлекаемых за один вызов `Poll()` (по умолчанию `4096`).
         * @note На Linux при сбое `epoll_create1()` исключение не выбрасывается: `IsValid()` вернёт `false`,
         *       а код ошибки ОС доступен через `GET_NET_ERROR()`.
         * @throws std::bad_alloc При нехватке памяти для предварительного резервирования контейнеров.
         */
        explicit SocketSelector(size_t initialCapacity = 0, size_t maxEventsPerPoll = DefaultMaxEventsPerPoll);
        /**
         * @brief Деструктор: освобождает системный дескриптор `epoll` (на Linux) и очищает внутренние контейнеры.
         * @note Гарантированно не выбрасывает исключений (`noexcept`). Отслеживаемые сокеты НЕ закрывает.
         */
        ~SocketSelector() noexcept;

        // Копирование запрещено: селектор владеет системным ресурсом (epoll fd) и уникальным набором дескрипторов.
        SocketSelector(const SocketSelector&) = delete;
        SocketSelector& operator=(const SocketSelector&) = delete;
        /**
         * @brief Конструктор перемещения: переносит владение системным бэкендом и набором сокетов за $O(1)$.
         * @param [in,out] other Исходный селектор (после перемещения становится пустым и закрытым).
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        SocketSelector(SocketSelector&& other) noexcept;
        /**
         * @brief Оператор перемещающего присваивания: освобождает текущий бэкенд и забирает состояние `other`.
         * @param [in,out] other Исходный селектор (после перемещения обнуляется).
         * @return Ссылка на текущий объект (`*this`).
         * @note Гарантированно не выбрасывает исключений (`noexcept`). Безопасен при самоприсваивании.
         */
        SocketSelector& operator=(SocketSelector&& other) noexcept;


        ///////////////////////////////////////////////////////////////////////
        ///            ИНФОРМАЦИЯ О БЭКЕНДЕ И НАСТРОЙКА ЁМКОСТИ             ///
        ///////////////////////////////////////////////////////////////////////
        /**
         * @brief Проверяет, успешно ли инициализирован системный бэкенд селектора.
         * @return `true`, если селектор готов к работе (`_epollFd >= 0` на Linux, всегда `true` на Windows); `false` иначе.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        bool IsValid() const noexcept;
        /**
         * @brief Возвращает название активного системного бэкенда мультиплексирования.
         * @return `"epoll"` на Linux или `"WSAPoll"` на Windows.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        static constexpr const char* BackendName() noexcept;
        /**
         * @brief Указывает, поддерживает ли активный бэкенд режим Edge-Triggered (`EPOLLET`).
         * @return `true` на Linux (`epoll`), `false` на Windows (`WSAPoll` всегда Level-Triggered).
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        static constexpr bool SupportsEdgeTriggered() noexcept;
        /**
         * @brief Заранее резервирует память во всех внутренних контейнерах под `capacity` соединений.
         *
         * Рекомендуется вызывать перед запуском высоконагруженного сервера (например, `Reserve(1000000)`),
         * чтобы избежать реаллокаций векторов и рехеширования `_indexMap` при массовом подключении клиентов.
         * @param [in] capacity Ожидаемое максимальное количество одновременных соединений.
         * @throws std::bad_alloc При нехватке оперативной памяти.
         */
        void Reserve(size_t capacity);
        /**
         * @brief Задаёт максимальное количество событий, возвращаемых за один вызов `Poll()`.
         * @param [in] maxEvents Размер батча (значения `< 1` автоматически приводятся к `1`).
         * @throws std::bad_alloc При нехватке памяти для изменения размера буферов событий.
         */
        void SetMaxEventsPerPoll(size_t maxEvents);
        /**
         * @brief Возвращает текущий размер батча событий за один вызов `Poll()`.
         * @return Максимальное число событий за один вызов `Poll()`.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        size_t MaxEventsPerPoll() const noexcept;


        ///////////////////////////////////////////////////////////////////////
        ///            УПРАВЛЕНИЕ НАБОРОМ ОТСЛЕЖИВАЕМЫХ СОКЕТОВ             ///
        ///////////////////////////////////////////////////////////////////////
        /**
         * @brief Добавляет сокет в селектор или обновляет его маску событий и указатель за $O(1)$.
         *
         * Если сокет уже находится в селекторе, обновляются его флаги отслеживания (`watchRead`, `watchWrite`,
         * `edgeTriggered`) и актуальный указатель в `_sockets`. Для `StreamSocket` передавайте `stream.SockInternal()`.
         * Обеспечивает строгую гарантию исключений: при `std::bad_alloc` или ошибке `epoll_ctl` состояние не повреждается.
         * @param [in] socket        Ссылка на открытый объект `Socket`.
         * @param [in] watchRead     Если `true` — отслеживать входящие данные (`POLLIN` / `EPOLLIN`) и закрытие соединения.
         * @param [in] watchWrite    Если `true` — отслеживать готовность буфера отправки (`POLLOUT` / `EPOLLOUT`).
         * @param [in] edgeTriggered Если `true` — включить Edge-Triggered режим (`EPOLLET` на Linux; на Windows игнорируется).
         * @return `true` при успехе; `false`, если сокет закрыт (`SOCK_EBADF`), селектор невалиден или `epoll_ctl()` завершился ошибкой.
         * @throws std::bad_alloc При нехватке памяти для расширения внутренних контейнеров.
         */
        bool Add(const Socket& socket, bool watchRead = true, bool watchWrite = false, bool edgeTriggered = false);
        /**
         * @brief Изменяет маску отслеживаемых событий уже зарегистрированного сокета за $O(1)$ без выделения памяти.
         * @param [in] socket        Ссылка на открытый объект `Socket`, уже добавленный в селектор.
         * @param [in] watchRead     Новое состояние отслеживания чтения.
         * @param [in] watchWrite    Новое состояние отслеживания записи.
         * @param [in] edgeTriggered Новое состояние режима `EPOLLET` (на Linux; на Windows игнорируется).
         * @return `true` при успехе; `false`, если сокет закрыт (`SOCK_EBADF`), не найден (`SOCK_ENOENT`) или `epoll_ctl()` вернул ошибку.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        bool Modify(const Socket& socket, bool watchRead, bool watchWrite, bool edgeTriggered = false) noexcept;
        /**
         * @brief Удаляет сокет из набора отслеживания.
         *
         * Для открытого сокета удаление выполняется за $O(1)$ по его системному дескриптору.
         * Если сокет был закрыт до вызова `Remove()`, выполняется резервный поиск по адресу `&socket`
         * в главном массиве `_sockets`, чтобы предотвратить утечку старой записи.
         * Безопасно вызывать во время обхода `ReadyEvents()`.
         * @param [in] socket Ссылка на удаляемый объект `Socket`.
         * @return `true`, если сокет был найден и удалён; `false`, если он отсутствовал в селекторе.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        bool Remove(const Socket& socket) noexcept;
        /**
         * @brief Удаляет сокет из набора отслеживания напрямую по системному дескриптору за $O(1)$.
         *
         * Удобно вызывать после закрытия объекта `Socket` (используя заранее сохранённый `socket_t` или `ev.handle`
         * из `ReadyEvents()`), чтобы избежать линейного поиска.
         * @param [in] handle Системный дескриптор сокета (`socket_t`).
         * @return `true`, если дескриптор был найден и удалён; `false`, если он невалиден или отсутствует в селекторе.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        bool Remove(socket_t handle) noexcept;
        /**
         * @brief Полностью очищает весь набор отслеживаемых сокетов и буфер готовых событий.
         * @note Гарантированно не выбрасывает исключений (`noexcept`). Зарезервированная память (`Reserve`) сохраняется.
         */
        void Clear() noexcept;
        /**
         * @brief Проверяет за $O(1)$, отслеживается ли указанный сокет в селекторе.
         * @param [in] socket Проверяемый объект `Socket`.
         * @return `true`, если сокет зарегистрирован в селекторе; `false` иначе.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        bool Contains(const Socket& socket) const noexcept;
        /**
         * @brief Проверяет за $O(1)$, отслеживается ли системный дескриптор в селекторе.
         * @param [in] handle Системный дескриптор сокета.
         * @return `true`, если дескриптор зарегистрирован в селекторе; `false` иначе.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        bool Contains(socket_t handle) const noexcept;


        ///////////////////////////////////////////////////////////////////////
        ///            ОПРОС СОБЫТИЙ (EVENT LOOP) И ПРОВЕРКА СОСТОЯНИЯ      ///
        ///////////////////////////////////////////////////////////////////////
        /**
         * @brief Ожидает наступления событий на отслеживаемых сокетах и заполняет `ReadyEvents()`.
         *
         * На Linux использует `::epoll_wait()` со сложностью $O(K)$ и автоматическим перезапуском
         * при прерывании сигналом (`EINTR`) с пересчётом оставшегося таймаута.
         * На Windows использует `::WSAPoll()`.
         * @param [in] timeoutMs Максимальное время ожидания в миллисекундах:
         *                       - `< 0` — ждать бесконечно до наступления события;
         *                       - `0`   — мгновенный неблокирующий опрос;
         *                       - `> 0` — ждать не дольше указанного числа миллисекунд.
         * @return Количество готовых сокетов (`> 0`, совпадает с `ReadyEvents().size()`);
         *         `0` — если набор пуст или истёк таймаут;
         *         `-1` — при системной ошибке (код доступен через `GET_NET_ERROR()`).
         * @throws std::bad_alloc На Windows при росте буфера `_readyEvents`, если его ёмкости недостаточно.
         */
        int Poll(int timeoutMs = -1);
        /**
         * @brief Возвращает список ТОЛЬКО тех сокетов, на которых сработали события в последнем вызове `Poll()`.
         *
         * Основной способ обработки событий при большом числе соединений ($O(K)$ вместо обхода всех $N$ сокетов).
         * Контейнер остаётся валидным до следующего вызова `Poll()` или `Clear()`, поэтому внутри цикла по
         * `ReadyEvents()` безопасно вызывать `Add()`, `Modify()` и `Remove()`.
         * @return Константная ссылка на вектор структур `SelectorEvent`.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        const std::vector<SelectorEvent>& ReadyEvents() const noexcept;
        /**
         * @brief Возвращает константную ссылку на главный вектор всех отслеживаемых объектов `Socket`.
         * @return Константная ссылка на внутренний `std::vector<const Socket*>`.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        const std::vector<const Socket*>& Sockets() const noexcept;
        /**
         * @brief Возвращает полную битовую маску событий (`SelectorEvent::*`) для сокета после последнего `Poll()` за $O(1)$.
         * @param [in] socket Проверяемый объект `Socket`.
         * @return Битовая маска событий или `SelectorEvent::None`, если на сокете событий не было.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        uint32_t GetEvents(const Socket& socket) const noexcept;
        /**
         * @brief Возвращает полную битовую маску событий (`SelectorEvent::*`) по системному дескриптору за $O(1)$.
         * @param [in] handle Системный дескриптор сокета.
         * @return Битовая маска событий или `SelectorEvent::None`, если событий не было.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        uint32_t GetEvents(socket_t handle) const noexcept;
        /**
         * @brief Проверяет за $O(1)$, готов ли сокет к чтению (после выполнения `Poll()`).
         *
         * Возвращает `true`, если пришли входящие данные (`Read`), получено закрытие соединения (`HangUp`)
         * или зафиксирована ошибка (`Error`).
         * @param [in] socket Проверяемый объект `Socket`.
         * @return `true`, если сокет доступен для чтения или требует обработки отключения/ошибки; `false` иначе.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        bool IsReadable(const Socket& socket) const noexcept;
        /**
         * @brief Проверяет за $O(1)$, готов ли сокет к записи (после выполнения `Poll()`).
         *
         * Возвращает `true`, если буфер отправки ОС готов принять данные (`Write`) или завершился неблокирующий
         * `Connect()` (включая отказ соединения `Error`).
         * @param [in] socket Проверяемый объект `Socket`.
         * @return `true`, если выставлен флаг `Write` или `Error`; `false` иначе.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        bool IsWritable(const Socket& socket) const noexcept;
        /**
         * @brief Проверяет за $O(1)$, произошла ли ошибка или разрыв соединения на сокете (после `Poll()`).
         * @param [in] socket Проверяемый объект `Socket`.
         * @return `true`, если выставлен флаг `Error`, `HangUp` или `Invalid`; `false` иначе.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        bool HasError(const Socket& socket) const noexcept;
        /**
         * @brief Проверяет за $O(1)$ наличие критической ошибки (`Error | Invalid`), не считая штатное закрытие `HangUp`.
         * @param [in] socket Проверяемый объект `Socket`.
         * @return `true`, если на сокете произошла критическая ошибка или дескриптор невалиден; `false` иначе.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        bool HasCriticalError(const Socket& socket) const noexcept;
        /**
         * @brief Проверяет за $O(1)$, получено ли уведомление о закрытии соединения удалённой стороной (`HangUp`).
         * @param [in] socket Проверяемый объект `Socket`.
         * @return `true`, если выставлен флаг `HangUp`; `false` иначе.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        bool IsHangUp(const Socket& socket) const noexcept;
        /**
         * @brief Возвращает текущее количество сокетов, находящихся под наблюдением селектора.
         * @return Количество отслеживаемых сокетов.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        size_t Size() const noexcept;
        /**
         * @brief Проверяет, пуст ли селектор (`Size() == 0`).
         * @return `true`, если в селекторе нет отслеживаемых сокетов; `false` иначе.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        bool Empty() const noexcept;
    };
}

#endif // LIBOTHER_NET_SOCK_SELECTOR_H
