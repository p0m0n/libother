#include "Timer.h"


namespace other::time::timer
{
    Timer* Timer::Instance()
    {
        static Timer _instance;
        return &_instance;
    }

    Timer::Timer(uint64_t timerId)
        : _timerId(timerId)
    {
    }

    // Создает новый таймер и регистрирует его в системе.
    uint64_t Timer::CreateTimer(const DateTime& startTime, const DateTime& endTime, std::shared_ptr<ICallable> callable, const std::string& title)
    {
        _timerId += 1;

        TimerInfo& timer = _timerList[_timerId];
        timer._startTime = startTime;
        timer._endTime   = endTime;
        timer._callable  = std::move(callable);
        timer._title     = title;

        return _timerId;
    }

    // Активирует таймер по его ID.
    void Timer::StartTimer(uint64_t id)
    {
        auto it = _timerList.find(id);
        if (it == _timerList.end())
            return;

        TimerInfo& timer = it->second;
        if (timer._finish)
            return;

        timer._paused = false;
    }

    // Перезапускает существующий таймер с новыми параметрами времени.
    void Timer::RestartTimer(uint64_t id, const DateTime& startTime, const DateTime& endTime)
    {
        auto it = _timerList.find(id);
        if (it == _timerList.end())
            return;

        TimerInfo& timer = it->second;
        timer._startTime = startTime;
        timer._endTime   = endTime;

        timer._called = false;
        timer._finish = false;
        timer._paused = false;
    }

    // Логическая остановка таймера.
    void Timer::StopTimer(uint64_t id)
    {
        auto it = _timerList.find(id);
        if (it == _timerList.end())
            return;

        it->second._finish = true;
    }

    // Приостанавливает работу таймера.
    void Timer::PauseTimer(uint64_t id)
    {
        auto it = _timerList.find(id);
        if (it == _timerList.end())
            return;

        it->second._paused = true;
    }

    // Возобновляет работу ранее приостановленного таймера.
    void Timer::ResumeTimer(uint64_t id)
    {
        auto it = _timerList.find(id);
        if (it == _timerList.end())
            return;

        TimerInfo& timer = it->second;
        if (timer._finish)
            return;

        timer._paused = false;
    }

    // Проверяет таймаут, обновляет флаги и возвращает колбэк для безопасного вызова вне мьютекса.
    std::shared_ptr<ICallable> Timer::PollDueCallback(uint64_t id)
    {
        auto it = _timerList.find(id);
        if (it == _timerList.end())
            return nullptr;

        TimerInfo& timer = it->second;
        if (timer._finish || timer._paused)
            return nullptr;

        DateTime now = (timer._endTime.Kind() == DateTimeKind::Local) ? DateTime::Now() : DateTime::UtcNow();

        if (now < timer._startTime)
            return nullptr;

        if (now >= timer._endTime)
        {
            // Помечаем таймер завершенным до вызова колбэка (и даже если колбэк == nullptr).
            timer._finish = true;

            if (timer._callable && !timer._called)
            {
                timer._called = true;
                return timer._callable;
            }
        }

        return nullptr;
    }

    // Главный аналитический метод.
    void Timer::CheckTimeout(uint64_t id)
    {
        std::shared_ptr<ICallable> callable = PollDueCallback(id);
        if (!callable)
            return;

        try
        {
            callable->callAfterTimeout();
        }
        catch (...)
        {
#if LIBOTHER_DEBUG
            auto it = _timerList.find(id);
            std::cout << "ERROR: in timer: " << (it != _timerList.end() ? it->second._title : "unknown") << std::endl;
#endif
            throw;
        }
    }

    // Возвращает время, прошедшее с момента старта таймера.
    TimeSpan Timer::ElapsedTime(uint64_t id) const
    {
        const auto it = _timerList.find(id);
        if (it == _timerList.end())
            return TimeSpan::Zero;

        const TimerInfo& timer = it->second;
        DateTime now = (timer._endTime.Kind() == DateTimeKind::Local) ? DateTime::Now() : DateTime::UtcNow();

        if (now < timer._startTime)
            return TimeSpan::Zero;

        return TimeSpan(now.Ticks() - timer._startTime.Ticks());
    }

    // Возвращает время, оставшееся до срабатывания таймера.
    TimeSpan Timer::RemainingTime(uint64_t id) const
    {
        const auto it = _timerList.find(id);
        if (it == _timerList.end())
            return TimeSpan::Zero;

        const TimerInfo& timer = it->second;
        if (timer._finish || timer._paused)
            return TimeSpan::Zero;

        DateTime now = (timer._endTime.Kind() == DateTimeKind::Local) ? DateTime::Now() : DateTime::UtcNow();

        if (now >= timer._endTime)
            return TimeSpan::Zero;

        return TimeSpan(timer._endTime.Ticks() - now.Ticks());
    }

    // Очищает map от всех завершенных таймеров.
    uint64_t Timer::RemoveStopTimers()
    {
        uint64_t result = 0;
        for (auto it = _timerList.begin(); it != _timerList.end();)
        {
            if (it->second._finish)
            {
                it = _timerList.erase(it);
                result++;
            }
            else
            {
                ++it;
            }
        }
        return result;
    }

    // Возвращает общее количество созданных таймеров.
    uint64_t Timer::TotalTimersCreated() const
    {
        return _timerId;
    }

    // Ищет идентификатор таймера по его текстовому названию.
    uint64_t Timer::FindTimerIdFromTitle(const std::string& title) const
    {
        for (const auto& [id, info] : _timerList)
        {
            if (info._title == title)
                return id;
        }
        return 0;
    }

    // Предоставляет доступ к внутреннему хранилищу таймеров.
    const Timer::timer_m& Timer::TimerList() const
    {
        return _timerList;
    }
}
