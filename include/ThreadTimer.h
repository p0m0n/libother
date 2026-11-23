#ifndef LIBOTHER_THREAD_TIMER_H
#define LIBOTHER_THREAD_TIMER_H

#include "Define.h"
#include "Timer.h"


namespace other::time::timer
{
    /**
     * @class ThreadTimer
     * @brief Потокобезопасный контроллер для управления таймерами.
     * Обеспечивает выполнение таймеров в отдельном фоновом потоке, защиту данных
     * через мьютексы и эффективное ожидание через condition_variable.
     */
    class ThreadTimer
    {
    private:
        mutable std::mutex      _mutex;          // Мьютекс для синхронизации доступа к данным класса Timer
        std::shared_ptr<Timer>  _timer;          // Указатель на ядро логики таймеров (всегда не null)
        std::condition_variable _cond_v;         // Используется для эффективного управления паузой и пробуждением потока
        std::thread             _thread;         // Фоновый рабочий поток
        std::atomic<bool>       _running;        // Атомарный флаг состояния жизненного цикла потока
        uint64_t                _wakeupCounter;  // Счетчик событий для немедленного пробуждения wait_for

    public:
        static ThreadTimer* Instance();

        /**
         * @brief Конструктор контроллера ThreadTimer.
         * Инициализирует контроллер, связывая его с объектом Timer.
         * Если передан nullptr, автоматически создается новый экземпляр Timer.
         * 
         * @param [in] timer: Общий указатель (shared_ptr) на экземпляр класса Timer.
         */
        explicit ThreadTimer(std::shared_ptr<Timer> timer = nullptr);
        /**
         * @brief Деструктор класса ThreadTimer.
         * Обеспечивает безопасное завершение фонового потока, автоматически вызывая Stop().
         */
        virtual ~ThreadTimer();
        /**
         * @brief Проверяет, запущен ли фоновый поток обработки таймеров.
         */
        bool IsRunning() const;
        /**
         * @brief Инициализирует фоновый поток с заданной частотой проверки.
         * 
         * @param [in] sleepMilliseconds: Время максимального ожидания (сна) потока между проверками (>= 1 мс).
         */
        void Start(int sleepMilliseconds = 100);
        /**
         * @brief Безопасно останавливает фоновый поток.
         */
        void Stop();
        /**
         * @brief Создает новый таймер и регистрирует его в системе.
         * Таймер создается в состоянии ПАУЗЫ. Используйте StartTimer() или ResumeTimer() для запуска.
         */
        uint64_t CreateTimer(const DateTime& startTime, const DateTime& endTime, std::shared_ptr<ICallable> callable = nullptr, const std::string& title = "unknown");
        /**
         * @brief Активирует таймер (снимает с паузы).
         */
        void StartTimer(uint64_t id);
        /**
         * @brief Принудительно останавливает таймер и помечает его на удаление.
         */
        void StopTimer(uint64_t id);
        /**
         * @brief Временно приостанавливает отсчет времени для таймера.
         */
        void PauseTimer(uint64_t id);
        /**
         * @brief Возобновляет работу ранее приостановленного таймера.
         */
        void ResumeTimer(uint64_t id);
        /**
         * @brief Перезапускает существующий таймер с новыми параметрами.
         */
        void RestartTimer(uint64_t id, const DateTime& startTime, const DateTime& endTime);
        /**
         * @brief Возвращает время, прошедшее с момента активации таймера.
         */
        TimeSpan ElapsedTime(uint64_t id) const;
        /**
         * @brief Возвращает время, оставшееся до ближайшего срабатывания таймера.
         */
        TimeSpan RemainingTime(uint64_t id) const;
        /**
         * @brief Ищет идентификатор таймера по его текстовому заголовку (названию).
         */
        uint64_t FindTimerIdFromTitle(const std::string& title) const;
        /**
         * @brief Очищает память от всех неактивных таймеров.
         */
        uint64_t RemoveStopTimers();
        /**
         * @brief Возвращает накопительный счетчик всех созданных в системе таймеров.
         */
        uint64_t TotalTimersCreated() const;
    };
}

#endif // LIBOTHER_THREAD_TIMER_H
