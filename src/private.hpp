#include <ctime>
#include <stdexcept>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <iostream>
#include <chrono>
#include <numeric>
#include <map>
#include <regex>

#undef min
#undef max

/** @brief Количество секунд в одной минуте. */
static constexpr int64_t SECS_PER_MINUTE       = 60LL;
/** @brief Количество секунд в одном часе (60 * 60). */
static constexpr int64_t SECS_PER_HOUR         = SECS_PER_MINUTE * 60LL;
/** @brief Количество секунд в полных сутках (3600 * 24). */
static constexpr int64_t SECS_PER_DAY          = SECS_PER_HOUR   * 24LL;
/** @brief Условное количество секунд в месяце (30 дней). Используется для грубых оценок. */
static constexpr int64_t SECS_PER_MONTH        = SECS_PER_DAY    * 30LL;
/** @brief Разница в секундах между Windows FileTime (1601) и Unix Epoch (1970), включает 369 лет, из которых 89 — високосные. */
static constexpr int64_t SECS_1601_TO_1970     = SECS_PER_DAY    * 369LL * 365LL + 89LL;
/** @brief Разница в секундах между 1601 годом и 1980 годом (начало эпохи DOS/FAT). */
static constexpr int64_t SECS_1601_TO_1980     = SECS_PER_DAY    * 379LL * 365LL + 91LL;

/** @brief Тактов в миллисекунде: 1 ms = 10,000 ticks. */
static constexpr int64_t TICKS_PER_MILLISECOND = 10000LL;
/** @brief Тактов в секунде: 1 s = 10,000,000 ticks. */
static constexpr int64_t TICKS_PER_SECOND      = TICKS_PER_MILLISECOND * 1000LL;
/** @brief Тактов в минуте: 60 секунд. */
static constexpr int64_t TICKS_PER_MINUTE      = TICKS_PER_SECOND * 60LL;
/** @brief Тактов в часе: 3,600 секунд. */
static constexpr int64_t TICKS_PER_HOUR        = TICKS_PER_MINUTE * 60LL;
/** @brief Тактов в сутках: 86,400 секунд. */
static constexpr int64_t TICKS_PER_DAY         = TICKS_PER_HOUR * 24LL;
/** * @brief Количество тиков от 01.01.0001 до 01.01.1970, критически важная константа для работы с Unix Timestamp. */
static constexpr int64_t TICKS_UNIX_EPOCH      = 621355968000000000LL;
/** @brief Количество тиков между эпохой 1601 года и 1970 годом. */
static constexpr int64_t TICKS_1601_TO_1970    = SECS_1601_TO_1970 * TICKS_PER_SECOND;
/** @brief Количество тактов между эпохой Windows FileTime (1601) и эпохой MS-DOS (1980), используется для конвертации времени в формат FAT/DIR, где отсчет ведется от 1 января 1980 года. */
static constexpr int64_t TICKS_1601_TO_1980    = SECS_1601_TO_1980 * TICKS_PER_SECOND;

/** @brief Накопленное количество дней по месяцам для обычного года (365 дней). */
static constexpr int DaysToMonth365[] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365 };
/** @brief Накопленное количество дней по месяцам для високосного года (366 дней). */
static constexpr int DaysToMonth366[] = { 0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366 };

/** @brief Дней в полном 400-летнем цикле григорианского календаря. */
static constexpr int64_t DAYS_PER_400YEARS = 146097;
/** @brief Дней в 100-летнем цикле (без учета високосного 400-го года). */
static constexpr int64_t DAYS_PER_100YEARS = 36524;
/** @brief Дней в 4-летнем цикле (с учетом одного високосного года). */
static constexpr int64_t DAYS_PER_4YEARS = 1461;
/** @brief Дней в обычном году. */
static constexpr int64_t DAYS_PER_YEAR = 365;
/** * @brief Количество дней в високосном году, используется в расчетах интервалов, превышающих один год, и для валидации дат (например, 366-й день допустим только в високосном году). */
static constexpr int64_t DAYS_LEAP_YEAR = 366;

/** @brief Названия дней недели для вывода в консоль или UI. */
static const char* long_days_table[]  = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
static const char* short_days_table[] = { "Sun",    "Mon",    "Tue",     "Wed",        "Thu",      "Fri",    "Sat" };
/** @brief Полные названия месяцев для вывода в консоль или UI. */
static const char* long_months_table[]   = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };
static const char* short_months_table[] = { "Jan",     "Feb",      "Mar",   "Apr",   "May", "Jun",   "Jul",  "Aug",    "Sep",      "Oct",     "Nov",      "Dec" };

/**
 * @brief Определяет, является ли символ разделителем в строке даты.
 * Позволяет корректно парсить форматы: 2025.10.10, 2025-10-10, 2025/10/10.
 */
static bool IsDateSeparator(char c) { return (c == '.') || (c == '/') || (c == '-'); }
static bool IsTimeSeparator(char c) { return (c == ':'); }

static constexpr int64_t MinTicksDateTime = 0000000000000000000LL;
static constexpr int64_t MaxTicksDateTime = 3155378975999999999LL; // 31.12.9999 23:59:59

static constexpr int64_t MinTicksTimeSpan = std::numeric_limits<int64_t>::min();
static constexpr int64_t MaxTicksTimeSpan = std::numeric_limits<int64_t>::max();

// Количество тактов от 01.01.0001 до 01.01.1601
static constexpr int64_t FILE_TIME_OFFSET = 504911232000000000LL;

// Макрос для накопление тактов.
#define ADDED_TICKS(RESULT,DAYS,PER_DAY,HOURS,PER_HOUR,MINUTES,PER_MIN,SECONDS,PER_SEC,MS,PER_MS)  \
    do                                                                                             \
    {                                                                                              \
       (RESULT) = (static_cast<int64_t>(DAYS)    * (PER_DAY))  +                                   \
                  (static_cast<int64_t>(HOURS)   * (PER_HOUR)) +                                   \
                  (static_cast<int64_t>(MINUTES) * (PER_MIN))  +                                   \
                  (static_cast<int64_t>(SECONDS) * (PER_SEC))  +                                   \
			      (static_cast<int64_t>(MS)      * (PER_MS));                                      \
    } while (0)
