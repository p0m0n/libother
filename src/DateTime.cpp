#include "DateTime.h"
#include "private/PrivateTime.hpp"


namespace other::time
{
    // Внутренняя вспомогательная функция для быстрой сборки стандартных форматов.
    static std::string InternalFormatByChar(const DateTime& dt, char format);

    // Инициализация нулевого значения (0 тиков).
    const DateTime DateTime::Zero(0LL, DateTimeKind::Unspecified);
    // Инициализация минимального значения.
    const DateTime DateTime::MinValue(MinTicksDateTime, DateTimeKind::Unspecified);
    // Инициализация максимального значения.
    const DateTime DateTime::MaxValue(MaxTicksDateTime, DateTimeKind::Unspecified);

    // Основной конструктор по тактам.
    DateTime::DateTime(int64_t ticks, const DateTimeKind& kind) : _ticks(ticks), _kind(kind)
    {
        // Проверка границ допустимого диапазона.
        if (ticks < MinTicksDateTime || ticks > MaxTicksDateTime)
        {
            throw std::out_of_range("DateTime: ticks are out of range.");
        }
    }

    // Делегирующий конструктор (Г.М.Д).
    DateTime::DateTime(int year, int month, int day)
        : DateTime(year, month, day, 0, 0, 0, 0, DateTimeKind::Unspecified)
    {
    }

    // Полный конструктор со всеми компонентами.
    DateTime::DateTime(int year, int month, int day, int hour, int minute, int second, int millisecond, const DateTimeKind& kind) : _kind(kind)
    {
        // Базовая проверка корректности месяца и года.
        if ((year < 1 || year > 9999) || (month < 1 || month > 12))
        {
            throw std::out_of_range("DateTime: year or Month out of range.");
        }

        // Выбираем таблицу дней (високосный год или нет).
        const int* days = IsLeapYear(year) ? DaysToMonth366 : DaysToMonth365;

        // Проверка диапазона дней в конкретном месяце.
        if (day < 1 || day > (days[month] - days[month - 1]))
        {
            throw std::out_of_range("DateTime: day out of range.");
        }

        // Проверка диапазона времени.
        if ((hour < 0 || hour >= 24) || (minute < 0 || minute >= 60) || (second < 0 || second >= 60) || (millisecond < 0 || millisecond >= 1000))
        {
            throw std::out_of_range("DateTime: time components out of range.");
        }

        // Формула: дни за полные года + поправки на високосные года + дни в текущем году.
        year -= 1;
        int nday = year * 365 + year / 4 - year / 100 + year / 400 + days[month - 1] + day - 1;

        int64_t ticks = 0;
        // Макрос для преобразования дней, часов и мс в общую сумму тиков.
        ADDED_TICKS(ticks, nday, hour, minute, second, millisecond);

        // Проверка границ допустимого диапазона.
        if (ticks < MinTicksDateTime || ticks > MaxTicksDateTime)
        {
            throw std::out_of_range("DateTime: ticks are out of range.");
        }

        // Сохраняем результат.
        _ticks = ticks;
    }

    // Конструктор копирования.
    DateTime::DateTime(const DateTime& value) : _ticks(value._ticks), _kind(value._kind)
    {
    }

    // Конструктор перемещения.
    DateTime::DateTime(DateTime&& value) noexcept : _ticks(value._ticks), _kind(value._kind)
    {
        // "Сбрасываем" временный объект в валидное состояние.
        value._ticks = 0;
        value._kind  = DateTimeKind::Unspecified;
    }

    DateTime DateTime::Now()
    {
        // Берем UTC и переводим в часовой пояс пользователя.
        return UtcNow().ToLocalTime();
    }

    DateTime DateTime::UtcNow()
    {
        // Используем стандартную библиотеку C++ chrono.
        auto now = std::chrono::system_clock::now();
        auto duration = now.time_since_epoch();

        // Переводим наносекунды из chrono в тики (1 тик = 100 нс).
        int64_t ticks = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count() / 100;

        // Прибавляем разницу между Unix Epoch (1970) и началом нашей эры (0001).
        return DateTime(CheckedAddTicks(ticks, TICKS_UNIX_EPOCH), DateTimeKind::Utc);
    }

    DateTime DateTime::Today()
    {
        return DateTime::Now().Date();
    }

    int64_t DateTime::Ticks() const { return _ticks; }
    DateTimeKind DateTime::Kind() const { return _kind; }

    int DateTime::Year() const
    {
        int64_t n = _ticks / TICKS_PER_DAY;       // Получаем общее кол-во дней.
        int y400 = (int)(n / DAYS_PER_400YEARS);  // Считаем 400-летние циклы.
        n -= y400 * DAYS_PER_400YEARS;

        int y100 = (int)(n / DAYS_PER_100YEARS);  // Считаем 100-летние циклы.
        if (y100 == 4)
            y100 = 3;                             // Коррекция для последнего года цикла.

        n -= y100 * DAYS_PER_100YEARS;
        int y4 = (int)(n / DAYS_PER_4YEARS);      // Считаем 4-летние циклы.
        n -= y4 * DAYS_PER_4YEARS;

        int y1 = (int)(n / DAYS_PER_YEAR);        // Оставшиеся года.
        if (y1 == 4)
            y1 = 3;

        return y400 * 400 + y100 * 100 + y4 * 4 + y1 + 1;
    }

    int DateTime::Month() const
    {
        int year = Year();
        int dayOfYear = DayOfYear();
        const int* days = DateTime::IsLeapYear(year) ? DaysToMonth366 : DaysToMonth365;

        int m = (dayOfYear >> 5) + 1;
        while (dayOfYear > days[m])
            m++;
        return m;
    }

    int DateTime::Day() const
    {
        int y = Year();
        int dayOfYear = DayOfYear();
        const int* days = DateTime::IsLeapYear(y) ? DaysToMonth366 : DaysToMonth365;

        int m = (dayOfYear >> 5) + 1;
        while (dayOfYear > days[m])
            m++;

        return dayOfYear - days[m - 1];
    }

    int DateTime::Hour() const { return (int)((_ticks / TICKS_PER_HOUR) % 24); }
    int DateTime::Minute() const { return (int)((_ticks / TICKS_PER_MINUTE) % 60); }
    int DateTime::Second() const { return (int)((_ticks / TICKS_PER_SECOND) % 60); }
    int DateTime::Millisecond() const { return (int)((_ticks / TICKS_PER_MILLISECOND) % 1000); }

    DayOfWeek DateTime::GetDayOfWeek() const
    {
        // Получаем общее количество полных дней с 01.01.0001.
        int64_t totalDays = _ticks / TICKS_PER_DAY;

        int dayIndex = (int)((totalDays + 1) % 7);
        return static_cast<DayOfWeek>(dayIndex);
    }

    int DateTime::DayOfYear() const
    {
        int64_t n = _ticks / TICKS_PER_DAY;
        int y400 = (int)(n / DAYS_PER_400YEARS);
        n -= y400 * DAYS_PER_400YEARS;

        int y100 = (int)(n / DAYS_PER_100YEARS);
        if (y100 == 4)
            y100 = 3;

        n -= y100 * DAYS_PER_100YEARS;
        int y4 = (int)(n / DAYS_PER_4YEARS);
        n -= y4 * DAYS_PER_4YEARS;

        int y1 = (int)(n / DAYS_PER_YEAR);
        if (y1 == 4)
            y1 = 3;

        n -= y1 * DAYS_PER_YEAR;
        return (int)n + 1;
    }

    MonthOfYear DateTime::GetMonthOfYear() const
    {
        return static_cast<MonthOfYear>(Month());
    }

    DateTime DateTime::Date() const
    {
        // Убираем остаток от деления на количество тиков в сутках.
        return DateTime(_ticks - (_ticks % TICKS_PER_DAY), _kind);
    }

    TimeSpan DateTime::TimeOfDay() const
    {
        return TimeSpan(_ticks % TICKS_PER_DAY);
    }

    DateTime DateTime::Add(const TimeSpan& value) const
    {
        // Важно: передать _kind, чтобы после сложения UTC-время оставалось UTC-временем.
        return DateTime(CheckedAddTicks(_ticks, value.Ticks()), _kind);
    }

    DateTime DateTime::AddDays(double value) const
    {
        return Add(TimeSpan::FromDays(value));
    }

    DateTime DateTime::AddHours(double value) const
    {
        return Add(TimeSpan::FromHours(value));
    }

    DateTime DateTime::AddMinutes(double value) const
    {
        return Add(TimeSpan::FromMinutes(value));
    }

    DateTime DateTime::AddSeconds(double value) const
    {
        return Add(TimeSpan::FromSeconds(value));
    }

    DateTime DateTime::AddMilliseconds(double value) const
    {
        return Add(TimeSpan::FromMilliseconds(value));
    }

    DateTime DateTime::AddTicks(int64_t value) const
    {
        return DateTime(CheckedAddTicks(_ticks, value), _kind);
    }

    DateTime DateTime::AddMonths(int months) const
    {
        if (months < -120000 || months > 120000)
        {
            throw std::out_of_range("DateTime: months out of range.");
        }

        // Получаем компоненты.
        int year  = Year();
        int month = Month();
        int day   = Day();

        int i = month - 1 + months;
        if (i >= 0)
        {
            month = i % 12 + 1;
            year = year + i / 12;
        }
        else
        {
            month = 12 + (i + 1) % 12;
            year = year + (i - 11) / 12;
        }

        if (year < 1 || year > 9999)
        {
            throw std::out_of_range("DateTime: resulting year out of range.");
        }

        // Если в целевом месяце меньше дней (например, был 31 день, стал 28).
        int daysInMonth = DateTime::DaysInMonth(year, month);
        if (day > daysInMonth)
            day = daysInMonth;

        // Сохраняем субмиллисекундные тики.
        int64_t subMsTicks = _ticks % TICKS_PER_MILLISECOND;
        DateTime base(year, month, day, Hour(), Minute(), Second(), Millisecond(), _kind);
        return DateTime(CheckedAddTicks(base.Ticks(), subMsTicks), _kind);
    }

    DateTime DateTime::AddYears(int years) const
    {
        if (years < -10000 || years > 10000)
        {
            throw std::out_of_range("DateTime: years out of range.");
        }

        // Получаем компоненты.
        int year  = Year();
        int month = Month();
        int day   = Day();

        // Рассчитываем новый год.
        int newYear = year + years;
        if (newYear < 1 || newYear > 9999)
        {
            throw std::out_of_range("DateTime: resulting year out of range.");
        }

        // Обработка 29 февраля: если в новом году нет 29-го числа, берем 28-е.
        if (month == 2 && day == 29 && !DateTime::IsLeapYear(newYear))
        {
            day = 28;
        }

        // Собираем новую дату, сохраняя субмиллисекундные тики и Kind.
        int64_t subMsTicks = _ticks % TICKS_PER_MILLISECOND;
        DateTime base(newYear, month, day, Hour(), Minute(), Second(), Millisecond(), _kind);
        return DateTime(CheckedAddTicks(base.Ticks(), subMsTicks), _kind);
    }

    DateTime DateTime::Subtract(const TimeSpan& value) const
    {
        return DateTime(CheckedSubTicks(_ticks, value.Ticks()), _kind);
    }

    TimeSpan DateTime::Subtract(const DateTime& value) const
    {
        return TimeSpan(_ticks - value._ticks);
    }

    DateTime DateTime::ToLocalTime() const
    {
        if (_kind == DateTimeKind::Local)
            return *this;

        // Конвертируем тики нашего объекта в unix timestamp (UTC).
        std::time_t utc_t = static_cast<std::time_t>((_ticks - TICKS_UNIX_EPOCH) / TICKS_PER_SECOND);
        std::tm ltm = {};

#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
        if (localtime_s(&ltm, &utc_t) != 0)
            throw std::out_of_range("DateTime: ToLocalTime failed for the given date.");
#else
        if (localtime_r(&utc_t, &ltm) == nullptr)
            throw std::out_of_range("DateTime: ToLocalTime failed for the given date.");
#endif
        // Создаем объект из компонентов, которые нам дала система (уже локальных).
        DateTime local(ltm.tm_year + 1900, ltm.tm_mon + 1, ltm.tm_mday, ltm.tm_hour, ltm.tm_min, ltm.tm_sec, 0, DateTimeKind::Local);

        // Возвращаем точность: добавляем остаток тиков, которые меньше одной секунды.
        int64_t subSecTicks = _ticks % TICKS_PER_SECOND;
        return DateTime(CheckedAddTicks(local.Ticks(), subSecTicks), DateTimeKind::Local);
    }

    DateTime DateTime::ToUniversalTime() const
    {
        // Если время уже UTC, возвращаем как есть.
        if (_kind == DateTimeKind::Utc)
            return *this;

        // Заполняем структуру tm компонентами.
        std::tm ltm = {};
        ltm.tm_year = Year() - 1900;
        ltm.tm_mon  = Month() - 1;
        ltm.tm_mday = Day();
        ltm.tm_hour = Hour();
        ltm.tm_min  = Minute();
        ltm.tm_sec  = Second();

        // -1 заставляет систему саму определить, действовал ли летний переход именно в эту дату.
        ltm.tm_isdst = -1;

        // mktime берет локальное время и возвращает UTC timestamp (секунды).
        std::time_t utc_t = std::mktime(&ltm);
        if (utc_t == static_cast<std::time_t>(-1))
        {
            // Проверяем, является ли -1 реальной ошибкой или датой 1969-12-31 23:59:59 UTC.
            std::tm verify = {};
#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
            if (localtime_s(&verify, &utc_t) != 0)
                throw std::out_of_range("DateTime: ToUniversalTime failed for the given date.");
#else
            if (localtime_r(&utc_t, &verify) == nullptr)
                throw std::out_of_range("DateTime: ToUniversalTime failed for the given date.");
#endif
            if (verify.tm_year != ltm.tm_year || verify.tm_mon != ltm.tm_mon || verify.tm_mday != ltm.tm_mday ||
                verify.tm_hour != ltm.tm_hour || verify.tm_min != ltm.tm_min || verify.tm_sec != ltm.tm_sec)
            {
                throw std::out_of_range("DateTime: ToUniversalTime failed for the given date.");
            }
        }

        // Переводим секунды обратно в тики и возвращаем точность < 1 секунды.
        int64_t ticks = FromUnixTimeSeconds(static_cast<int64_t>(utc_t)).Ticks();
        int64_t subSecTicks = _ticks % TICKS_PER_SECOND;
        return DateTime(CheckedAddTicks(ticks, subSecTicks), DateTimeKind::Utc);
    }

    int DateTime::Compare(const DateTime& value) const
    {
        if (_ticks > value._ticks) return 1;
        if (_ticks < value._ticks) return -1;
        return 0;
    }

    bool DateTime::Equals(const DateTime& value) const
    {
        return _ticks == value._ticks;
    }

    size_t DateTime::GetHashCode() const
    {
        // Хэш согласован с Equals() / operator== (сравнение только по _ticks).
        return std::hash<int64_t>{}(_ticks);
    }

    bool DateTime::IsDaylightSavingTime() const
    {
        // В UTC времени переходов на летнее время не существует.
        if (_kind == DateTimeKind::Utc)
            return false;

        try
        {
            // localtime_s / localtime_r ожидают UTC time_t, поэтому сначала переводим в UTC.
            DateTime utc = ToUniversalTime();
            std::time_t time = static_cast<std::time_t>(utc.ToUnixTimeSeconds());
            std::tm ltm = {};

#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
            if (localtime_s(&ltm, &time) != 0)
                return false;
#else
            // POSIX версия для Linux / macOS
            if (localtime_r(&time, &ltm) == nullptr)
                return false;
#endif
            return ltm.tm_isdst > 0;
        }
        catch (...)
        {
            return false;
        }
    }

    int DateTime::DaysInMonth(int year, int month)
    {
        // Проверка месяца.
        if (month < 1 || month > 12)
            return 0;

        // Проверка года.
        if (year < 1 || year > 9999)
            return 0;

        // Выбираем нужный массив (високосный/обычный).
        const int* daysToMonth = DateTime::IsLeapYear(year) ? DaysToMonth366 : DaysToMonth365;

        // Количество дней в месяце — это разница между текущим и предыдущим накопленным значением.
        return daysToMonth[month] - daysToMonth[month - 1];
    }

    bool DateTime::IsLeapYear(int year)
    {
        // Год високосный, если делится на 4, но не на 100, либо делится на 400.
        return (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
    }

    int DateTime::Compare(const DateTime& t1, const DateTime& t2)
    {
        return t1.Compare(t2);
    }

    bool DateTime::Equals(const DateTime& t1, const DateTime& t2)
    {
        return t1.Equals(t2);
    }

    DateTime DateTime::SpecifyKind(const DateTime& value, const DateTimeKind& kind)
    {
        return DateTime(value.Ticks(), kind);
    }

    std::string DateTime::ToString(const std::string& format) const
    {
        if (format.empty())
        {
            // Вызов стандартного ДД.ММ.ГГГГ ЧЧ:ММ:СС
            return InternalFormatByChar(*this, 'G');
        }

        // Если передана строка из одного символа, вызываем быстрый switch.
        if (format.length() == 1)
        {
            return InternalFormatByChar(*this, format[0]);
        }

        const int year   = Year();
        const int month  = Month();
        const int day    = Day();
        const int hour   = Hour();
        const int minute = Minute();
        const int second = Second();
        const int dow    = static_cast<int>(GetDayOfWeek());

        std::string result;
        result.reserve(format.length() + 16);

        for (size_t i = 0; i < format.length(); i++)
        {
            // Проверяем маски от самых длинных к коротким.
            if (format.compare(i, 4, "yyyy") == 0)
            {
                result += PadNumber(year, 4);
                i += 3;
            }
            else if (format.compare(i, 2, "yy") == 0)
            {
                result += PadNumber(year % 100, 2);
                i += 1;
            }
            else if (format.compare(i, 4, "MMMM") == 0)
            {
                result += long_months_table[month - 1];
                i += 3;
            }
            else if (format.compare(i, 3, "MMM") == 0)
            {
                result += short_months_table[month - 1];
                i += 2;
            }
            else if (format.compare(i, 2, "MM") == 0)
            {
                result += PadNumber(month, 2);
                i += 1;
            }
            else if (format.compare(i, 4, "dddd") == 0)
            {
                result += long_days_table[dow];
                i += 3;
            }
            else if (format.compare(i, 3, "ddd") == 0)
            {
                result += short_days_table[dow];
                i += 2;
            }
            else if (format.compare(i, 2, "dd") == 0)
            {
                result += PadNumber(day, 2);
                i += 1;
            }
            else if (format.compare(i, 2, "HH") == 0)
            {
                result += PadNumber(hour, 2);
                i += 1;
            }
            else if (format.compare(i, 2, "mm") == 0)
            {
                result += PadNumber(minute, 2);
                i += 1;
            }
            else if (format.compare(i, 2, "ss") == 0)
            {
                result += PadNumber(second, 2);
                i += 1;
            }
            else
            {
                result += format[i];
            }
        }
        return result;
    }

    std::vector<std::string> DateTime::DateTimeFormats() const
    {
        static const char formats[] = { 'd', 'D', 't', 'T', 'g', 'G', 'f', 'F', 'm', 'y', 's', 'u', 'r' };

        std::vector<std::string> results;
        results.reserve(sizeof(formats));

        for (char f : formats)
        {
            results.push_back(InternalFormatByChar(*this, f));
        }
        return results;
    }

    // Возвращает строку, содержащую только дату в коротком формате "dd.MM.yyyy".
    std::string DateTime::ToShortDateString() const
    {
        return InternalFormatByChar(*this, 'd');
    }

    // Возвращает строку с полным названием "Thursday, 25 December 2025".
    std::string DateTime::ToLongDateString() const
    {
        return InternalFormatByChar(*this, 'D');
    }

    // Возвращает строку, содержащую только время в коротком формате "HH:mm".
    std::string DateTime::ToShortTimeString() const
    {
        return InternalFormatByChar(*this, 't');
    }

    // Возвращает строку, содержащую время в полном формате "HH:mm:ss".
    std::string DateTime::ToLongTimeString() const
    {
        return InternalFormatByChar(*this, 'T');
    }

    DateTime DateTime::Parse(const std::string& input)
    {
        DateTime result;
        if (TryParse(input, result))
            return result;

        throw std::invalid_argument("DateTime::Parse: invalid date/time string.");
    }

    // Вспомогательная функция (статическая)
    static int FindIndex(const std::string& input)
    {
        for (int i = 0; i < 12; ++i)
        {
            if (input.find(long_months_table[i]) != std::string::npos)
                return i;
        }
        return -1;
    }

    // Разбор дробной части секунд (от 1 до 7 цифр) в миллисекунды.
    static bool ParseFractionalSecondsToMs(const std::string& str, size_t& pos, int& millisecond)
    {
        if (pos >= str.size() || str[pos] != '.')
            return true;

        ++pos;
        size_t start = pos;
        while (pos < str.size() && std::isdigit(static_cast<unsigned char>(str[pos])))
            ++pos;

        size_t len = pos - start;
        if (len == 0)
            return false;

        std::string frac = str.substr(start, std::min<size_t>(len, 3));
        while (frac.size() < 3)
            frac.push_back('0');

        millisecond = std::stoi(frac);
        return true;
    }

    bool DateTime::TryParse(const std::string& input, DateTime& result)
    {
        if (input.empty())
            return false;

        char sep1 = 0, sep2 = 0, col1 = 0, col2 = 0;
        int  year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0, millisecond = 0;

        try
        {
            // --- 1. ПРОВЕРКА: RFC1123 (напр. "Mon, 12 Jan 2026 19:24:08 GMT") ---
            if (input.find("GMT") != std::string::npos && input.length() >= 25)
            {
                std::istringstream iss(input);
                std::string dowToken, monthToken, timeToken, tzToken;
                if (iss >> dowToken >> day >> monthToken >> year >> timeToken >> tzToken && tzToken == "GMT")
                {
                    for (int i = 0; i < 12; ++i)
                    {
                        if (monthToken == short_months_table[i])
                        {
                            month = i + 1;
                            break;
                        }
                    }

                    std::string extra;
                    if (month != 0 && !(iss >> extra))
                    {
                        size_t pos = 0;
                        if (timeToken.size() >= 8 && timeToken[2] == ':' && timeToken[5] == ':')
                        {
                            hour   = std::stoi(timeToken.substr(0, 2));
                            minute = std::stoi(timeToken.substr(3, 2));
                            second = std::stoi(timeToken.substr(6, 2));
                            pos = 8;
                            if (ParseFractionalSecondsToMs(timeToken, pos, millisecond) && pos == timeToken.size())
                            {
                                result = DateTime(year, month, day, hour, minute, second, millisecond, DateTimeKind::Utc);
                                return true;
                            }
                        }
                    }
                }

                result = DateTime::Zero;
                return false;
            }

            // --- 2. ISO 8601 (2026-01-12T19:24:08 или 2026-01-12 19:24:08Z) + Смещение (Offset) ---
            if (input.length() >= 10 && input[4] == '-' && input[7] == '-')
            {
                year  = std::stoi(input.substr(0, 4));
                month = std::stoi(input.substr(5, 2));
                day   = std::stoi(input.substr(8, 2));

                size_t pos = 10;
                if (pos < input.length() && (input[pos] == 'T' || input[pos] == ' '))
                {
                    if (input.length() < 19 || input[13] != ':' || input[16] != ':')
                    {
                        result = DateTime::Zero;
                        return false;
                    }

                    hour   = std::stoi(input.substr(11, 2));
                    minute = std::stoi(input.substr(14, 2));
                    second = std::stoi(input.substr(17, 2));
                    pos = 19;

                    if (!ParseFractionalSecondsToMs(input, pos, millisecond))
                    {
                        result = DateTime::Zero;
                        return false;
                    }
                }

                // Обработка Z или смещения (+03:00 / +0300)
                int64_t offsetTicks = 0;
                DateTimeKind kind = DateTimeKind::Unspecified;
                if (pos < input.length())
                {
                    if (input[pos] == 'Z' && pos + 1 == input.length())
                    {
                        kind = DateTimeKind::Utc;
                        pos += 1;
                    }
                    else if (input[pos] == '+' || input[pos] == '-')
                    {
                        char sign = input[pos];
                        int offH = 0;
                        int offM = 0;

                        if (pos + 6 == input.length() && input[pos + 3] == ':')
                        {
                            offH = std::stoi(input.substr(pos + 1, 2));
                            offM = std::stoi(input.substr(pos + 4, 2));
                            pos += 6;
                        }
                        else if (pos + 5 == input.length())
                        {
                            offH = std::stoi(input.substr(pos + 1, 2));
                            offM = std::stoi(input.substr(pos + 3, 2));
                            pos += 5;
                        }
                        else
                        {
                            result = DateTime::Zero;
                            return false;
                        }

                        if (offH < 0 || offH > 23 || offM < 0 || offM > 59)
                        {
                            result = DateTime::Zero;
                            return false;
                        }

                        offsetTicks = (offH * TICKS_PER_HOUR) + (offM * TICKS_PER_MINUTE);
                        if (sign == '+')
                            offsetTicks = -offsetTicks; // UTC = Local - Offset
                        kind = DateTimeKind::Utc;
                    }
                    else
                    {
                        result = DateTime::Zero;
                        return false;
                    }
                }

                if (pos != input.length())
                {
                    result = DateTime::Zero;
                    return false;
                }

                DateTime base(year, month, day, hour, minute, second, millisecond, kind);
                result = DateTime(CheckedAddTicks(base.Ticks(), offsetTicks), kind);
                return true;
            }

            // --- 3. Текстовые форматы (Monday, 12 January 2026 [19:24:08]) ---
            int foundMonthIdx = FindIndex(input);
            if (foundMonthIdx != -1)
            {
                month = foundMonthIdx + 1;
                std::istringstream iss(input);
                std::string word;

                while (iss >> word)
                {
                    if (std::isdigit(static_cast<unsigned char>(word[0])))
                    {
                        // Сначала проверяем, является ли токен временем (HH:mm[:ss]).
                        if (word.find(':') != std::string::npos)
                        {
                            int consumed = 0;
                            int fields = m_sscanf(word.c_str(), "%2d:%2d:%2d%n", &hour, &minute, &second, &consumed);
                            if (fields < 2)
                            {
                                result = DateTime::Zero;
                                return false;
                            }
                            if (fields == 2)
                            {
                                consumed = 0;
                                m_sscanf(word.c_str(), "%2d:%2d%n", &hour, &minute, &consumed);
                            }
                            if (static_cast<size_t>(consumed) != word.size())
                            {
                                result = DateTime::Zero;
                                return false;
                            }
                        }
                        else
                        {
                            size_t idx = 0;
                            int val = std::stoi(word, &idx);
                            if (idx != word.size())
                            {
                                result = DateTime::Zero;
                                return false;
                            }
                            if (val > 31)
                                year = val;
                            else if (day == 0)
                                day = val;
                            else
                                year = val;
                        }
                    }
                }

                if (year == 0)
                    year = DateTime::Now().Year();

                if (day == 0)
                    day = 1;

                result = DateTime(year, month, day, hour, minute, second, millisecond);
                return true;
            }

            // --- ПРОВЕРКА 4: Чистое время (19:24 или 19:24:08) ---
            if (input.find(':') != std::string::npos && input.find_first_of(".-") == std::string::npos)
            {
                std::istringstream iss(input);
                if (iss >> hour >> col1 >> minute && IsTimeSeparator(col1))
                {
                    if (iss.peek() == ':')
                    {
                        if (!(iss >> col2 >> second))
                        {
                            result = DateTime::Zero;
                            return false;
                        }
                    }

                    std::string extra;
                    if (!(iss >> extra))
                    {
                        DateTime now = DateTime::Now();
                        result = DateTime(now.Year(), now.Month(), now.Day(), hour, minute, second);
                        return true;
                    }
                }
                result = DateTime::Zero;
                return false;
            }

            // --- ПРОВЕРКА 5: Стандартный числовой (DD.MM.YYYY [HH:mm[:ss]]) ---
            std::istringstream iss(input);
            if (iss >> day >> sep1 >> month >> sep2 >> year)
            {
                // Проверяем, что разделители даты одинаковые и входят в список разрешенных.
                if (sep1 != sep2 || !IsDateSeparator(sep1))
                {
                    result = DateTime::Zero;
                    return false;
                }

                // Пропускаем пробелы после даты.
                iss >> std::ws;
                if (!iss.eof())
                {
                    if (!(iss >> hour >> col1 >> minute) || !IsTimeSeparator(col1))
                    {
                        result = DateTime::Zero;
                        return false;
                    }

                    if (iss.peek() == ':')
                    {
                        if (!(iss >> col2 >> second) || !IsTimeSeparator(col2))
                        {
                            result = DateTime::Zero;
                            return false;
                        }
                    }

                    iss >> std::ws;
                    if (!iss.eof())
                    {
                        result = DateTime::Zero;
                        return false;
                    }
                }

                result = DateTime(year, month, day, hour, minute, second, millisecond, DateTimeKind::Unspecified);
                return true;
            }
        }
        catch (...)
        {
            result = DateTime::Zero;
            return false;
        }

        result = DateTime::Zero;
        return false;
    }

    DateTime DateTime::ParseExact(const std::string& input, const std::string& format)
    {
        DateTime result;
        if (TryParseExact(input, format, result))
            return result;

        throw std::invalid_argument("DateTime::ParseExact: string does not match the specified format.");
    }

    DateTime DateTime::ParseExact(const std::string& input, const std::vector<std::string>& formats)
    {
        DateTime result;
        if (TryParseExact(input, formats, result))
            return result;

        throw std::invalid_argument("DateTime::ParseExact: string does not match any of the specified formats.");
    }

    bool DateTime::TryParseExact(const std::string& input, const std::string& format, DateTime& result)
    {
        if (input.empty() || format.empty())
            return false;

        try
        {
            // Подготовка формата (преобразование токенов).
            std::string cFormat = format;
            auto replace = [](std::string& str, const std::string& from, const std::string& to)
            {
                size_t start_pos = 0;
                while ((start_pos = str.find(from, start_pos)) != std::string::npos)
                {
                    str.replace(start_pos, from.length(), to);
                    start_pos += to.length();
                }
            };

            replace(cFormat, "yyyy", "%Y");
            replace(cFormat, "MM", "%m");
            replace(cFormat, "dd", "%d");
            replace(cFormat, "HH", "%H");
            replace(cFormat, "mm", "%M");
            replace(cFormat, "ss", "%S");

            // Инициализируем валидной датой по умолчанию (1900-01-01), чтобы частичные форматы
            // вроде "yyyy-MM" или "HH:mm:ss" не падали из-за tm_mday == 0.
            std::tm tm = {};
            tm.tm_mday = 1;
            tm.tm_mon  = 0;
            tm.tm_year = 0;

#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
            std::istringstream ss(input);
            ss >> std::get_time(&tm, cFormat.c_str());
            if (ss.fail() || ss.peek() != std::char_traits<char>::eof())
            {
                result = DateTime::Zero;
                return false;
            }
#else
            char* res = strptime(input.c_str(), cFormat.c_str(), &tm);
            if (res == nullptr || *res != '\0')
            {
                result = DateTime::Zero;
                return false;
            }
#endif

            result = DateTime(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, 0, DateTimeKind::Unspecified);
            return true;
        }
        catch (...)
        {
            result = DateTime::Zero;
            return false;
        }
    }

    bool DateTime::TryParseExact(const std::string& input, const std::vector<std::string>& formats, DateTime& result)
    {
        if (input.empty() || formats.empty())
            return false;

        for (const std::string& format : formats)
        {
            if (format.empty())
                continue;

            if (DateTime::TryParseExact(input, format, result))
                return true;
        }

        return false;
    }

    int64_t DateTime::ToUnixTimeSeconds() const
    {
        int64_t utcTicks = (_kind == DateTimeKind::Local) ? ToUniversalTime().Ticks() : _ticks;
        return (utcTicks - TICKS_UNIX_EPOCH) / TICKS_PER_SECOND;
    }

    int64_t DateTime::ToUnixTimeMilliseconds() const
    {
        int64_t utcTicks = (_kind == DateTimeKind::Local) ? ToUniversalTime().Ticks() : _ticks;
        return (utcTicks - TICKS_UNIX_EPOCH) / TICKS_PER_MILLISECOND;
    }

    DateTime DateTime::FromUnixTimeSeconds(int64_t seconds)
    {
        const int64_t minSeconds = (MinTicksDateTime - TICKS_UNIX_EPOCH) / TICKS_PER_SECOND;
        const int64_t maxSeconds = (MaxTicksDateTime - TICKS_UNIX_EPOCH) / TICKS_PER_SECOND;
        if (seconds < minSeconds || seconds > maxSeconds)
        {
            throw std::out_of_range("DateTime: unix seconds out of range.");
        }

        int64_t ticks = (seconds * TICKS_PER_SECOND) + TICKS_UNIX_EPOCH;
        return DateTime(ticks, DateTimeKind::Utc);
    }

    DateTime DateTime::FromUnixTimeMilliseconds(int64_t milliseconds)
    {
        const int64_t minMillis = (MinTicksDateTime - TICKS_UNIX_EPOCH) / TICKS_PER_MILLISECOND;
        const int64_t maxMillis = (MaxTicksDateTime - TICKS_UNIX_EPOCH) / TICKS_PER_MILLISECOND;
        if (milliseconds < minMillis || milliseconds > maxMillis)
        {
            throw std::out_of_range("DateTime: unix milliseconds out of range.");
        }

        int64_t ticks = (milliseconds * TICKS_PER_MILLISECOND) + TICKS_UNIX_EPOCH;
        return DateTime(ticks, DateTimeKind::Utc);
    }

    int64_t DateTime::ToBinary() const
    {
        // Приводим (1, 2, 3) к (0, 1, 2) и используем uint64_t во избежание UB при сдвиге знакового числа.
        uint64_t kindValue = 0;
        switch (_kind)
        {
        case DateTimeKind::Unspecified: kindValue = 0ULL; break;
        case DateTimeKind::Utc:         kindValue = 1ULL; break;
        case DateTimeKind::Local:       kindValue = 2ULL; break;
        default:                        kindValue = 0ULL; break;
        }

        uint64_t kindData = kindValue << 62;
        uint64_t tickBits = static_cast<uint64_t>(_ticks) & 0x3FFFFFFFFFFFFFFFULL;
        return static_cast<int64_t>(kindData | tickBits);
    }

    DateTime DateTime::FromBinary(int64_t dateData)
    {
        uint64_t raw = static_cast<uint64_t>(dateData);
        uint64_t kindRaw = (raw >> 62) & 0x03ULL;
        if (kindRaw > 2ULL)
        {
            throw std::out_of_range("DateTime: invalid DateTimeKind in binary data.");
        }

        int64_t ticks = static_cast<int64_t>(raw & 0x3FFFFFFFFFFFFFFFULL);
        DateTimeKind kind = static_cast<DateTimeKind>(static_cast<int>(kindRaw) + 1);
        return DateTime(ticks, kind);
    }

    int64_t DateTime::ToFileTime() const
    {
        return ToFileTimeUtc();
    }

    DateTime DateTime::FromFileTime(int64_t fileTime)
    {
        return FromFileTimeUtc(fileTime).ToLocalTime();
    }

    int64_t DateTime::ToFileTimeUtc() const
    {
        int64_t ticks = _ticks;

        if (_kind == DateTimeKind::Local)
        {
            ticks = ToUniversalTime().Ticks();
        }

        int64_t result = ticks - FILE_TIME_OFFSET;
        return (result < 0) ? 0 : result;
    }

    DateTime DateTime::FromFileTimeUtc(int64_t fileTime)
    {
        if (fileTime < 0)
            throw std::out_of_range("DateTime: fileTime is less than 0.");

        if (fileTime > MaxTicksDateTime - FILE_TIME_OFFSET)
            throw std::out_of_range("DateTime: fileTime exceeds MaxValue.");

        int64_t universalTicks = fileTime + FILE_TIME_OFFSET;
        return DateTime(universalTicks, DateTimeKind::Utc);
    }

    double DateTime::ToOADate() const
    {
        int64_t ticks = _ticks;

        // Дата 0001-01-01 00:00:00 (0 тиков) в OLE соответствует 0.0.
        if (ticks == 0)
            return 0.0;

        // Минимум для OLE Automation Date: 01.01.0100 00:00:00 (36159 дней * 864000000000 тиков).
        static constexpr int64_t OADateMinAsTicks = 31241376000000000LL;
        if (ticks < OADateMinAsTicks)
        {
            throw std::overflow_error("DateTime: ticks are too small for OLE Automation Date.");
        }

        // Смещение эпохи OLE (30.12.1899) относительно 01.01.0001 в миллисекундах.
        static constexpr int64_t DoubleDateOffset = 59926435200000LL;
        static constexpr int64_t MillisPerDay = 86400000LL;

        int64_t millis = ticks / TICKS_PER_MILLISECOND;
        int64_t diffMillis = millis - DoubleDateOffset;
        if (diffMillis < 0)
        {
            int64_t fraction = diffMillis % MillisPerDay;
            if (fraction != 0)
            {
                diffMillis -= (MillisPerDay + fraction) * 2;
            }
        }

        return static_cast<double>(diffMillis) / MillisPerDay;
    }

    DateTime DateTime::FromOADate(double value)
    {
        // Проверка границ (01.01.0100 - 31.12.9999) и NaN.
        if (std::isnan(value) || value >= 2958466.0 || value <= -657435.0)
        {
            throw std::out_of_range("DateTime: OADate is out of range (must be between 0100 and 10000 year).");
        }

        static constexpr int64_t MillisPerDay = 86400000LL;
        static constexpr int64_t DoubleDateOffsetMillis = 59926435200000LL;

        // Конвертируем double в миллисекунды с округлением.
        int64_t millis = static_cast<int64_t>(value * static_cast<double>(MillisPerDay) + (value >= 0.0 ? 0.5 : -0.5));

        // Логика коррекции для отрицательных дат:
        // в OLE дробная часть отрицательного числа идет "вперед" от начала суток.
        if (millis < 0)
        {
            int64_t remainder = millis % MillisPerDay;
            if (remainder != 0)
            {
                millis -= remainder * 2;
            }
        }

        int64_t ticks = (millis + DoubleDateOffsetMillis) * TICKS_PER_MILLISECOND;
        return DateTime(ticks, DateTimeKind::Unspecified);
    }

    //////////////////////////////////////////////////////////////////////
    ///                     Операторы сравнения                        ///
    //////////////////////////////////////////////////////////////////////
    bool DateTime::operator==(const DateTime& value) const { return _ticks == value._ticks; }
    bool DateTime::operator!=(const DateTime& value) const { return _ticks != value._ticks; }
    bool DateTime::operator<(const DateTime& value)  const { return _ticks < value._ticks; }
    bool DateTime::operator>(const DateTime& value)  const { return _ticks > value._ticks; }
    bool DateTime::operator<=(const DateTime& value) const { return _ticks <= value._ticks; }
    bool DateTime::operator>=(const DateTime& value) const { return _ticks >= value._ticks; }

    //////////////////////////////////////////////////////////////////////
    ///                   Арифметические операторы                     ///
    //////////////////////////////////////////////////////////////////////
    DateTime DateTime::operator+(const DateTime& value) const { return DateTime(CheckedAddTicks(_ticks, value._ticks), _kind); }
    DateTime DateTime::operator-(const DateTime& value) const { return DateTime(CheckedSubTicks(_ticks, value._ticks), _kind); }
    DateTime DateTime::operator+(const TimeSpan& value) const { return DateTime(CheckedAddTicks(_ticks, value.Ticks()), _kind); }
    DateTime DateTime::operator-(const TimeSpan& value) const { return DateTime(CheckedSubTicks(_ticks, value.Ticks()), _kind); }

    //////////////////////////////////////////////////////////////////////
    ///           Арифметические операторы с присваиванием             ///
    //////////////////////////////////////////////////////////////////////
    DateTime& DateTime::operator+=(const DateTime& value)
    {
        _ticks = CheckedAddTicks(_ticks, value._ticks);
        return *this;
    }

    DateTime& DateTime::operator-=(const DateTime& value)
    {
        _ticks = CheckedSubTicks(_ticks, value._ticks);
        return *this;
    }

    DateTime& DateTime::operator+=(const TimeSpan& value)
    {
        _ticks = CheckedAddTicks(_ticks, value.Ticks());
        return *this;
    }

    DateTime& DateTime::operator-=(const TimeSpan& value)
    {
        _ticks = CheckedSubTicks(_ticks, value.Ticks());
        return *this;
    }

    DateTime& DateTime::operator=(const DateTime& value)
    {
        if (this != &value)
        {
            _ticks = value._ticks;
            _kind  = value._kind;
        }
        return *this;
    }

    DateTime& DateTime::operator=(DateTime&& value) noexcept
    {
        if (this != &value)
        {
            _ticks = value._ticks;
            _kind  = value._kind;

            // "Сбрасываем" временный объект в валидное состояние.
            value._ticks = 0;
            value._kind  = DateTimeKind::Unspecified;
        }
        return *this;
    }

    std::ostream& operator<<(std::ostream& os, const DateTime& ts)
    {
        os << ts.ToString();
        return os;
    }

    std::istream& operator>>(std::istream& is, DateTime& ts)
    {
        std::string firstToken;
        if (!(is >> firstToken))
            return is;

        // Если первый токен не содержит время (нет ':'), проверяем,
        // не следует ли на той же строке время через пробел (формат "G": "DD.MM.YYYY HH:mm:ss").
        if (firstToken.find(':') == std::string::npos && is.good())
        {
            const int nextCh = is.peek();
            if (nextCh == ' ' || nextCh == '\t')
            {
                std::streampos savedPos = is.tellg();
                // Пропускаем только горизонтальные пробелы, не переходя на следующую строку.
                while (is.good() && (is.peek() == ' ' || is.peek() == '\t'))
                {
                    is.get();
                }

                const int afterSpaces = is.good() ? is.peek() : std::char_traits<char>::eof();
                if (afterSpaces != std::char_traits<char>::eof() &&
                    std::isdigit(static_cast<unsigned char>(afterSpaces)))
                {
                    std::string secondToken;
                    if (is >> secondToken)
                    {
                        DateTime combined;
                        if (secondToken.find(':') != std::string::npos &&
                            DateTime::TryParse(firstToken + " " + secondToken, combined))
                        {
                            ts = combined;
                            return is;
                        }
                    }
                }

                // Откатываем позицию, если поток поддерживает seek, и сбрасываем возможный eofbit/failbit от чтения.
                if (savedPos != std::streampos(-1))
                {
                    is.clear();
                    is.seekg(savedPos);
                }
                else
                {
                    is.clear();
                }
            }
        }

        if (!DateTime::TryParse(firstToken, ts))
        {
            is.setstate(std::ios::failbit);
        }
        return is;
    }

    /**
     * @brief Внутренняя вспомогательная функция для быстрой сборки стандартных форматов.
     */
    std::string InternalFormatByChar(const DateTime& dt, char format)
    {
        std::ostringstream oss;

        int year   = dt.Year();
        int month  = dt.Month();
        int day    = dt.Day();
        int hour   = dt.Hour();
        int minute = dt.Minute();
        int second = dt.Second();
        int dow    = static_cast<int>(dt.GetDayOfWeek());

        auto format_date_short = [&]() {
            std::ostringstream s;
            s << std::setfill('0')
              << std::setw(2) << day   << "."
              << std::setw(2) << month << "."
              << std::setw(4) << year;
            return s.str();
        };

        auto format_date_long = [&]() {
            std::ostringstream s;
            s << long_days_table[dow] << ", " << day << " " << long_months_table[month - 1] << " " << year;
            return s.str();
        };

        auto format_time_short = [&]() {
            std::ostringstream s;
            s << std::setfill('0')
              << std::setw(2) << hour << ":"
              << std::setw(2) << minute;
            return s.str();
        };

        auto format_time_long = [&]() {
            std::ostringstream s;
            s << std::setfill('0')
              << std::setw(2) << hour   << ":"
              << std::setw(2) << minute << ":"
              << std::setw(2) << second;
            return s.str();
        };

        switch (format)
        {
        case 'd': // Short date: "28.12.2025"
            return format_date_short();

        case 'D': // Long date: "Sunday, 28 December 2025"
            return format_date_long();

        case 't': // Short time: "22:04"
            return format_time_short();

        case 'T': // Long time: "22:04:59"
            return format_time_long();

        case 'g': // General short: "28.12.2025 22:04"
            return format_date_short() + " " + format_time_short();

        case 'G': // General long: "28.12.2025 22:04:59"
            return format_date_short() + " " + format_time_long();

        case 'f': // Full date/short time: "Sunday, 28 December 2025 22:04"
            return format_date_long() + " " + format_time_short();

        case 'F': // Full date/long time: "Sunday, 28 December 2025 22:04:59"
            return format_date_long() + " " + format_time_long();

        case 'm':
        case 'M': // Month/Day: "28 December"
            oss << day << " " << long_months_table[month - 1];
            return oss.str();

        case 'y':
        case 'Y': // Year Month: "December 2025"
            oss << long_months_table[month - 1] << " " << year;
            return oss.str();

        case 's': // Sortable (ISO 8601): "2025-12-28T22:04:59"
            oss << std::setfill('0')
                << std::setw(4) << year   << "-"
                << std::setw(2) << month  << "-"
                << std::setw(2) << day    << "T"
                << std::setw(2) << hour   << ":"
                << std::setw(2) << minute << ":"
                << std::setw(2) << second;
            return oss.str();

        case 'u': // Universal sortable: "2025-12-28 22:04:59Z"
            oss << std::setfill('0')
                << std::setw(4) << year   << "-"
                << std::setw(2) << month  << "-"
                << std::setw(2) << day    << " "
                << std::setw(2) << hour   << ":"
                << std::setw(2) << minute << ":"
                << std::setw(2) << second << "Z";
            return oss.str();

        case 'r':
        case 'R': // RFC1123 (Всегда на английском!)
        {
            oss << short_days_table[dow] << ", "
                << std::setfill('0') << std::setw(2) << day << " "
                << short_months_table[month - 1] << " "
                << std::setw(4) << year << " " << format_time_long() << " GMT";
            return oss.str();
        }

        default: // По умолчанию возвращаем короткую дату 'd'
            return format_date_short();
        }
    }
}
