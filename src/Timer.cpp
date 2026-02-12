#include "Timer.h"
#include "private.hpp"


namespace other::time::timer
{
   
    Timer::Timer(uint64_t timerId) 
        : _timerId(timerId) // Инициализация счетчика
    {
        /**
         * Контейнер _timerList (std::map) инициализируется автоматически
         * как пустой список. На данном этапе память под таймеры еще не выделена.
         */
    }
    
	// Создает новый таймер и регистрирует его в системе.
    uint64_t Timer::CreateTimer(const DateTime & startTime, const DateTime & endTime, std::shared_ptr<ICallable> callable, const std::string & title)
    {
        // Увеличиваем ID на 1
        _timerId += 1;

        // Получаем ссылку на элемент в map.
        TimerInfo& timer = _timerList[_timerId];

        // Заполняем поля.
        timer._startTime = startTime; // Дата, начиная с которой таймер "активен"
        timer._endTime   = endTime;   // Дата первого срабатывания
        timer._callable  = callable;
        timer._title     = title;

        // Возвращаем ID созданого таймера.
        return _timerId;
    }
	
	// Активирует таймер по его ID.
	void Timer::StartTimer(uint64_t id)
    {
        // Экономит одно лишнее обращение к дереву map.
        auto it = _timerList.find(id);

        // Если таймер не найден то выходим.
        if (it == _timerList.end())
            return;

        // Получаем ссылку на элемент в map.
        TimerInfo& timer = it->second;

        timer._paused = false;
    }
	
	// Перезапускает существующий таймер с новыми параметрами времени.
	void Timer::RestartTimer(uint64_t id, const DateTime & startTime, const DateTime & endTime)
    {
        // Экономит одно лишнее обращение к дереву map.
        auto it = _timerList.find(id);

        // Если таймер не найден то выходим.
        if (it == _timerList.end())
            return;

        // Получаем ссылку на элемент в map.
        TimerInfo& timer = it->second;

        // Обновляем временные параметры
        timer._startTime = startTime;
        timer._endTime   = endTime;

        // Сбрасываем состояние
        timer._called = false;
        timer._finish = false;

        // При рестарте обычно логично оставить таймер на паузе, 
        // пока не вызовут StartTimer, но если нужно запускать сразу — можно поставить в false. Оставим true для безопасности.
        timer._paused = false;
    }
	
	// Логическая остановка таймера.
    void Timer::StopTimer(uint64_t id)
    {
        // Экономит одно лишнее обращение к дереву map.
        auto it = _timerList.find(id);

        // Если таймер не найден то выходим.
        if (it == _timerList.end())
            return;

        // Получаем ссылку на элемент в map.
        TimerInfo& timer = it->second;

        // Помечаем как неактивный (для будущего удаления)
        timer._finish = true;
    }
	
	// Приостанавливает работу таймера.
    void Timer::PauseTimer(uint64_t id)
    {
        // Экономит одно лишнее обращение к дереву map.
        auto it = _timerList.find(id);

        // Если таймер не найден то выходим.
        if (it == _timerList.end())
            return;

        // Получаем ссылку на элемент в map.
        TimerInfo& timer = it->second;

        // Временно останавливаем проверку таймер
        timer._paused = true;
    }
	
	// Возобновляет работу ранее приостановленного таймера.
    void Timer::ResumeTimer(uint64_t id)
    {
        // Экономит одно лишнее обращение к дереву map.
        auto it = _timerList.find(id);

        // Если таймер не найден то выходим.
        if (it == _timerList.end())
            return;

        // Получаем ссылку на элемент в map.
        TimerInfo& timer = it->second;

        // Продолжаем отсчет
        timer._paused = false;
    }
	
    // Главный аналитический метод.
    void Timer::CheckTimeout(uint64_t id)
    {
        // Экономит одно лишнее обращение к дереву map.
        auto it = _timerList.find(id);

        // Если таймер не найден то выходим.
        if (it == _timerList.end())
            return;

        // Получаем ссылку на элемент в map.
        TimerInfo& timer = it->second;

        if (timer._finish || timer._paused)
            return;

        // Определяем текущее время (Локальное или UTC)
        DateTime now = (timer._endTime.Kind() == DateTimeKind::Local) ? DateTime::Now() : DateTime::UtcNow();

        // Если еще не наступило время старта — выходим.
        if (now < timer._startTime)
            return;

        // Если время срабатывания наступило.
        if (now >= timer._endTime)
        {
            // Вызываем колбэк (один раз)
            if (timer._callable && !timer._called)
            {
                try
                {
                    timer._callable->callAfterTimeout();
                    timer._called = true;
                }
                catch (...)
                {
                    std::cerr << "Error in timer: " << timer._title << std::endl;
                }
            }
        }
    }
	
	// Возвращает время, прошедшее с момента старта таймера.
    TimeSpan Timer::ElapsedTime(uint64_t id) const
    {
        // Экономит одно лишнее обращение к дереву map.
        const auto it = _timerList.find(id);

        // Если таймер не найден то выходим.
        if (it == _timerList.end())
            return TimeSpan::Zero;

        // Получаем ссылку на элемент в map.
        const TimerInfo& timer = it->second;

        // Определяем текущее время (Локальное или UTC)
        DateTime now = (timer._endTime.Kind() == DateTimeKind::Local) ? DateTime::Now() : DateTime::UtcNow();

        if (now < timer._startTime)
            return TimeSpan::Zero;

        return TimeSpan(now.Ticks() - timer._startTime.Ticks());
    }
	
	// Возвращает время, оставшееся до срабатывания таймера.
    TimeSpan Timer::RemainingTime(uint64_t id) const
    {
        // Экономит одно лишнее обращение к дереву map.
        const auto it = _timerList.find(id);

        // Если таймер не найден то выходим.
        if (it == _timerList.end())
            return TimeSpan::Zero;

        // Получаем ссылку на элемент в map.
        const TimerInfo& timer = it->second;

        // Определяем текущее время (Локальное или UTC)
        DateTime now = (timer._endTime.Kind() == DateTimeKind::Local) ? DateTime::Now() : DateTime::UtcNow();

        // Если сейчас еще не наступило время старта
        if (now < timer._startTime)
        {
            // Осталось = время до старта + время от старта до конца
            return TimeSpan(timer._endTime.Ticks() - now.Ticks());
        }

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
                it++;
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
	uint64_t Timer::FindTimerIdFromTitle(const std::string & title) const
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
