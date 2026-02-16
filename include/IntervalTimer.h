#ifndef _OTHER_INTERVALTIMER_H_
#define _OTHER_INTERVALTIMER_H_

#include "TimeSpan.h"


namespace other::time::timer
{
    /**
     * @class IntervalTimer
     * @brief Класс для отсчета повторяющихся временных интервалов.
     * Реализует механизм "аккумулятора времени", что позволяет избегать накопления
     * ошибки (дрейфа времени) при выполнении периодических задач.
     */
    class IntervalTimer
    {
    private:
        TimeSpan _interval;    // Длительность одного периода срабатывания (Get/Set).
        TimeSpan _current;     // Текущее накопленное время (аккумулятор) (Get/Set).

    public:
        /**
         * @brief Конструктор таймера.
         * @param interval Длительность интервала (по умолчанию 0).
         */
        IntervalTimer(const TimeSpan& interval = TimeSpan::Zero) : _interval(interval), _current(TimeSpan::Zero) { }
        /**
         * @brief Накапливает прошедшее время.
         * @param diff Разница во времени с момента последнего обновления (Delta Time).
         */
        void Update(const TimeSpan& diff)
        {
            _current += diff;
            // Защита: текущее время не может быть отрицательным (например, при скачках системных часов)
            if (_current < TimeSpan::Zero)
                _current = TimeSpan::Zero;
        }
        /**
         * @brief Проверяет, прошел ли заданный интервал.
         * @return true, если накопленное время больше или равно интервалу.
         */
        bool IsPassed()
        {
            return _current >= _interval;
        }
        /**
         * @brief Сбрасывает таймер, вычитая интервал из текущего времени.
         * Использование вычитания вместо обнуления позволяет сохранить остаток времени
         * для следующего цикла, обеспечивая высокую точность на длительном периоде.
         */
        void Reset()
        {
            if (_current >= _interval)
                _current -= _interval;
        }
        /**
         * @brief Комбинированный метод: проверяет готовность и сразу сбрасывает таймер.
         * @return true, если интервал пройден и таймер был успешно сброшен.
         */
        bool IsTicks()
        {
            if (IsPassed())
            {
                Reset();
                return true;
            }
            return false;
        }
        
		/**
         * @brief Возвращает текущий настроенный интервал срабатывания.
         */
        const TimeSpan& Interval() const
        {
            return _interval;
        }
		/**
         * @brief Устанавливает новую длительность периода.
         */
        void SetInterval(const TimeSpan& interval)
        {
            _interval = interval;
        }
		
		/**
         * @brief Возвращает текущее накопленное время.
         */
        const TimeSpan& Current() const
        {
            return _current;
        }
		/**
         * @brief Принудительно задает накопленное значение таймера.
         */
        void SetCurrent(const TimeSpan& current)
        {
            _current = current;
        }
    };
}

#endif
