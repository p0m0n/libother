#ifndef LIBOTHER_PRIVATETIME_HPP
#define LIBOTHER_PRIVATETIME_HPP

#include "Define.h"

namespace other::time
{
    /**
     * @section Константы времени в секундах.
     */

     /** @brief Количество секунд в одной минуте. */
    inline constexpr int64_t SECS_PER_MINUTE = 60LL;
    /** @brief Количество секунд в одном часе (60 * 60). */
    inline constexpr int64_t SECS_PER_HOUR = (60LL * SECS_PER_MINUTE);
    /** @brief Количество секунд в полных сутках (3600 * 24). */
    inline constexpr int64_t SECS_PER_DAY = (24LL * SECS_PER_HOUR);
    /** @brief Условное количество секунд в месяце (30 дней). Используется для грубых оценок. */
    inline constexpr int64_t SECS_PER_MONTH = (30LL * SECS_PER_DAY);
    /** @brief Разница в секундах между Windows FileTime (1601) и Unix Epoch (1970), включает 369 лет, из которых 89 — високосные. */
    inline constexpr int64_t SECS_1601_TO_1970 = ((369LL * 365LL + 89LL) * SECS_PER_DAY);
    /** @brief Разница в секундах между 1601 годом и 1980 годом (начало эпохи DOS/FAT). */
    inline constexpr int64_t SECS_1601_TO_1980 = ((379LL * 365LL + 91LL) * SECS_PER_DAY);

    /**
     * @section Константы времени в тиках (1 тик = 100 наносекунд).
     */

     /** @brief Количество тиков в одной миллисекунде (1 ms = 10,000 ticks). */
    inline constexpr int64_t TICKS_PER_MILLISECOND = 10000LL;
    /** @brief Количество тиков в одной секунде (1 s = 10,000,000 ticks). */
    inline constexpr int64_t TICKS_PER_SECOND = (1000LL * TICKS_PER_MILLISECOND);
    /** @brief Количество тиков в одной минуте. */
    inline constexpr int64_t TICKS_PER_MINUTE = (60LL * TICKS_PER_SECOND);
    /** @brief Количество тиков в одном часе. */
    inline constexpr int64_t TICKS_PER_HOUR = (60LL * TICKS_PER_MINUTE);
    /** @brief Количество тиков в одних сутках. */
    inline constexpr int64_t TICKS_PER_DAY = (24LL * TICKS_PER_HOUR);
    /** @brief Количество тиков от 01.01.0001 до 01.01.1970 (Unix Epoch). */
    inline constexpr int64_t TICKS_UNIX_EPOCH = 621355968000000000LL;
    /** @brief Количество тиков между эпохой Windows (1601) и Unix (1970). */
    inline constexpr int64_t TICKS_1601_TO_1970 = (SECS_1601_TO_1970 * TICKS_PER_SECOND);
    /** @brief Количество тактов между эпохой Windows (1601) и DOS (1980). */
    inline constexpr int64_t TICKS_1601_TO_1980 = (SECS_1601_TO_1980 * TICKS_PER_SECOND);
    /** @brief Смещение в тиках от 01.01.0001 до начала эпохи Windows FileTime (01.01.1601). */
    inline constexpr int64_t FILE_TIME_OFFSET = 504911232000000000LL;

    /**
     * @section Данные григорианского календаря.
     */

     /** @brief Накопленное количество дней по месяцам для обычного года (365 дней). Index 12 = 365. */
    inline constexpr int DaysToMonth365[] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365 };
    /** @brief Накопленное количество дней по месяцам для високосного года (366 дней). Index 12 = 366. */
    inline constexpr int DaysToMonth366[] = { 0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366 };

    /** @brief Дней в полном 400-летнем цикле григорианского календаря. */
    inline constexpr int64_t DAYS_PER_400YEARS = 146097;
    /** @brief Дней в 100-летнем цикле (без учета високосного года в конце цикла). */
    inline constexpr int64_t DAYS_PER_100YEARS = 36524;
    /** @brief Дней в 4-летнем цикле (включая один високосный год). */
    inline constexpr int64_t DAYS_PER_4YEARS = 1461;
    /** @brief Дней в обычном (невисокосном) году. */
    inline constexpr int64_t DAYS_PER_YEAR = 365;
    /** @brief Количество дней в високосном году. */
    inline constexpr int64_t DAYS_LEAP_YEAR = 366;

    /** @brief Полные названия дней недели. */
    inline constexpr const char* long_days_table[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
    /** @brief Сокращенные названия дней недели. */
    inline constexpr const char* short_days_table[] = { "Sun",    "Mon",    "Tue",     "Wed",        "Thu",      "Fri",    "Sat" };
    /** @brief Полные названия месяцев. */
    inline constexpr const char* long_months_table[] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };
    /** @brief Сокращенные названия месяцев. */
    inline constexpr const char* short_months_table[] = { "Jan",     "Feb",      "Mar",   "Apr",   "May", "Jun",   "Jul",  "Aug",    "Sep",      "Oct",     "Nov",      "Dec" };

    /**
     * @section Вспомогательные функции и лимиты.
     */

     /** @brief Проверяет, является ли символ разделителем даты. */
    inline constexpr bool IsDateSeparator(char c) { return (c == '.') || (c == '/') || (c == '-'); }
    /** @brief Проверяет, является ли символ разделителем времени. */
    inline constexpr bool IsTimeSeparator(char c) { return (c == ':'); }

    /** @brief Минимально возможное значение тиков для даты (01.01.0001 00:00:00). */
    inline constexpr int64_t MinTicksDateTime = 0LL;
    /** @brief Максимально возможное значение тиков для даты (31.12.9999 23:59:59.9999999). */
    inline constexpr int64_t MaxTicksDateTime = 3155378975999999999LL;

    /** @brief Минимальное значение для интервала времени TimeSpan (отрицательное). */
    inline constexpr int64_t MinTicksTimeSpan = std::numeric_limits<int64_t>::min();
    /** @brief Максимальное значение для интервала времени TimeSpan. */
    inline constexpr int64_t MaxTicksTimeSpan = std::numeric_limits<int64_t>::max();

    /** @brief Макрос для безопасного вычисления суммарного количества тиков из компонентов времени. */
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

/** @brief Макрос для разложения общего количества тиков на отдельные компоненты (дни, часы и т.д.). */
#define FROM_TICKS(TICKS, DAYS, HOURS, MINUTES, SECONDS, MS) \
    do \
    { \
        (DAYS)    = ((TICKS)  / ::other::time::TICKS_PER_DAY); \
        (HOURS)   = (((TICKS) / ::other::time::TICKS_PER_HOUR) % 24); \
        (MINUTES) = (((TICKS) / ::other::time::TICKS_PER_MINUTE) % 60); \
        (SECONDS) = (((TICKS) / ::other::time::TICKS_PER_SECOND) % 60); \
        (MS)      = (((TICKS) / ::other::time::TICKS_PER_MILLISECOND) % 1000); \
    } while (0)


    /** @brief Безопасное сложение int64_t с проверкой знакового переполнения. */
    inline int64_t CheckedAddInt64(int64_t lhs, int64_t rhs, const char* errMsg)
    {
        if ((rhs > 0 && lhs > std::numeric_limits<int64_t>::max() - rhs) ||
            (rhs < 0 && lhs < std::numeric_limits<int64_t>::min() - rhs))
        {
            throw std::out_of_range(errMsg);
        }
        return lhs + rhs;
    }

    /** @brief Безопасное вычитание int64_t с проверкой знакового переполнения. */
    inline int64_t CheckedSubInt64(int64_t lhs, int64_t rhs, const char* errMsg)
    {
        if ((rhs < 0 && lhs > std::numeric_limits<int64_t>::max() + rhs) ||
            (rhs > 0 && lhs < std::numeric_limits<int64_t>::min() + rhs))
        {
            throw std::out_of_range(errMsg);
        }
        return lhs - rhs;
    }

    /** @brief Безопасное умножение int64_t на положительный множитель. */
    inline int64_t CheckedMulPosInt64(int64_t val, int64_t factor, const char* errMsg)
    {
        if (val > std::numeric_limits<int64_t>::max() / factor ||
            val < std::numeric_limits<int64_t>::min() / factor)
        {
            throw std::out_of_range(errMsg);
        }
        return val * factor;
    }

    /** @brief Безопасное вычисление тиков из компонентов времени (дни, часы, минуты, секунды, миллисекунды). */
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

    /** @brief Безопасное сложение тиков с проверкой границ DateTime. */
    inline int64_t CheckedAddTicks(int64_t lhs, int64_t rhs)
    {
        const int64_t result = CheckedAddInt64(lhs, rhs, "DateTime: ticks arithmetic overflow.");
        if (result < MinTicksDateTime || result > MaxTicksDateTime)
        {
            throw std::out_of_range("DateTime: ticks are out of range.");
        }
        return result;
    }

    /** @brief Безопасное вычитание тиков с проверкой границ DateTime. */
    inline int64_t CheckedSubTicks(int64_t lhs, int64_t rhs)
    {
        const int64_t result = CheckedSubInt64(lhs, rhs, "DateTime: ticks arithmetic overflow.");
        if (result < MinTicksDateTime || result > MaxTicksDateTime)
        {
            throw std::out_of_range("DateTime: ticks are out of range.");
        }
        return result;
    }

    /** @brief Форматирует целое число с ведущими нулями до указанной ширины. */
    inline std::string PadNumber(int value, int width)
    {
        std::ostringstream oss;
        oss << std::setfill('0') << std::setw(width) << value;
        return oss.str();
    }

}

#endif // LIBOTHER_PRIVATETIME_HPP
