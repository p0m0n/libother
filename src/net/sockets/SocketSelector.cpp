#include "SocketSelector.h"


namespace other::net::sockets
{
    namespace
    {
#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
        // Преобразует булевы флаги наблюдения в системную маску событий WSAPoll.
        inline short BuildPollEvents(bool watchRead, bool watchWrite) noexcept
        {
            short events = 0;
            if (watchRead)
                events |= POLLIN;
            if (watchWrite)
                events |= POLLOUT;
            return events;
        }

        // Преобразует системную маску WSAPoll (revents) в унифицированные флаги SelectorEvent.
        inline uint32_t TranslatePollRevents(short revents) noexcept
        {
            uint32_t mask = SelectorEvent::None;
            // В WSAPoll внеполосные данные сигнализируются флагом POLLRDBAND вместо POSIX POLLPRI.
            if (revents & (POLLIN | POLLRDBAND))
                mask |= SelectorEvent::Read;
            if (revents & POLLOUT)
                mask |= SelectorEvent::Write;
            if (revents & POLLERR)
                mask |= SelectorEvent::Error;
            if (revents & POLLHUP)
                mask |= SelectorEvent::HangUp;
            if (revents & POLLNVAL)
                mask |= SelectorEvent::Invalid;
            return mask;
        }
#else
        // Преобразует параметры наблюдения в системную маску epoll.
        inline uint32_t BuildEpollEvents(bool watchRead, bool watchWrite, bool edgeTriggered) noexcept
        {
            uint32_t events = 0U;
            // EPOLLRDHUP позволяет мгновенно обнаруживать полузакрытие (FIN) удалённой стороны.
            if (watchRead)
                events |= (EPOLLIN | EPOLLRDHUP);
            if (watchWrite)
                events |= EPOLLOUT;
            if (edgeTriggered)
                events |= EPOLLET;
            return events;
        }

        // Преобразует системную маску epoll_event::events в унифицированные флаги SelectorEvent.
        inline uint32_t TranslateEpollEvents(uint32_t epollEvents) noexcept
        {
            uint32_t mask = SelectorEvent::None;
            if (epollEvents & (EPOLLIN | EPOLLPRI))
                mask |= SelectorEvent::Read;
            if (epollEvents & EPOLLOUT)
                mask |= SelectorEvent::Write;
            if (epollEvents & EPOLLERR)
                mask |= SelectorEvent::Error;
            if (epollEvents & (EPOLLHUP | EPOLLRDHUP))
                mask |= SelectorEvent::HangUp;
            return mask;
        }
#endif
    }

	// Проверяет, требуется ли обработка чтения (пришли данные, получено закрытие `HangUp` или ошибка `Error`).
    bool SelectorEvent::IsReadable() const noexcept { return (events & (Read | HangUp | Error)) != 0U; }
    // Проверяет, готов ли сокет к записи данных без блокировки (либо завершился неблокирующий `Connect()`).
    bool SelectorEvent::IsWritable() const noexcept { return (events & (Write | Error)) != 0U; }
    // Проверяет наличие любой ошибки или разрыва соединения (`Error | HangUp | Invalid`).
    bool SelectorEvent::HasError() const noexcept { return (events & (Error | HangUp | Invalid)) != 0U; }
    // Проверяет наличие только критической ошибки (`Error | Invalid`), исключая штатное закрытие `HangUp`.
    bool SelectorEvent::HasCriticalError() const noexcept { return (events & (Error | Invalid)) != 0U; }
    // Проверяет, получено ли уведомление о закрытии соединения удалённой стороной (`HangUp`).
    bool SelectorEvent::IsHangUp() const noexcept { return (events & HangUp) != 0U; }
		

    // Создаёт селектор, инициализирует epoll (на Linux) и резервирует рабочие буферы.
    SocketSelector::SocketSelector(size_t initialCapacity, size_t maxEventsPerPoll)
        : _pollEpoch(0)
        , _maxEventsPerPoll(maxEventsPerPoll > 0 ? maxEventsPerPoll : 1U)
#if LIBOTHER_PLATFORM != LIBOTHER_PLATFORM_WINDOWS
        , _epollFd(-1)
#endif
    {
#if LIBOTHER_PLATFORM != LIBOTHER_PLATFORM_WINDOWS
        // Создаём экземпляр epoll с флагом EPOLL_CLOEXEC (дескриптор не утечёт в дочерние процессы при exec).
        _epollFd = ::epoll_create1(EPOLL_CLOEXEC);
        if (_epollFd < 0)
            SET_NET_ERROR(GET_NET_ERROR());
        else
            _epollEvents.resize(_maxEventsPerPoll);
#endif
        // Заранее резервируем буфер готовых событий под размер батча.
        _readyEvents.reserve(_maxEventsPerPoll);

        // При заданной начальной ёмкости резервируем память под все соединения заранее.
        if (initialCapacity > 0)
            Reserve(initialCapacity);
    }

    // Деструктор: закрывает системный дескриптор epoll на Linux (сами сокеты не трогает).
    SocketSelector::~SocketSelector() noexcept
    {
#if LIBOTHER_PLATFORM != LIBOTHER_PLATFORM_WINDOWS
        if (_epollFd >= 0)
        {
            ::close(_epollFd);
            _epollFd = -1;
        }
#endif
    }

    // Конструктор перемещения: переносит владение epoll и всеми контейнерами за O(1).
    SocketSelector::SocketSelector(SocketSelector&& other) noexcept
        : _sockets(std::move(other._sockets))
        , _slotStates(std::move(other._slotStates))
        , _indexMap(std::move(other._indexMap))
        , _readyEvents(std::move(other._readyEvents))
        , _pollEpoch(other._pollEpoch)
        , _maxEventsPerPoll(other._maxEventsPerPoll)
#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
        , _pollFds(std::move(other._pollFds))
#else
        , _epollFd(other._epollFd)
        , _handles(std::move(other._handles))
        , _epollEvents(std::move(other._epollEvents))
#endif
    {
#if LIBOTHER_PLATFORM != LIBOTHER_PLATFORM_WINDOWS
        // Обнуляем дескриптор у источника, чтобы его деструктор не закрыл наш epoll.
        other._epollFd = -1;
#endif
        other._pollEpoch = 0;
    }

    // Оператор перемещающего присваивания.
    SocketSelector& SocketSelector::operator=(SocketSelector&& other) noexcept
    {
        if (this != &other)
        {
#if LIBOTHER_PLATFORM != LIBOTHER_PLATFORM_WINDOWS
            // Освобождаем собственный дескриптор epoll перед переносом.
            if (_epollFd >= 0)
                ::close(_epollFd);
#endif
            _sockets          = std::move(other._sockets);
            _slotStates       = std::move(other._slotStates);
            _indexMap         = std::move(other._indexMap);
            _readyEvents      = std::move(other._readyEvents);
            _pollEpoch        = other._pollEpoch;
            _maxEventsPerPoll = other._maxEventsPerPoll;

#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
            _pollFds          = std::move(other._pollFds);
#else
            _epollFd          = other._epollFd;
            _handles          = std::move(other._handles);
            _epollEvents      = std::move(other._epollEvents);
            other._epollFd    = -1;
#endif
            other._pollEpoch  = 0;
        }
        return *this;
    }

    // Безопасно увеличивает номер эпохи Poll(); при переполнении uint64_t сбрасывает метки слотов.
    void SocketSelector::AdvanceEpoch() noexcept
    {
        ++_pollEpoch;
        if (_pollEpoch == 0)
        {
            for (auto& state : _slotStates)
                state.lastEpoch = 0;
            _pollEpoch = 1;
        }
    }

    // Удаляет слот по индексу idx за O(1) через перенос последнего элемента (swap-and-pop).
    void SocketSelector::RemoveSlot(size_t idx) noexcept
    {
        const size_t lastIdx = _sockets.size() - 1;

#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
        const socket_t removedHandle = _pollFds[idx].fd;
#else
        const socket_t removedHandle = _handles[idx];
        // Удаляем дескриптор из epoll (игнорируем EBADF/ENOENT, если сокет уже был закрыт пользователем).
        if (_epollFd >= 0)
            ::epoll_ctl(_epollFd, EPOLL_CTL_DEL, removedHandle, nullptr);
#endif

        // Если удаляемый элемент не последний — переносим последний элемент на его место во всех массивах.
        if (idx != lastIdx)
        {
            _sockets[idx]    = _sockets[lastIdx];
            _slotStates[idx] = _slotStates[lastIdx];

#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
            _pollFds[idx]    = _pollFds[lastIdx];
            const socket_t movedHandle = _pollFds[idx].fd;
#else
            _handles[idx]    = _handles[lastIdx];
            const socket_t movedHandle = _handles[idx];
#endif
            // Обновляем индекс перемещённого сокета в хэш-таблице за O(1).
            auto movedIt = _indexMap.find(movedHandle);
            if (movedIt != _indexMap.end())
                movedIt->second = idx;
        }

        // Отбрасываем освободившийся хвост за O(1).
        _sockets.pop_back();
        _slotStates.pop_back();
#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
        _pollFds.pop_back();
#else
        _handles.pop_back();
#endif
        _indexMap.erase(removedHandle);
    }

    // Проверяет, готов ли системный бэкенд к работе.
    bool SocketSelector::IsValid() const noexcept
    {
#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
        return true;
#else
        return _epollFd >= 0;
#endif
    }

    constexpr const char* SocketSelector::BackendName() noexcept
    {
#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
        return "WSAPoll";
#else
        return "epoll";
#endif
    }

    constexpr bool SocketSelector::SupportsEdgeTriggered() noexcept
    {
#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
        return false;
#else
        return true;
#endif
    }

    // Заранее резервирует память во всех внутренних структурах под большое число соединений.
    void SocketSelector::Reserve(size_t capacity)
    {
        _sockets.reserve(capacity);
        _slotStates.reserve(capacity);
        _indexMap.reserve(capacity);
#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
        _pollFds.reserve(capacity);
#else
        _handles.reserve(capacity);
#endif
    }

    // Изменяет максимальное число событий, извлекаемых за один вызов Poll().
    void SocketSelector::SetMaxEventsPerPoll(size_t maxEvents)
    {
        const size_t clamped = (maxEvents > 0) ? maxEvents : 1U;
#if LIBOTHER_PLATFORM != LIBOTHER_PLATFORM_WINDOWS
        _epollEvents.resize(clamped);
#endif
        _readyEvents.reserve(clamped);
        _maxEventsPerPoll = clamped;
    }

    // Возвращает текущий размер батча событий за один вызов Poll().
    size_t SocketSelector::MaxEventsPerPoll() const noexcept
    {
        return _maxEventsPerPoll;
    }

    // Добавляет сокет в селектор или обновляет маску его событий за O(1).
    bool SocketSelector::Add(const Socket& socket, bool watchRead, bool watchWrite, bool edgeTriggered)
    {
        // Проверяем валидность системного бэкенда и самого сокета.
        if (!IsValid() || socket.IsClosed())
        {
            SET_NET_ERROR(SOCK_EBADF);
            return false;
        }

        const socket_t handle = socket.SockHandle();
        auto it = _indexMap.find(handle);

        // Если сокет уже зарегистрирован — обновляем маску событий и актуальный указатель в _sockets.
        if (it != _indexMap.end())
        {
            const size_t idx = it->second;
#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
            LIBOTHER_UNUSED(edgeTriggered);
            _pollFds[idx].events = BuildPollEvents(watchRead, watchWrite);
#else
            epoll_event ev{};
            ev.events  = BuildEpollEvents(watchRead, watchWrite, edgeTriggered);
            ev.data.fd = handle;
            if (::epoll_ctl(_epollFd, EPOLL_CTL_MOD, handle, &ev) == SOCKET_ERROR)
            {
                SET_NET_ERROR(GET_NET_ERROR());
                return false;
            }
#endif
            _sockets[idx] = &socket;
            return true;
        }

        // Регистрируем новый слот во всех параллельных контейнерах со строгой гарантией исключений.
        const size_t newIndex = _sockets.size();

#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
        LIBOTHER_UNUSED(edgeTriggered);
        // Проверяем, что размер вектора не превышает лимит параметра ULONG для WSAPoll.
        if (_pollFds.size() >= static_cast<size_t>(std::numeric_limits<ULONG>::max()))
        {
            SET_NET_ERROR(SOCK_EINVAL);
            return false;
        }

        pollfd pfd{};
        pfd.fd      = handle;
        pfd.events  = BuildPollEvents(watchRead, watchWrite);
        pfd.revents = 0;

        _sockets.push_back(&socket);
        try
        {
            _slotStates.push_back(SlotState{});
            try
            {
                _pollFds.push_back(pfd);
                try
                {
                    _indexMap.emplace(handle, newIndex);
                }
                catch (...)
                {
                    _pollFds.pop_back();
                    throw;
                }
            }
            catch (...)
            {
                _slotStates.pop_back();
                throw;
            }
        }
        catch (...)
        {
            _sockets.pop_back();
            throw;
        }
        return true;
#else
        // Сначала расширяем пользовательские контейнеры (с откатом при bad_alloc), затем регистрируем в ядре.
        _sockets.push_back(&socket);
        try
        {
            _slotStates.push_back(SlotState{});
            try
            {
                _handles.push_back(handle);
                try
                {
                    _indexMap.emplace(handle, newIndex);
                }
                catch (...)
                {
                    _handles.pop_back();
                    throw;
                }
            }
            catch (...)
            {
                _slotStates.pop_back();
                throw;
            }
        }
        catch (...)
        {
            _sockets.pop_back();
            throw;
        }

        epoll_event ev{};
        ev.events  = BuildEpollEvents(watchRead, watchWrite, edgeTriggered);
        ev.data.fd = handle;

        if (::epoll_ctl(_epollFd, EPOLL_CTL_ADD, handle, &ev) == SOCKET_ERROR)
        {
            // Сохраняем ошибку ядра и откатываем пользовательские контейнеры.
            const int err = GET_NET_ERROR();
            _indexMap.erase(handle);
            _handles.pop_back();
            _slotStates.pop_back();
            _sockets.pop_back();
            SET_NET_ERROR(err);
            return false;
        }
        return true;
#endif
    }

    // Обновляет маску событий уже зарегистрированного сокета за O(1) без аллокаций.
    bool SocketSelector::Modify(const Socket& socket, bool watchRead, bool watchWrite, bool edgeTriggered) noexcept
    {
        if (!IsValid() || socket.IsClosed())
        {
            SET_NET_ERROR(SOCK_EBADF);
            return false;
        }

        const socket_t handle = socket.SockHandle();
        auto it = _indexMap.find(handle);
        if (it == _indexMap.end())
        {
            SET_NET_ERROR(SOCK_ENOENT);
            return false;
        }

        const size_t idx = it->second;
#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
        LIBOTHER_UNUSED(edgeTriggered);
        _pollFds[idx].events = BuildPollEvents(watchRead, watchWrite);
#else
        epoll_event ev{};
        ev.events  = BuildEpollEvents(watchRead, watchWrite, edgeTriggered);
        ev.data.fd = handle;
        if (::epoll_ctl(_epollFd, EPOLL_CTL_MOD, handle, &ev) == SOCKET_ERROR)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return false;
        }
#endif
        _sockets[idx] = &socket;
        return true;
    }

    // Удаляет сокет из селектора (за O(1) для открытого сокета; резервный поиск по _sockets для закрытого).
    bool SocketSelector::Remove(const Socket& socket) noexcept
    {
        if (!socket.IsClosed())
            return Remove(socket.SockHandle());

        // Если сокет был закрыт до вызова Remove(), ищем его по указателю в главном массиве _sockets.
        for (size_t i = 0; i < _sockets.size(); ++i)
        {
            if (_sockets[i] == &socket)
            {
                RemoveSlot(i);
                return true;
            }
        }
        return false;
    }

    // Удаляет сокет напрямую по системному дескриптору за O(1).
    bool SocketSelector::Remove(socket_t handle) noexcept
    {
        if (handle == INVALID_SOCKET)
            return false;

        auto it = _indexMap.find(handle);
        if (it == _indexMap.end())
            return false;

        RemoveSlot(it->second);
        return true;
    }

    // Полностью очищает набор отслеживаемых сокетов и буфер готовых событий.
    void SocketSelector::Clear() noexcept
    {
#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
        _pollFds.clear();
#else
        // Для большого числа сокетов быстрее пересоздать epoll-экземпляр за O(1), чем делать N вызовов epoll_ctl(DEL).
        if (!_handles.empty() && _epollFd >= 0)
        {
            int newFd = ::epoll_create1(EPOLL_CLOEXEC);
            if (newFd >= 0)
            {
                ::close(_epollFd);
                _epollFd = newFd;
            }
            else
            {
                // Резервный путь при нехватке дескрипторов (EMFILE): удаляем каждый fd из текущего epoll.
                for (socket_t h : _handles)
                    ::epoll_ctl(_epollFd, EPOLL_CTL_DEL, h, nullptr);
            }
        }
        _handles.clear();
#endif
        _sockets.clear();
        _slotStates.clear();
        _indexMap.clear();
        _readyEvents.clear();
    }

    // Проверяет за O(1), находится ли сокет под наблюдением селектора.
    bool SocketSelector::Contains(const Socket& socket) const noexcept
    {
        if (socket.IsClosed())
            return false;
        return Contains(socket.SockHandle());
    }

    // Проверяет за O(1), находится ли системный дескриптор под наблюдением селектора.
    bool SocketSelector::Contains(socket_t handle) const noexcept
    {
        if (handle == INVALID_SOCKET)
            return false;
        return _indexMap.find(handle) != _indexMap.end();
    }

    // Выполняет опрос событий (epoll_wait на Linux за O(K) / WSAPoll на Windows) и заполняет ReadyEvents().
    int SocketSelector::Poll(int timeoutMs)
    {
        // Очищаем результаты предыдущего опроса и сдвигаем эпоху.
        _readyEvents.clear();

        if (!IsValid())
        {
            SET_NET_ERROR(SOCK_EBADF);
            return -1;
        }

        if (_sockets.empty())
            return 0;

#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
        // В WSAPoll обнуляем revents перед очередным опросом.
        for (auto& pfd : _pollFds)
            pfd.revents = 0;

        int result = POLL_FUNC(_pollFds.data(), static_cast<ULONG>(_pollFds.size()), timeoutMs);
        if (result == SOCKET_ERROR || result < 0)
        {
            SET_NET_ERROR(GET_NET_ERROR());
            return -1;
        }

        AdvanceEpoch();

        if (result == 0)
            return 0;

        // Извлекаем только сработавшие сокеты (с ранним выходом, как только собрали все result элементов).
        const size_t expected = static_cast<size_t>(result);
        const size_t limit    = (expected < _maxEventsPerPoll) ? expected : _maxEventsPerPoll;
        if (_readyEvents.capacity() < limit)
            _readyEvents.reserve(limit);

        size_t collected = 0;
        for (size_t i = 0; i < _pollFds.size() && collected < limit; ++i)
        {
            if (_pollFds[i].revents == 0)
                continue;

            const uint32_t mask = TranslatePollRevents(_pollFds[i].revents);
            if (mask == SelectorEvent::None)
                continue;

            // Обновляем метку эпохи и маску слота для O(1)-методов IsReadable/IsWritable/HasError.
            _slotStates[i].lastEpoch  = _pollEpoch;
            _slotStates[i].lastEvents = mask;

            _readyEvents.push_back(SelectorEvent{ _sockets[i], _pollFds[i].fd, mask });
            ++collected;
        }

        return static_cast<int>(_readyEvents.size());
#else
        // На Linux ограничиваем число запрашиваемых событий размером батча (и диапазоном int).
        const int maxEvents = static_cast<int>(
            (_epollEvents.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
                ? static_cast<size_t>(std::numeric_limits<int>::max())
                : _epollEvents.size());

        using Clock = std::chrono::steady_clock;
        const auto deadline = (timeoutMs > 0)
            ? (Clock::now() + std::chrono::milliseconds(timeoutMs))
            : Clock::time_point{};

        int remainingMs = timeoutMs;
        int readyCount  = 0;

        // Цикл ожидания с корректным перезапуском при прерывании сигналом ОС (EINTR).
        while (true)
        {
            readyCount = ::epoll_wait(_epollFd, _epollEvents.data(), maxEvents, remainingMs);
            if (readyCount >= 0)
                break;

            if (GET_NET_ERROR() != SOCK_EINTR)
                return -1;

            // Для конечного таймаута пересчитываем остаток времени до дедлайна.
            if (timeoutMs > 0)
            {
                const auto now = Clock::now();
                if (now >= deadline)
                {
                    readyCount = 0;
                    break;
                }
                remainingMs = static_cast<int>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
            }
        }

        // Увеличиваем эпоху за O(1): все старые результаты автоматически считаются устаревшими.
        AdvanceEpoch();

        if (readyCount == 0)
            return 0;

        // Обрабатываем строго K активных событий (сложность O(K), не зависит от миллиона открытых сокетов).
        for (int i = 0; i < readyCount; ++i)
        {
            const socket_t handle = _epollEvents[static_cast<size_t>(i)].data.fd;
            const uint32_t mask   = TranslateEpollEvents(_epollEvents[static_cast<size_t>(i)].events);
            if (mask == SelectorEvent::None)
                continue;

            auto it = _indexMap.find(handle);
            if (it == _indexMap.end())
                continue;

            const size_t idx = it->second;
            _slotStates[idx].lastEpoch  = _pollEpoch;
            _slotStates[idx].lastEvents = mask;

            _readyEvents.push_back(SelectorEvent{ _sockets[idx], handle, mask });
        }

        return static_cast<int>(_readyEvents.size());
#endif
    }

    // Возвращает список только тех сокетов, на которых сработали события в последнем Poll().
    const std::vector<SelectorEvent>& SocketSelector::ReadyEvents() const noexcept
    {
        return _readyEvents;
    }

    // Возвращает главный массив всех отслеживаемых сокетов.
    const std::vector<const Socket*>& SocketSelector::Sockets() const noexcept
    {
        return _sockets;
    }

    // Возвращает битовую маску событий для объекта Socket за O(1).
    uint32_t SocketSelector::GetEvents(const Socket& socket) const noexcept
    {
        if (socket.IsClosed())
            return SelectorEvent::None;
        return GetEvents(socket.SockHandle());
    }

    // Возвращает битовую маску событий для системного дескриптора за O(1).
    uint32_t SocketSelector::GetEvents(socket_t handle) const noexcept
    {
        if (handle == INVALID_SOCKET || _pollEpoch == 0)
            return SelectorEvent::None;

        auto it = _indexMap.find(handle);
        if (it == _indexMap.end())
            return SelectorEvent::None;

        const SlotState& state = _slotStates[it->second];
        return (state.lastEpoch == _pollEpoch) ? state.lastEvents : SelectorEvent::None;
    }

    // Проверяет за O(1), готов ли сокет к чтению (данные, закрытие HangUp или ошибка Error).
    bool SocketSelector::IsReadable(const Socket& socket) const noexcept
    {
        return (GetEvents(socket) & (SelectorEvent::Read | SelectorEvent::HangUp | SelectorEvent::Error)) != 0U;
    }

    // Проверяет за O(1), готов ли сокет к записи (Write или Error).
    bool SocketSelector::IsWritable(const Socket& socket) const noexcept
    {
        return (GetEvents(socket) & (SelectorEvent::Write | SelectorEvent::Error)) != 0U;
    }

    // Проверяет за O(1), произошла ли ошибка или разрыв соединения на сокете (Error | HangUp | Invalid).
    bool SocketSelector::HasError(const Socket& socket) const noexcept
    {
        return (GetEvents(socket) & (SelectorEvent::Error | SelectorEvent::HangUp | SelectorEvent::Invalid)) != 0U;
    }

    // Проверяет за O(1) наличие критической ошибки (Error | Invalid), без штатного закрытия HangUp.
    bool SocketSelector::HasCriticalError(const Socket& socket) const noexcept
    {
        return (GetEvents(socket) & (SelectorEvent::Error | SelectorEvent::Invalid)) != 0U;
    }

    // Проверяет за O(1), получено ли уведомление о закрытии соединения (HangUp).
    bool SocketSelector::IsHangUp(const Socket& socket) const noexcept
    {
        return (GetEvents(socket) & SelectorEvent::HangUp) != 0U;
    }

    // Возвращает текущее количество отслеживаемых сокетов.
    size_t SocketSelector::Size() const noexcept
    {
        return _sockets.size();
    }

    // Проверяет, пуст ли селектор.
    bool SocketSelector::Empty() const noexcept
    {
        return _sockets.empty();
    }
}
