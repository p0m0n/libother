/**
 * @file PrivateTime.hpp
 * @brief Внутренние константы, таблицы календаря и безопасная арифметика модуля времени.
 *
 * @details
 * Этот заголовок предназначен исключительно для внутреннего использования внутри реализации
 * классов даты и временных интервалов и не является частью публичного API библиотеки.
 *
 * Единицей измерения времени в модуле служит один тик длительностью 100 наносекунд (10 миллионов
 * тиков в одной секунде), отсчитываемый от `01.01.0001 00:00:00.0000000`. Файл содержит константы
 * перевода между секундами и тиками, смещения системных эпох (Windows FileTime, Unix Epoch, DOS/FAT),
 * таблицы длин месяцев и текстовых названий григорианского календаря, допустимые границы диапазонов,
 * а также функции арифметики 64-битных целых чисел с обязательной проверкой переполнения.
 */

#ifndef LIBOTHER_PRIVATETIME_HPP
#define LIBOTHER_PRIVATETIME_HPP

#include "Define.h"


namespace other::time
{
    ///////////////////////////////////////////////////////////////////////
    ///            КОНСТАНТЫ ВРЕМЕНИ В СЕКУНДАХ                         ///
    ///////////////////////////////////////////////////////////////////////

    /** @brief Количество секунд в одной минуте (`60`). */
    inline constexpr int64_t SECS_PER_MINUTE  = 60LL;
    /** @brief Количество секунд в одном часе (`3600`). */
    inline constexpr int64_t SECS_PER_HOUR    = (60LL * SECS_PER_MINUTE);
    /** @brief Количество секунд в полных сутках (`86400`). */
    inline constexpr int64_t SECS_PER_DAY     = (24LL * SECS_PER_HOUR);
    /** @brief Условное количество секунд в месяце из 30 дней (для приближённых оценок). */
    inline constexpr int64_t SECS_PER_MONTH   = (30LL * SECS_PER_DAY);
    /** @brief Разница в секундах между эпохой Windows FileTime (`01.01.1601`) и Unix Epoch (`01.01.1970`) — 369 лет, включая 89 високосных. */
    inline constexpr int64_t SECS_1601_TO_1970 = ((369LL * 365LL + 89LL) * SECS_PER_DAY);
    /** @brief Разница в секундах между эпохой Windows FileTime (`01.01.1601`) и эпохой DOS/FAT (`01.01.1980`) — 379 лет, включая 91 високосный. */
    inline constexpr int64_t SECS_1601_TO_1980 = ((379LL * 365LL + 91LL) * SECS_PER_DAY);


    ///////////////////////////////////////////////////////////////////////
    ///            КОНСТАНТЫ ВРЕМЕНИ В ТИКАХ (1 ТИК = 100 НС)           ///
    ///////////////////////////////////////////////////////////////////////

    /** @brief Количество тиков в одной миллисекунде (`10 000`). */
    inline constexpr int64_t TICKS_PER_MILLISECOND = 10000LL;
    /** @brief Количество тиков в одной секунде (`10 000 000`). */
    inline constexpr int64_t TICKS_PER_SECOND      = (1000LL * TICKS_PER_MILLISECOND);
    /** @brief Количество тиков в одной минуте. */
    inline constexpr int64_t TICKS_PER_MINUTE      = (60LL * TICKS_PER_SECOND);
    /** @brief Количество тиков в одном часе. */
    inline constexpr int64_t TICKS_PER_HOUR        = (60LL * TICKS_PER_MINUTE);
    /** @brief Количество тиков в одних сутках. */
    inline constexpr int64_t TICKS_PER_DAY         = (24LL * TICKS_PER_HOUR);
    /** @brief Количество тиков от `01.01.0001` до начала Unix Epoch (`01.01.1970 00:00:00 UTC`). */
    inline constexpr int64_t TICKS_UNIX_EPOCH      = 621355968000000000LL;
    /** @brief Количество тиков между эпохой Windows (`1601`) и Unix (`1970`). */
    inline constexpr int64_t TICKS_1601_TO_1970    = (SECS_1601_TO_1970 * TICKS_PER_SECOND);
    /** @brief Количество тиков между эпохой Windows (`1601`) и DOS/FAT (`1980`). */
    inline constexpr int64_t TICKS_1601_TO_1980    = (SECS_1601_TO_1980 * TICKS_PER_SECOND);
    /** @brief Смещение в тиках от `01.01.0001` до начала эпохи Windows FileTime (`01.01.1601 00:00:00 UTC`). */
    inline constexpr int64_t FILE_TIME_OFFSET      = 504911232000000000LL;


    ///////////////////////////////////////////////////////////////////////
    ///            ДАННЫЕ И ТАБЛИЦЫ ГРИГОРИАНСКОГО КАЛЕНДАРЯ            ///
    ///////////////////////////////////////////////////////////////////////

    /** @brief Накопленное число дней к началу каждого месяца обычного года (`index 0 = 0`, `index 12 = 365`). */
    inline constexpr int DaysToMonth365[] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365 };
    /** @brief Накопленное число дней к началу каждого месяца високосного года (`index 0 = 0`, `index 12 = 366`). */
    inline constexpr int DaysToMonth366[] = { 0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366 };

    /** @brief Число дней в полном 400-летнем цикле григорианского календаря (`146097`). */
    inline constexpr int64_t DAYS_PER_400YEARS = 146097;
    /** @brief Число дней в 100-летнем цикле без високосного векового года (`36524`). */
    inline constexpr int64_t DAYS_PER_100YEARS = 36524;
    /** @brief Число дней в 4-летнем цикле с одним високосным годом (`1461`). */
    inline constexpr int64_t DAYS_PER_4YEARS   = 1461;
    /** @brief Число дней в обычном невисокосном году (`365`). */
    inline constexpr int64_t DAYS_PER_YEAR     = 365;
    /** @brief Число дней в високосном году (`366`). */
    inline constexpr int64_t DAYS_LEAP_YEAR    = 366;

    /** @brief Полные англоязычные названия дней недели, начиная с воскресенья (`0 = Sunday .. 6 = Saturday`). */
    inline constexpr const char* long_days_table[]    = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
    /** @brief Сокращённые трёхбуквенные названия дней недели (`0 = Sun .. 6 = Sat`). */
    inline constexpr const char* short_days_table[]   = { "Sun",    "Mon",    "Tue",     "Wed",       "Thu",      "Fri",    "Sat" };
    /** @brief Полные англоязычные названия месяцев (`0 = January .. 11 = December`). */
    inline constexpr const char* long_months_table[]  = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };
    /** @brief Сокращённые трёхбуквенные названия месяцев (`0 = Jan .. 11 = Dec`). */
    inline constexpr const char* short_months_table[] = { "Jan",     "Feb",      "Mar",   "Apr",   "May", "Jun",  "Jul",  "Aug",    "Sep",       "Oct",     "Nov",      "Dec" };


    ///////////////////////////////////////////////////////////////////////
    ///            ЛИМИТЫ, ПРЕДИКАТЫ И МАКРОСЫ РАЗЛОЖЕНИЯ ТИКОВ         ///
    ///////////////////////////////////////////////////////////////////////

    /**
     * @brief Проверяет, является ли символ допустимым разделителем компонентов даты (`'.'`, `'/'` или `'-'`).
     * @param [in] c Проверяемый символ.
     * @return `true`, если `c` равен `'.'`, `'/'` или `'-'`; `false` иначе.
     */
    inline constexpr bool IsDateSeparator(char c) { return (c == '.') || (c == '/') || (c == '-'); }

    /**
     * @brief Проверяет, является ли символ разделителем компонентов времени (`':'`).
     * @param [in] c Проверяемый символ.
     * @return `true`, если `c == ':'`; `false` иначе.
     */
    inline constexpr bool IsTimeSeparator(char c) { return (c == ':'); }

    /** @brief Минимально допустимое значение тиков для даты (`01.01.0001 00:00:00.0000000`). */
    inline constexpr int64_t MinTicksDateTime = 0LL;
    /** @brief Максимально допустимое значение тиков для даты (`31.12.9999 23:59:59.9999999`). */
    inline constexpr int64_t MaxTicksDateTime = 3155378975999999999LL;

    /** @brief Минимальное значение тиков для знакового временного интервала `TimeSpan`. */
    inline constexpr int64_t MinTicksTimeSpan = std::numeric_limits<int64_t>::min();
    /** @brief Максимальное значение тиков для знакового временного интервала `TimeSpan`. */
    inline constexpr int64_t MaxTicksTimeSpan = std::numeric_limits<int64_t>::max();

    /**
     * @brief Безопасно вычисляет суммарное количество тиков из отдельных компонентов времени и записывает в `RESULT`.
     * @param [out] RESULT  Переменная-приёмник типа `int64_t` (или совместимого), куда записывается итог в тиках.
     * @param [in]  DAYS    Количество дней (приводится к `int64_t`).
     * @param [in]  HOURS   Количество часов (приводится к `int64_t`).
     * @param [in]  MINUTES Количество минут (приводится к `int64_t`).
     * @param [in]  SECONDS Количество секунд (приводится к `int64_t`).
     * @param [in]  MS      Количество миллисекунд (приводится к `int64_t`).
     * @throws std::out_of_range При переполнении 64-битного знакового счётчика тиков.
     */
#define ADDED_TICKS(RESULT, DAYS, HOURS, MINUTES, SECONDS, MS) \
    do \
    { \
        (RESULT) = ::other::time::CheckedComponentsToTicks( \
            static_cast<int64_t>(DAYS), \
            static_cast<int64_t>(HOURS), \
            static_cast<int64_t>(MINUTES), \
            static_cast<int64_t>(SECONDS), \
            static_cast<int64_t>(MS)); \
    } while (0)

    /**
     * @brief Раскладывает общее количество тиков на целые компоненты: дни, часы, минуты, секунды и миллисекунды.
     * @param [in]  TICKS   Исходное количество тиков (`int64_t`; выражение вычисляется несколько раз — избегайте побочных эффектов).
     * @param [out] DAYS    Переменная для записи целого числа дней.
     * @param [out] HOURS   Переменная для записи остатка часов (`-23..23`).
     * @param [out] MINUTES Переменная для записи остатка минут (`-59..59`).
     * @param [out] SECONDS Переменная для записи остатка секунд (`-59..59`).
     * @param [out] MS      Переменная для записи остатка миллисекунд (`-999..999`).
     */
#define FROM_TICKS(TICKS, DAYS, HOURS, MINUTES, SECONDS, MS) \
    do \
    { \
        (DAYS)    = ((TICKS)  / ::other::time::TICKS_PER_DAY); \
        (HOURS)   = (((TICKS) / ::other::time::TICKS_PER_HOUR) % 24); \
        (MINUTES) = (((TICKS) / ::other::time::TICKS_PER_MINUTE) % 60); \
        (SECONDS) = (((TICKS) / ::other::time::TICKS_PER_SECOND) % 60); \
        (MS)      = (((TICKS) / ::other::time::TICKS_PER_MILLISECOND) % 1000); \
    } while (0)


    ///////////////////////////////////////////////////////////////////////
    ///            БЕЗОПАСНАЯ АРИФМЕТИКА И ФОРМАТИРОВАНИЕ               ///
    ///////////////////////////////////////////////////////////////////////

    /**
     * @brief Выполняет сложение двух 64-битных знаковых целых с проверкой переполнения.
     * @param [in] lhs    Левый операнд сложения.
     * @param [in] rhs    Правый операнд сложения.
     * @param [in] errMsg Текст сообщения для исключения при переполнении (не должен быть `nullptr`).
     * @return Сумма `lhs + rhs`.
     * @throws std::out_of_range Если результат выходит за пределы диапазона `int64_t`.
     */
    inline int64_t CheckedAddInt64(int64_t lhs, int64_t rhs, const char* errMsg)
    {
        if ((rhs > 0 && lhs > std::numeric_limits<int64_t>::max() - rhs) ||
            (rhs < 0 && lhs < std::numeric_limits<int64_t>::min() - rhs))
        {
            throw std::out_of_range(errMsg);
        }
        return lhs + rhs;
    }

    /**
     * @brief Выполняет вычитание двух 64-битных знаковых целых с проверкой переполнения.
     * @param [in] lhs    Уменьшаемое.
     * @param [in] rhs    Вычитаемое.
     * @param [in] errMsg Текст сообщения для исключения при переполнении (не должен быть `nullptr`).
     * @return Разность `lhs - rhs`.
     * @throws std::out_of_range Если результат выходит за пределы диапазона `int64_t`.
     */
    inline int64_t CheckedSubInt64(int64_t lhs, int64_t rhs, const char* errMsg)
    {
        if ((rhs < 0 && lhs > std::numeric_limits<int64_t>::max() + rhs) ||
            (rhs > 0 && lhs < std::numeric_limits<int64_t>::min() + rhs))
        {
            throw std::out_of_range(errMsg);
        }
        return lhs - rhs;
    }

    /**
     * @brief Выполняет умножение 64-битного знакового целого на строго положительный множитель с проверкой переполнения.
     * @param [in] val    Множимое (может быть положительным, отрицательным или нулём).
     * @param [in] factor Положительный множитель (предусловие: `factor > 0`).
     * @param [in] errMsg Текст сообщения для исключения при переполнении (не должен быть `nullptr`).
     * @return Произведение `val * factor`.
     * @throws std::out_of_range Если результат выходит за пределы диапазона `int64_t`.
     */
    inline int64_t CheckedMulPosInt64(int64_t val, int64_t factor, const char* errMsg)
    {
        if (val > std::numeric_limits<int64_t>::max() / factor ||
            val < std::numeric_limits<int64_t>::min() / factor)
        {
            throw std::out_of_range(errMsg);
        }
        return val * factor;
    }

    /**
     * @brief Переводит компоненты времени (дни, часы, минуты, секунды, миллисекунды) в суммарное число тиков с защитой от переполнения.
     * @param [in] days         Количество дней (может быть отрицательным для интервалов).
     * @param [in] hours        Количество часов.
     * @param [in] minutes      Количество минут.
     * @param [in] seconds      Количество секунд.
     * @param [in] milliseconds Количество миллисекунд.
     * @return Суммарное значение в тиках (`1 тик = 100 нс`).
     * @throws std::out_of_range Если на любом этапе умножения или суммирования возникает переполнение `int64_t`.
     */
    inline int64_t CheckedComponentsToTicks(int64_t days, int64_t hours, int64_t minutes, int64_t seconds, int64_t milliseconds)
    {
        constexpr const char* kErr = "Time: components overflow int64_t ticks.";
        int64_t total = CheckedMulPosInt64(days, TICKS_PER_DAY, kErr);
        total = CheckedAddInt64(total, CheckedMulPosInt64(hours, TICKS_PER_HOUR, kErr), kErr);
        total = CheckedAddInt64(total, CheckedMulPosInt64(minutes, TICKS_PER_MINUTE, kErr), kErr);
        total = CheckedAddInt64(total, CheckedMulPosInt64(seconds, TICKS_PER_SECOND, kErr), kErr);
        total = CheckedAddInt64(total, CheckedMulPosInt64(milliseconds, TICKS_PER_MILLISECOND, kErr), kErr);
        return total;
    }

    /**
     * @brief Безопасно складывает значения в тиках и проверяет попадание итога в допустимый диапазон `DateTime`.
     * @param [in] lhs Исходное значение тиков.
     * @param [in] rhs Прибавляемое смещение в тиках (может быть отрицательным).
     * @return Сумма `lhs + rhs`, гарантированно лежащая в `[MinTicksDateTime, MaxTicksDateTime]`.
     * @throws std::out_of_range При арифметическом переполнении `int64_t` или выходе за пределы `01.01.0001..31.12.9999`.
     */
    inline int64_t CheckedAddTicks(int64_t lhs, int64_t rhs)
    {
        const int64_t result = CheckedAddInt64(lhs, rhs, "DateTime: ticks arithmetic overflow.");
        if (result < MinTicksDateTime || result > MaxTicksDateTime)
        {
            throw std::out_of_range("DateTime: ticks are out of range.");
        }
        return result;
    }

    /**
     * @brief Безопасно вычитает тики и проверяет попадание итога в допустимый диапазон `DateTime`.
     * @param [in] lhs Исходное значение тиков.
     * @param [in] rhs Вычитаемое смещение в тиках (может быть отрицательным).
     * @return Разность `lhs - rhs`, гарантированно лежащая в `[MinTicksDateTime, MaxTicksDateTime]`.
     * @throws std::out_of_range При арифметическом переполнении `int64_t` или выходе за пределы `01.01.0001..31.12.9999`.
     */
    inline int64_t CheckedSubTicks(int64_t lhs, int64_t rhs)
    {
        const int64_t result = CheckedSubInt64(lhs, rhs, "DateTime: ticks arithmetic overflow.");
        if (result < MinTicksDateTime || result > MaxTicksDateTime)
        {
            throw std::out_of_range("DateTime: ticks are out of range.");
        }
        return result;
    }

    /**
     * @brief Форматирует целое число в строку с дополнением ведущими нулями до заданной минимальной ширины.
     * @param [in] value Форматируемое целое число (например, месяц, день, час или миллисекунды).
     * @param [in] width Минимальная ширина результирующей строки в символах (например, `2` для `"05"` или `4` для `"2026"`).
     * @return Строковое представление числа, дополненное слева символами `'0'` при необходимости.
     * @throws std::bad_alloc При нехватке памяти для буфера потока или возвращаемой строки.
     */
    inline std::string PadNumber(int value, int width)
    {
        std::ostringstream oss;
        oss << std::setfill('0') << std::setw(width) << value;
        return oss.str();
    }

}

#endif // LIBOTHER_PRIVATETIME_HPP
