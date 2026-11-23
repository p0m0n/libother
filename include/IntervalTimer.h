#ifndef LIBOTHER_INTERVALTIMER_H
#define LIBOTHER_INTERVALTIMER_H

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
         * @param interval Длительность интервала (по умолчанию 0; отрицательные значения приводятся к 0).
         */
        explicit IntervalTimer(const TimeSpan& interval = TimeSpan::Zero)
            : _interval(interval < TimeSpan::Zero ? TimeSpan::Zero : interval)
            , _currents(TimeSpan::Zero)
        {
        }

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
         * @return true, если задан положительный интервал и накопленное время больше или равно интервалу.
         */
        bool IsPassed() const
        {
            return _interval > TimeSpan::Zero && _currents >= _interval;
        }

        /**
         * @brief Сбрасывает таймер, вычитая интервал из текущего времени.
         * Использование вычитания вместо обнуления позволяет сохранить остаток времени
         * для следующего цикла, обеспечивая высокую точность на длительном периоде.
         */
        void Reset()
        {
            if (_interval > TimeSpan::Zero && _currents >= _interval)
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

        /**
         * @brief Возвращает прогресс текущего интервала в диапазоне [0.0, 1.0].
         */
        double Progress() const
        {
            if (_interval.Ticks() <= 0)
                return 0.0;

            double ratio = static_cast<double>(_currents.Ticks()) / static_cast<double>(_interval.Ticks());
            return std::clamp(ratio, 0.0, 1.0);
        }

        /**
         * @brief Возвращает текущий настроенный интервал срабатывания.
         */
        const TimeSpan& Interval() const
        {
            return _interval;
        }

        /**
         * @brief Устанавливает новую длительность периода (отрицательные значения приводятся к 0).
         */
        void SetInterval(const TimeSpan& interval)
        {
            _interval = (interval < TimeSpan::Zero) ? TimeSpan::Zero : interval;
        }

        /**
         * @brief Возвращает текущее накопленное время.
         */
        const TimeSpan& Currents() const
        {
            return _currents;
        }

        /**
         * @brief Принудительно задает накопленное значение таймера (отрицательные значения приводятся к 0).
         */
        void SetCurrents(const TimeSpan& current)
        {
            _currents = (current < TimeSpan::Zero) ? TimeSpan::Zero : current;
        }
    };
}

#endif // LIBOTHER_INTERVALTIMER_H
