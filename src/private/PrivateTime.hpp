#include "Define.h"

/** 
 * @section Константы времени в секундах.
 */
 
/** @brief Количество секунд в одной минуте. */
static constexpr int64_t SECS_PER_MINUTE       = 60LL;
/** @brief Количество секунд в одном часе (60 * 60). */
static constexpr int64_t SECS_PER_HOUR         = (60LL * SECS_PER_MINUTE);
/** @brief Количество секунд в полных сутках (3600 * 24). */
static constexpr int64_t SECS_PER_DAY          = (24LL * SECS_PER_HOUR);
/** @brief Условное количество секунд в месяце (30 дней). Используется для грубых оценок. */
static constexpr int64_t SECS_PER_MONTH        = (30LL * SECS_PER_DAY);
/** @brief Разница в секундах между Windows FileTime (1601) и Unix Epoch (1970), включает 369 лет, из которых 89 — високосные. */
static constexpr int64_t SECS_1601_TO_1970     = ((369LL * 365LL + 89LL) * SECS_PER_DAY);
/** @brief Разница в секундах между 1601 годом и 1980 годом (начало эпохи DOS/FAT). */
static constexpr int64_t SECS_1601_TO_1980     = ((379LL * 365LL + 91LL) * SECS_PER_DAY);

/** 
 * @section Константы времени в тиках (1 тик = 100 наносекунд).
 */

/** @brief Количество тиков в одной миллисекунде (1 ms = 10,000 ticks). */
static constexpr int64_t TICKS_PER_MILLISECOND = 10000LL;
/** @brief Количество тиков в одной секунде (1 s = 10,000,000 ticks). */
static constexpr int64_t TICKS_PER_SECOND      = (1000LL * TICKS_PER_MILLISECOND);
/** @brief Количество тиков в одной минуте. */
static constexpr int64_t TICKS_PER_MINUTE      = (60LL * TICKS_PER_SECOND);
/** @brief Количество тиков в одном часе. */
static constexpr int64_t TICKS_PER_HOUR        = (60LL * TICKS_PER_MINUTE);
/** @brief Количество тиков в одних сутках. */
static constexpr int64_t TICKS_PER_DAY         = (24LL * TICKS_PER_HOUR);
/**@brief Количество тиков от 01.01.0001 до 01.01.1970 (Unix Epoch). 
 * Критически важная константа для конвертации системного времени в Unix Timestamp.
 */
static constexpr int64_t TICKS_UNIX_EPOCH      = 621355968000000000LL;
/** @brief Количество тиков между эпохой Windows (1601) и Unix (1970). */
static constexpr int64_t TICKS_1601_TO_1970    = (SECS_1601_TO_1970 * TICKS_PER_SECOND);
/** @brief Количество тактов между эпохой Windows (1601) и DOS (1980). */
static constexpr int64_t TICKS_1601_TO_1980    = (SECS_1601_TO_1980 * TICKS_PER_SECOND);
/** @brief Смещение в тиках от 01.01.0001 до начала эпохи Windows FileTime (01.01.1601). */
static constexpr int64_t FILE_TIME_OFFSET      = 504911232000000000LL;

/** 
 * @section Данные григорианского календаря.
 */

/** @brief Накопленное количество дней по месяцам для обычного года (365 дней). Index 12 = 365. */
static constexpr int DaysToMonth365[] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365 };
/** @brief Накопленное количество дней по месяцам для високосного года (366 дней). Index 12 = 366. */
static constexpr int DaysToMonth366[] = { 0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366 };

/** @brief Дней в полном 400-летнем цикле григорианского календаря. */
static constexpr int64_t DAYS_PER_400YEARS = 146097;
/** @brief Дней в 100-летнем цикле (без учета високосного года в конце цикла). */
static constexpr int64_t DAYS_PER_100YEARS = 36524;
/** @brief Дней в 4-летнем цикле (включая один високосный год). */
static constexpr int64_t DAYS_PER_4YEARS   = 1461;
/** @brief Дней в обычном (невисокосном) году. */
static constexpr int64_t DAYS_PER_YEAR     = 365;
/** @brief Количество дней в високосном году. */
static constexpr int64_t DAYS_LEAP_YEAR    = 366;

/** @brief Полные названия дней недели. */
static const char* long_days_table[]  = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
/** @brief Сокращенные названия дней недели. */
static const char* short_days_table[] = { "Sun",    "Mon",    "Tue",     "Wed",        "Thu",      "Fri",    "Sat" };
/** @brief Полные названия месяцев. */
static const char* long_months_table[]   = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };
/** @brief Сокращенные названия месяцев. */
static const char* short_months_table[] = { "Jan",     "Feb",      "Mar",   "Apr",   "May", "Jun",   "Jul",  "Aug",    "Sep",      "Oct",     "Nov",      "Dec" };

/** 
 * @section Вспомогательные функции и лимиты.
 */

/** @brief Проверяет, является ли символ разделителем даты. */
static bool IsDateSeparator(char c) { return (c == '.') || (c == '/') || (c == '-'); }
/** @brief Проверяет, является ли символ разделителем времени. */
static bool IsTimeSeparator(char c) { return (c == ':'); }

/** @brief Минимально возможное значение тиков для даты (01.01.0001 00:00:00). */
static constexpr int64_t MinTicksDateTime = 0000000000000000000LL;
/** @brief Максимально возможное значение тиков для даты (31.12.9999 23:59:59). */
static constexpr int64_t MaxTicksDateTime = 3155378975999999999LL;

/** @brief Минимальное значение для интервала времени TimeSpan (отрицательное). */
static constexpr int64_t MinTicksTimeSpan = -9223372036854775807LL - 1LL;
/** @brief Максимальное значение для интервала времени TimeSpan. */
static constexpr int64_t MaxTicksTimeSpan = 9223372036854775807LL;

/**@brief Макрос для вычисления суммарного количества тиков из компонентов времени.
 * @param RESULT Переменная, в которую будет записан результат.
 * @param DAYS, HOURS, MINUTES, SECONDS, MS Компоненты времени.
 */
#define ADDED_TICKS(RESULT, DAYS, HOURS, MINUTES, SECONDS, MS)                          \
    do                                                                                  \
    {                                                                                   \
       (RESULT) = (static_cast<int64_t>(DAYS)    * (TICKS_PER_DAY))     +               \
                  (static_cast<int64_t>(HOURS)   * (TICKS_PER_HOUR))    +               \
                  (static_cast<int64_t>(MINUTES) * (TICKS_PER_MINUTE))  +               \
                  (static_cast<int64_t>(SECONDS) * (TICKS_PER_SECOND))  +               \
			      (static_cast<int64_t>(MS)      * (TICKS_PER_MILLISECOND));            \
    } while (0)

/**@brief Макрос для разложения общего количества тиков на отдельные компоненты (дни, часы и т.д.).
 * @param TICKS Исходное количество тиков.
 * @param DAYS, HOURS, MINUTES, SECONDS, MS Переменные для записи результата.
 */
#define FROM_TICKS(TICKS, DAYS, HOURS, MINUTES, SECONDS, MS)                            \
    do                                                                                  \
    {                                                                                   \
        (DAYS)    = ((TICKS)  / TICKS_PER_DAY);                                         \
        (HOURS)   = (((TICKS) / TICKS_PER_HOUR) % 24);                                  \
        (MINUTES) = (((TICKS) / TICKS_PER_MINUTE) % 60);                                \
        (SECONDS) = (((TICKS) / TICKS_PER_SECOND) % 60);                                \
        (MS)      = (((TICKS) / TICKS_PER_MILLISECOND) % 1000);                         \
    } while (0)
