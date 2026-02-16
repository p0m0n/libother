#include "ThreadTimer.h"

namespace other::time::timer
{
    ThreadTimer* ThreadTimer::Instance()
    {
        static ThreadTimer _instance;
        return &_instance;
    }

    ThreadTimer::ThreadTimer(std::shared_ptr<Timer> timer)
        : _timer(timer),      // Сохраняем указатель на Timer
        _running(false)       // Изначально поток выключен (атомарная инициализация)
    {
        /**
         * Конструктор подготавливает инфраструктуру:
         * 1. Устанавливает связь с хранилищем таймеров.
         * 2. Гарантирует, что флаг управления потоком (_running) находится в безопасном состоянии false.
         * 3. Мьютекс и condition_variable инициализируются автоматически.
         */
    }

    ThreadTimer::~ThreadTimer()
    {
        /**
         * Вызов Stop() выполняет три важные вещи:
         * 1. Устанавливает _running = false.
         * 2. Вызывает _cond_v.notify_all(), чтобы поток мгновенно вышел из ожидания (wait).
         * 3. Выполняет _thread.join(), чтобы дождаться полного завершения функции потока.
         * * Это предотвращает вызов std::terminate, который случается, если объект std::thread
         * уничтожается, находясь в активном состоянии.
         */
        Stop();
    }

    bool ThreadTimer::IsRunning() const
    {
        /**
         * Возвращает значение атомарного флага _running.
         * Поскольку переменная имеет тип std::atomic, чтение безопасно
         * проводить из любого потока без использования мьютекса.
         * Это делает вызов максимально быстрым и дешевым.
         */
        return _running.load();
    }

    void ThreadTimer::Start(int sleepMilliseconds)
    {
        // Защита от повторного запуска: один объект — один поток.
        if (_running)
            return;

        // Устанавливаем флаг работы в true
        _running = true;

        // Запуск фонового потока (Worker Thread)
        _thread = std::thread([this, sleepMilliseconds]()
            {
                while (_running)
                {
					// Используем unique_lock, так как нам нужно гибко освобождать и захватывать мьютекс
                    std::unique_lock<std::mutex> lock(_mutex);

                    /**
                     * ФАЗА 1: Ожидание задач.
                     * Если список пуст, поток не "молотит" процессор впустую, а засыпает до тех пор,
                     * пока через CreateTimer не придет уведомление (notify_all) или пока не вызовут Stop().
                     */
                    if (_timer->TimerList().empty())
                    {
                        _cond_v.wait(lock, [this]
                            {
                                return !_running || !_timer->TimerList().empty();
                            });
                    }

                    // Проверка: проснулись ли для работы или для завершения?
                    if (!_running)
                        break;

                    /**
                     * ФАЗА 2: Подготовка к обработке.
                     * Копируем ID всех таймеров в локальный вектор. 
                     * Это позволяет нам быстро "сфотографировать" состояние списка под защитой мьютекса,
                     * а затем работать с ним, не блокируя другие методы (например, CreateTimer).
                     */
                    std::vector<uint64_t> ids;
                    for (const auto& [id, _] : _timer->TimerList())
                    {
                        ids.push_back(id);
                    }

                    /**
                     * ФАЗА 3: Выполнение колбэков.
                     * ВАЖНО: Мы отпускаем мьютекс! Если колбэк внутри CheckTimeout будет долгим
                     * или попытается вызвать методы этого же класса (Deadlock Prevention), 
                     * программа не зависнет.
                     */
                    lock.unlock();

                    for (uint64_t id : ids)
                    {
                        _timer->CheckTimeout(id);
                    }

                    /**
                     * ФАЗА 4: Ожидание следующего цикла.
                     * Снова захватываем мьютекс для безопасного использования condition_variable.
                     */
                    lock.lock();

                    /**
                     * Спим заданное время (например, 100мс), чтобы не перегружать CPU.
                     * Но если во время сна флаг _running станет false (вызвали Stop), 
                     * поток проснется мгновенно, не дожидаясь конца тайм-аута.
                     */
                    _cond_v.wait_for(lock, std::chrono::milliseconds(sleepMilliseconds), [this]
                        {
                            return !_running;
                        });
                }
            });
    }

    void ThreadTimer::Stop()
    {
        {
            // Захватываем мьютекс, чтобы изменение флага было атомарным
            // по отношению к проверкам внутри цикла Start().
            std::lock_guard<std::mutex> lock(_mutex);

            // Чтобы не останавливать уже остановленный поток
            if (!_running)
                return;

            _running = false;
        }

        /**
         * ВАЖНО: Уведомляем поток.
         * Если поток в данный момент спит в _cond_v.wait_for(),
         * этот вызов заставит его немедленно проснуться, проверить флаг _running
         * и выйти из бесконечного цикла.
         */
        _cond_v.notify_all();

        /**
         * Блокируем текущий (вызывающий) поток до тех пор, пока фоновый поток
         * не завершит свою работу. Это гарантирует, что после выхода из Stop()
         * никаких фоновых операций с таймерами больше не происходит.
         */
        if (_thread.joinable())
        {
            _thread.join();
        }
    }

    uint64_t ThreadTimer::CreateTimer(const DateTime& startTime, const DateTime& endTime, std::shared_ptr<ICallable> callable, const std::string& title)
    {
        uint64_t newId;
        {
			// Захватываем мьютекс для безопасного создания таймера.
            std::lock_guard<std::mutex> lock(_mutex);

			// Создаём новый таймер.
            newId = _timer->CreateTimer(startTime, endTime, callable, title);
        }

        // Будим поток! Если он спал в wait(), он тут же проснется и начнет проверку нового таймера.
        _cond_v.notify_one();
        return newId;
    }

    void ThreadTimer::StartTimer(uint64_t id)
    {
		// Захватываем мьютекс для безопасного изменения состояния.
        std::lock_guard<std::mutex> lock(_mutex);
		
		// Обращаемся к ядру для сброса флага паузы
        _timer->StartTimer(id);
		
		/**
         * Генерируем сигнал для фонового потока.
         * Если поток находился в состоянии ожидания (wait) внутри метода Start(),
         * так как список задач был пуст или все таймеры были на паузе,
         * этот вызов заставит его немедленно проснуться и перейти к проверке.
         */
        _cond_v.notify_all();
    }

    void ThreadTimer::StopTimer(uint64_t id)
    {
		// Захватываем мьютекс для безопасного изменения состояния.
        std::lock_guard<std::mutex> lock(_mutex);

		/**
         * Вызываем метод ядра, который установит:
         * timer._finish = true;
         * Это гарантирует, что в следующем цикле CheckTimeout проигнорирует этот ID.
         */
        _timer->StopTimer(id);
		
		// Уведомляем поток на случай, если он ждал изменений в списке.
        _cond_v.notify_all();
    }

    void ThreadTimer::PauseTimer(uint64_t id)
    {
		// Захватываем мьютекс для безопасного изменения состояния.
        std::lock_guard<std::mutex> lock(_mutex);

		/**
         * Вызываем метод ядра, который установит:
         * timer._paused = true;
         */
        _timer->PauseTimer(id);
		
		/**
         * ПРИМЕЧАНИЕ: Здесь notify_all() обычно не обязателен, 
         * так как пауза не требует немедленного пробуждения потока для работы.
         * Поток просто пропустит этот таймер при следующей плановой проверке.
         */
    }

    void ThreadTimer::ResumeTimer(uint64_t id)
    {
		// Захватываем мьютекс для безопасного изменения состояния.
        std::lock_guard<std::mutex> lock(_mutex);
		
		// Сбрасываем флаг паузы в ядре
        _timer->ResumeTimer(id);

		/**
         * ВАЖНО: Вызываем notify_all(). 
         * Если поток "спал" долгим сном (например, sleepMilliseconds = 500), 
         * мы не хотим ждать конца сна. Мы будим поток немедленно, чтобы он 
         * проверил: не пора ли выполнить этот таймер сразу после снятия с паузы.
         */
        _cond_v.notify_all();
    }

    void ThreadTimer::RestartTimer(uint64_t id, const DateTime& startTime, const DateTime& endTime)
    {
		// Захватываем мьютекс для безопасного изменения состояния.
        std::lock_guard<std::mutex> lock(_mutex);

		/**
         * Вызываем метод ядра, который:
         * 1. Находит таймер по ID.
         * 2. Обновляет временные метки.
         * 3. Устанавливает _paused = false и _finish = false.
         * 4. Сбрасывает _called = false, чтобы позволить новое срабатывание.
         */
        _timer->RestartTimer(id, startTime, endTime);
		
		/**
         * ВАЖНО: notify_all() здесь обязателен.
         * Поскольку мы могли установить endTime на "прямо сейчас" или на очень близкое время,
         * нам нужно, чтобы фоновый поток немедленно пересчитал свой цикл ожидания 
         * и обработал обновленный таймер без задержек.
         */
        _cond_v.notify_all();
    }

    TimeSpan ThreadTimer::ElapsedTime(uint64_t id) const
    {
		/**
         * Захватываем мьютекс для безопасного чтения данных из родительского класса.
         * Мы используем lock_guard, так как операция чтения быстрая.
         * Несмотря на то, что метод const, мьютекс помечен как mutable, 
         * что позволяет блокировать его здесь.
         */
        std::lock_guard<std::mutex> lock(_mutex);

		/**
         * Вся математика расчета находится внутри ядра Timer.
         * Метод вернет разницу (DateTime::Now - _startTime).
         */
        return _timer->ElapsedTime(id);
    }

    TimeSpan ThreadTimer::RemainingTime(uint64_t id) const
    {
		// Блокируем мьютекс (mutable), так как состояние endTime может быть 
        // изменено фоновым потоком в момент срабатывания циклического таймера.
        std::lock_guard<std::mutex> lock(_mutex);

		/**
         * Вызывает метод ядра Timer, который сравнивает текущее время с timer._endTime.
         * Если Now >= endTime, метод вернет TimeSpan(0), сигнализируя, что ждать больше нечего.
         */
        return _timer->RemainingTime(id);
    }

    uint64_t ThreadTimer::FindTimerIdFromTitle(const std::string& title) const
    {
		// Захватываем мьютекс, так как итерация по std::map во время её 
        // возможной модификации другим потоком приведет к неопределенному поведению (UB).
        std::lock_guard<std::mutex> lock(_mutex);
		
		/**
         * Вызывает метод ядра Timer, который проходит циклом по всем элементам _timerList.
         * Линейная сложность поиска O(n).
         */
        return _timer->FindTimerIdFromTitle(title);
    }

    uint64_t ThreadTimer::RemoveStopTimers()
    {
		/**
         * Захватываем мьютекс, так как удаление элементов из std::map (изменение структуры контейнера)
         * категорически нельзя проводить одновременно с итерацией по этому же контейнеру
         * в фоновом потоке (метод Start).
         */
        std::lock_guard<std::mutex> lock(_mutex);
		
		/**
         * Вызывает метод ядра Timer, который:
         * 1. Находит все элементы с _finish == true.
         * 2. Вызывает erase() для каждого такого элемента.
         * 3. Возвращает число удаленных записей.
         */
        return _timer->RemoveStopTimers();
    }

    uint64_t ThreadTimer::TotalTimersCreated() const
    {
		// Захватываем мьютекс (mutable), так как мы обращаемся к размеру контейнера _timerList,
        // который может быть изменен другими потоками (через CreateTimer или RemoveStopTimers).
        std::lock_guard<std::mutex> lock(_mutex);
		
		/**
         * Возвращает размер внутреннего контейнера std::map.
         * Сложность операции O(1).
         */
        return _timer->TotalTimersCreated();
    }
}
