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
		TimeSpan _interval;          // Длительность одного периода срабатывания (Get/Set).
        TimeSpan _currents;          // Текущее накопленное время (аккумулятор) (Get/Set).
		
    public:
        /**
         * @brief Конструктор таймера.
         * @param interval Длительность интервала (по умолчанию 0).
         */
        IntervalTimer(const TimeSpan& interval = TimeSpan::Zero) : _interval(interval), _currents(TimeSpan::Zero) { }
		
        /**
         * @brief Накапливает прошедшее время.
         * @param diff Разница во времени с момента последнего обновления (Delta Time).
         */
        void Update(const TimeSpan& diff)
        {
            _currents += diff;
            if (_currents < TimeSpan::Zero)
                _currents = TimeSpan::Zero;
        }
		
        /**
         * @brief Проверяет, прошел ли заданный интервал.
         * @return true, если накопленное время больше или равно интервалу.
         */
        bool IsPassed()
        {
            return _currents >= _interval;
        }
		
        /**
         * @brief Сбрасывает таймер, вычитая интервал из текущего времени.
         * Использование вычитания вместо обнуления позволяет сохранить остаток времени
         * для следующего цикла, обеспечивая высокую точность на длительном периоде.
         */
        void Reset()
        {
            if (_currents >= _interval)
                _currents -= _interval;
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
		
		// Дополнительный метод для получения прогресса от 0.0 до 1.0
        double Progress() const 
        {
            if (_interval.Ticks() == 0) 
				return 1.0;
			
            return static_cast<double>(_currents.Ticks()) / _interval.Ticks();
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
        const TimeSpan& Currents() const
        {
            return _currents;
        }
		
		/**
         * @brief Принудительно задает накопленное значение таймера.
         */
        void SetCurrents(const TimeSpan& current)
        {
            _currents = current;
        }
    };
}

#endif
