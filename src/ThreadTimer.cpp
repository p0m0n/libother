#include "ThreadTimer.h"

namespace other::time::timer
{
    ThreadTimer* ThreadTimer::Instance()
    {
        static ThreadTimer _instance;
        return &_instance;
    }

    ThreadTimer::ThreadTimer(std::shared_ptr<Timer> timer)
        : _timer(timer ? std::move(timer) : std::make_shared<Timer>())
        , _running(false)
        , _wakeupCounter(0)
    {
    }

    ThreadTimer::~ThreadTimer()
    {
        Stop();
    }

    bool ThreadTimer::IsRunning() const
    {
        return _running.load();
    }

    void ThreadTimer::Start(int sleepMilliseconds)
    {
        const int sleepMs = (sleepMilliseconds > 0) ? sleepMilliseconds : 1;

        std::lock_guard<std::mutex> startLock(_mutex);
        if (_running)
            return;

        if (_thread.joinable())
            _thread.join();

        _running = true;

        _thread = std::thread([this, sleepMs]()
            {
                std::unique_lock<std::mutex> lock(_mutex);
                while (_running)
                {
                    // ФАЗА 1: Ожидание задач, если список пуст.
                    if (_timer->TimerList().empty())
                    {
                        _cond_v.wait(lock, [this]
                            {
                                return !_running || !_timer->TimerList().empty();
                            });
                    }

                    if (!_running)
                        break;

                    // ФАЗА 2: Проверяем таймауты и собираем сработавшие колбэки ПОД мьютексом.
                    std::vector<std::shared_ptr<ICallable>> dueCallbacks;
                    for (const auto& [id, _] : _timer->TimerList())
                    {
                        if (auto cb = _timer->PollDueCallback(id))
                        {
                            dueCallbacks.push_back(std::move(cb));
                        }
                    }

                    const uint64_t wakeupSnapshot = _wakeupCounter;

                    // ФАЗА 3: Вызываем колбэки БЕЗ мьютекса, перехватывая исключения,
                    // чтобы фоновый поток не завершился через std::terminate().
                    lock.unlock();

                    for (const auto& cb : dueCallbacks)
                    {
                        try
                        {
                            cb->callAfterTimeout();
                        }
                        catch (...)
                        {
#if LIBOTHER_DEBUG
                            std::cerr << "ERROR: unhandled exception in timer callback." << std::endl;
#endif
                        }
                    }

                    // ФАЗА 4: Ожидание следующего цикла с немедленным пробуждением при изменениях.
                    lock.lock();
                    if (!_running)
                        break;

                    _cond_v.wait_for(lock, std::chrono::milliseconds(sleepMs), [this, wakeupSnapshot]
                        {
                            return !_running || _wakeupCounter != wakeupSnapshot;
                        });
                }
            });
    }

    void ThreadTimer::Stop()
    {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (!_running)
                return;

            _running = false;
            ++_wakeupCounter;
        }

        _cond_v.notify_all();

        // Защита от самоблокировки (deadlock), если Stop() вызван изнутри колбэка.
        if (_thread.joinable() && _thread.get_id() != std::this_thread::get_id())
        {
            _thread.join();
        }
    }

    uint64_t ThreadTimer::CreateTimer(const DateTime& startTime, const DateTime& endTime, std::shared_ptr<ICallable> callable, const std::string& title)
    {
        uint64_t newId;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            newId = _timer->CreateTimer(startTime, endTime, std::move(callable), title);
            ++_wakeupCounter;
        }

        _cond_v.notify_all();
        return newId;
    }

    void ThreadTimer::StartTimer(uint64_t id)
    {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _timer->StartTimer(id);
            ++_wakeupCounter;
        }
        _cond_v.notify_all();
    }

    void ThreadTimer::StopTimer(uint64_t id)
    {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _timer->StopTimer(id);
            ++_wakeupCounter;
        }
        _cond_v.notify_all();
    }

    void ThreadTimer::PauseTimer(uint64_t id)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _timer->PauseTimer(id);
    }

    void ThreadTimer::ResumeTimer(uint64_t id)
    {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _timer->ResumeTimer(id);
            ++_wakeupCounter;
        }
        _cond_v.notify_all();
    }

    void ThreadTimer::RestartTimer(uint64_t id, const DateTime& startTime, const DateTime& endTime)
    {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _timer->RestartTimer(id, startTime, endTime);
            ++_wakeupCounter;
        }
        _cond_v.notify_all();
    }

    TimeSpan ThreadTimer::ElapsedTime(uint64_t id) const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _timer->ElapsedTime(id);
    }

    TimeSpan ThreadTimer::RemainingTime(uint64_t id) const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _timer->RemainingTime(id);
    }

    uint64_t ThreadTimer::FindTimerIdFromTitle(const std::string& title) const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _timer->FindTimerIdFromTitle(title);
    }

    uint64_t ThreadTimer::RemoveStopTimers()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _timer->RemoveStopTimers();
    }

    uint64_t ThreadTimer::TotalTimersCreated() const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _timer->TotalTimersCreated();
    }
}
