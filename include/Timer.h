#ifndef LIBOTHER_TIMER_H
#define LIBOTHER_TIMER_H

#include "Define.h"
#include "DateTime.h"

namespace other::time::timer
{
    /**
     * @class ICallable
     * @brief Интерфейс для обратного вызова (callback).
     * Любой класс, который хочет выполнить действие по завершению таймера, должен наследоваться от него.
     */
    class ICallable
    {
    public:
        virtual ~ICallable() = default;
        /**
         * @brief Метод, который будет вызван при срабатывании таймера.
         */
        virtual void callAfterTimeout() = 0;
    };

    /**
     * @class Timer
     * @brief Базовый класс управления логикой таймеров.
     * Содержит коллекцию таймеров и методы для расчета их состояний. 
     * Не является потокобезопасным (защиту обеспечивает внешний контроллер).
     */
    class Timer
    {
    public:
        /**
         * @struct TimerInfo
         * @brief Контейнер данных конкретного таймера.
         * Хранит все параметры времени, состояния и пользовательскую логику.
         */
        struct TimerInfo
        {
            /**
             * @brief Конструктор по умолчанию.
             * Инициализирует таймер в безопасном состоянии:
             * - _finish = false (таймер "жив")
             * - _paused = true  (таймер ждет команды StartTimer или ResumeTimer)
             * - _called = false (колбэк еще не выполнялся)
             */
            TimerInfo()
                : _startTime(DateTime::Zero)
                , _endTime(DateTime::Zero)
                , _finish(false)
                , _paused(true)
                , _called(false)
                , _title("unknown")
                , _callable(nullptr)
            {
            }

            // --- Временные параметры ---
            DateTime    _startTime;   // Точка во времени, начиная с которой таймер считается активным
            DateTime    _endTime;     // Точка во времени, когда должно произойти ближайшее срабатывание

            // --- Флаги состояния ---
            bool        _finish;      // Флаг завершения: если true, таймер будет удален при чистке
            bool        _paused;      // Флаг паузы: если true, проверки времени игнорируются
            bool        _called;      // Флаг выполнения: предотвращает повторный вызов колбэка для одной точки времени

            // --- Пользовательские данные ---
            std::string _title;                   // Имя таймера для идентификации в логах или поиске.
            std::shared_ptr<ICallable> _callable; // Объект-обработчик события срабатывания.
        };

        using timer_m = std::map<uint64_t, TimerInfo>;

    private:
        uint64_t _timerId;      // Счетчик для генерации уникальных идентификаторов.
        timer_m  _timerList;    // Основное хранилище: ID таймера -> Данные таймера.

    public:
        static Timer* Instance();

        /**
         * @brief Конструктор ядра управления таймерами.
         * Инициализирует внутренний счетчик идентификаторов.
         * 
         * @param [in] timerId: Начальное значение для генератора ID.
         */
        explicit Timer(uint64_t timerId = 0);
        virtual ~Timer() = default;
        /**
         * @brief Создает новый таймер и регистрирует его в системе.
         * 
         * @param [in] startTime: Время, начиная с которого таймер считается активным.
         * @param [in] endTime:   Время первого (или единственного) срабатывания.
         * @param [in] callable:  Умный указатель на объект интерфейса ICallable, чей метод будет выполнен.
         * @param [in] title:     Человекочитаемое название таймера (полезно для логов и поиска).
         * 
         * @return uint64_t Уникальный идентификатор созданного таймера (ID).
         */
        uint64_t CreateTimer(const DateTime& startTime, const DateTime& endTime, std::shared_ptr<ICallable> callable = nullptr, const std::string& title = "unknown");
        /**
         * @brief Активирует таймер по его ID.
         * 
         * @param [in] id: Уникальный идентификатор таймера, который нужно запустить.
         */
        void StartTimer(uint64_t id);
        /**
         * @brief Перезапускает существующий таймер с новыми параметрами времени.
         * 
         * @param [in] id:        Идентификатор таймера, который нужно обновить.
         * @param [in] startTime: Новая точка отсчета активации.
         * @param [in] endTime:   Новое время ближайшего срабатывания.
         */
        void RestartTimer(uint64_t id, const DateTime& startTime, const DateTime& endTime);
        /**
         * @brief Логическая остановка таймера.
         * 
         * @param [in] id: Уникальный идентификатор таймера.
         */
        void StopTimer(uint64_t id);
        /**
         * @brief Приостанавливает работу таймера.
         * 
         * @param [in] id: Уникальный идентификатор таймера.
         */
        void PauseTimer(uint64_t id);
        /**
         * @brief Возобновляет работу ранее приостановленного таймера.
         * Метод игнорируется, если таймер был окончательно остановлен (_finish = true).
         * 
         * @param [in] id: Уникальный идентификатор таймера.
         */
        void ResumeTimer(uint64_t id);
        /**
         * @brief Проверяет наступление времени срабатывания, обновляет состояние таймера
         * и возвращает колбэк для вызова (или nullptr, если таймер не сработал / без колбэка).
         * Предназначен для безопасного вызова под мьютексом в многопоточных контроллерах.
         *
         * @param [in] id: Уникальный идентификатор таймера.
         * @return std::shared_ptr<ICallable> Колбэк для вызова вне блокировки либо nullptr.
         */
        std::shared_ptr<ICallable> PollDueCallback(uint64_t id);
        /**
         * @brief Главный аналитический метод.
         * Сравнивает текущее время с _endTime таймера.
         * 
         * Если время пришло и таймер не на паузе:
         * Помечает таймер завершенным (_finish = true) и вызывает _callable->callAfterTimeout() (если задан).
         * 
         * @param [in] id: Уникальный идентификатор таймера.
         */
        void CheckTimeout(uint64_t id);
        /**
         * @brief Возвращает время, прошедшее с момента старта таймера.
         * 
         * @param [in] id: Уникальный идентификатор таймера.
         * 
         * @return TimeSpan Интервал прошедшего времени.
         */
        TimeSpan ElapsedTime(uint64_t id) const;
        /**
         * @brief Возвращает время, оставшееся до срабатывания таймера.
         * Если время уже вышло, таймер на паузе, завершен или не найден — возвращает TimeSpan::Zero.
         * 
         * @param [in] id: Уникальный идентификатор таймера.
         * 
         * @return TimeSpan Интервал времени до срабатывания.
         */
        TimeSpan RemainingTime(uint64_t id) const;
        /**
         * @brief Очищает map от всех завершенных таймеров.
         *
         * @return uint64_t Количество успешно удаленных таймеров.
         */
        uint64_t RemoveStopTimers();
        /**
         * @brief Возвращает общее количество созданных таймеров (накопительный счетчик).
         * 
         * @return uint64_t Накопительный счетчик созданных таймеров.
         */
        uint64_t TotalTimersCreated() const;
        /**
         * @brief Ищет идентификатор таймера по его текстовому названию.
         * 
         * @param [in] title: Строка-название, указанная при создании таймера.
         * 
         * @return uint64_t ID первого найденного таймера с таким заголовком (или 0).
         */
        uint64_t FindTimerIdFromTitle(const std::string& title) const;
        /**
         * @brief Предоставляет доступ к внутреннему хранилищу таймеров.
         * 
         * @return const timer_m& Константная ссылка на std::map с данными таймеров.
         */
        const timer_m& TimerList() const;
    };
}

#endif // LIBOTHER_TIMER_H
