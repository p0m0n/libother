#include "DateTime.h"
#include "private.hpp"


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
            // Если вышли за границы — выбрасываем исключение.
            throw std::out_of_range("DateTime ticks are out of range.");
        }
    }

    // Делегирующий конструктор (Г.М.Д).
    DateTime::DateTime(int year, int month, int day) 
	: DateTime(year, month, day, 0, 0, 0, 0, DateTimeKind::Unspecified) // Перенаправляем вызов в полный конструктор.
    {
    }

    // Полный конструктор со всеми компонентами.
    DateTime::DateTime(int year, int month, int day, int hour, int minute, int second, int millisecond, const DateTimeKind& kind) : _kind(kind)
    {
        // Базовая проверка корректности месяца и года.
        if ((year < 1 || year > 9999) || (month < 1 || month > 12))
            throw std::out_of_range("Year or Month out of range.");

        // Выбираем таблицу дней (високосный год или нет).
        const int* days = IsLeapYear(year) ? DaysToMonth366 : DaysToMonth365;

        // Проверка диапазона дней в конкретном месяце.
        if (day < 1 || day >(days[month] - days[month - 1]))
            throw std::out_of_range("Day out of range.");

        // Проверка диапазона времени.
        if ((hour < 0 || hour >= 24) || (minute < 0 || minute >= 60) || (second < 0 || second >= 60) || (millisecond < 0 || millisecond >= 1000))
            throw std::out_of_range("Time components out of range.");

        // Формула: дни за полные года + поправки на високосные года + дни в текущем году.
        year -= 1;
        int nday = year * 365 + year / 4 - year / 100 + year / 400 + days[month - 1] + day - 1;

        int64_t ticks = 0;
		// Макрос для преобразования дней, часов и мс в общую сумму тиков.
        ADDED_TICKS(ticks, nday, hour, minute, second, millisecond);

        // Проверка границ допустимого диапазона.
        if (ticks < MinTicksDateTime || ticks > MaxTicksDateTime)
        {
            // Если вышли за границы — выбрасываем исключение.
            throw std::out_of_range("Ticks are out of range.");
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
        // "Сбрасываем" временный объект.
        value._ticks = 0;
        value._kind  = static_cast<DateTimeKind>(0);
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
        return DateTime(ticks + TICKS_UNIX_EPOCH, DateTimeKind::Utc);
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
        // Получаем общее количество дней с 01.01.0001
        int totalDays = static_cast<int>(_ticks / TICKS_PER_DAY);

        // Вычисляем год (аналогично логике Years())
        int year = Year();

        // Находим день года (1-366)
        // Вычитаем из общего кол-ва дней все дни полных лет до текущего
        int y = year - 1;
        int daysBeforeYear = y * 365 + y / 4 - y / 100 + y / 400;
        int dayOfYear = totalDays - daysBeforeYear; // результат 0-365

        // Определяем месяц по массиву накопленных дней
        const int* daysToMonth = DateTime::IsLeapYear(year) ? DaysToMonth366 : DaysToMonth365;

        // Ищем в массиве, в какой промежуток попадает dayOfYear
        for (int i = 1; i <= 12; i++)
        {
            if (dayOfYear <= daysToMonth[i])
            {
                return static_cast<MonthOfYear>(i);
            }
        }

        return MonthOfYear::December; // На случай ошибки
    }

    DateTime DateTime::Date() const
    {
        // Убираем остаток от деления на количество тиков в сутках
        return DateTime(_ticks - (_ticks % TICKS_PER_DAY), _kind);
    }

    TimeSpan DateTime::TimeOfDay() const
    {
        return TimeSpan(_ticks % TICKS_PER_DAY);
    }

    DateTime DateTime::Add(const TimeSpan& value) const
    {
        // Важно: передать _kind, чтобы после сложения UTC-время оставалось UTC-временем.
        return DateTime(_ticks + value.Ticks(), _kind);
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
        return DateTime(_ticks + value, _kind);
    }

    DateTime DateTime::AddMonths(int months) const
    {
        if (months < -120000 || months > 120000)
            throw std::out_of_range("Months out of range");

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

        // Если в целевом месяце меньше дней (например, был 31 день, стал 28)
        int daysInMonth = DateTime::DaysInMonth(year, month);
        if (day > daysInMonth)
            day = daysInMonth;

        return DateTime(year, month, day, Hour(), Minute(), Second(), Millisecond(), _kind);
    }

    DateTime DateTime::AddYears(int years) const
    {
        if (years < -10000 || years > 10000)
            throw std::out_of_range("Years out of range");

        // Получаем компоненты.
        int year  = Year();
        int month = Month();
        int day   = Day();

        // Рассчитываем новый год.
        int newYear = year + years;

        // Обработка 29 февраля: если в новом году нет 29-го числа, берем 28-е.
        if (month == 2 && day == 29 && !DateTime::IsLeapYear(newYear))
        {
            day = 28;
        }

        // Собираем новую дату, сохраняя время и Kind.
        return DateTime(newYear, month, day, Hour(), Minute(), Second(), Millisecond(), _kind);
    }

    DateTime DateTime::Subtract(const TimeSpan& value) const
    {
        return DateTime(_ticks - value.Ticks(), _kind);
    }

    TimeSpan DateTime::Subtract(const DateTime& value) const
    {
        return TimeSpan(_ticks - value._ticks);
    }

    DateTime DateTime::ToLocalTime() const 
    {
        if (_kind == DateTimeKind::Local) 
            return *this;

        // Конвертируем тики нашего объекта в unix timestamp.
        std::time_t utc_t = static_cast<std::time_t>(ToUnixTimeSeconds());
        std::tm ltm = { 0 };

#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
        localtime_s(&ltm, &utc_t);
#elif LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_UNIX
        localtime_r(&utc_t, &ltm);
#endif
        // Создаем объект из компонентов, которые нам дала система (уже локальных)
        // Важно: передаем DateTimeKind::Local
        DateTime local(ltm.tm_year + 1900, ltm.tm_mon + 1, ltm.tm_mday, ltm.tm_hour, ltm.tm_min, ltm.tm_sec, 0, DateTimeKind::Local);

        // ВОЗВРАЩАЕМ ТОЧНОСТЬ: добавляем остаток тиков, которые меньше одной секунды.
        int64_t result = _ticks % TICKS_PER_SECOND;
        return DateTime(local.Ticks() + result, DateTimeKind::Local);
    }

    DateTime DateTime::ToUniversalTime() const
    {
        // Если время уже UTC, возвращаем как есть.
        if (_kind == DateTimeKind::Utc)
            return *this;

        // Заполняем структуру tm компонентами.
        std::tm ltm = { 0 };
        ltm.tm_year = Year() - 1900;
        ltm.tm_mon  = Month() - 1;
        ltm.tm_mday = Day();
        ltm.tm_hour = Hour();
        ltm.tm_min  = Minute();
        ltm.tm_sec  = Second();

        // -1 заставляет систему саму определить, действовал ли летний переход  именно в ЭТУ дату (например, 5 лет назад).
        ltm.tm_isdst = -1;

        // mktime берет локальное время и возвращает UTC timestamp (секунды)
        std::time_t utc_t = std::mktime(&ltm);
        if (utc_t == static_cast<std::time_t>(-1))
            return *this;
        
        // Переводим секунды обратно в тики.
        int64_t ticks = FromUnixTimeSeconds(utc_t).Ticks();

        // ВОЗВРАЩАЕМ ТОЧНОСТЬ: добавляем остаток тиков, которые меньше одной секунды.
        ticks += (_ticks % TICKS_PER_SECOND);

        return DateTime(ticks, DateTimeKind::Utc);
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
        // Смешиваем тики и тип времени
        return std::hash<int64_t>{}(_ticks) ^ (static_cast<size_t>(_kind) << 2);
    }

    bool DateTime::IsDaylightSavingTime() const
    {
        // В UTC времени переходов на летнее время не существует
        if (_kind == DateTimeKind::Utc)
            return false;

        // Конвертируем Ticks в Unix timestamp (секунды)
        // Используем константы TICKS_UNIX_EPOCH и TICKS_PER_SECOND
        std::time_t time = static_cast<std::time_t>(ToUnixTimeSeconds());

        std::tm ltm = { 0 };

#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
        if (localtime_s(&ltm, &time) != 0)
            return false;
#elif LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_UNIX
        // POSIX версия для Linux/macOS
        if (localtime_r(&time, &ltm) == nullptr)
            return false;
#endif

        // Анализируем флаг летнего времени
        // tm_isdst > 0 означает, что летнее время активно
        return ltm.tm_isdst > 0;
    }

    int DateTime::DaysInMonth(int year, int month)
    {
        // Проверка месяца.
        if (month < 1 || month > 12)
            return 0;

        // Проверка года.
        if (year < 1 || year > 9999) 
            return 0;

        // Выбираем нужный массив (високосный/обычный)
        const int* daysToMonth = DateTime::IsLeapYear(year) ? DaysToMonth366 : DaysToMonth365;

        // Количество дней в месяце — это разница между текущим и предыдущим накопленным значением
        return daysToMonth[month] - daysToMonth[month - 1];
    }

    bool DateTime::IsLeapYear(int year)
    {
        // Год високосный, если делится на 4, но не на 100, либо делится на 400
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

    std::string DateTime::ToString() const
    {
        return InternalFormatByChar(*this, 'G');
    }

    std::string DateTime::ToString(const std::string& format) const
    {
        if (format.empty())
        {
            // Вызов стандартного ДД.ММ.ГГГГ ЧЧ:ММ:СС
            return InternalFormatByChar(*this, 'G');
        }

        // Если передана строка из одного символа, вызываем быстрый switch
        if (format.length() == 1)
        {
            return InternalFormatByChar(*this, format[0]);
        }


        std::string result = "";
        for (size_t i = 0; i < format.length(); i++)
        {
            // Проверяем маски от самых длинных к коротким

            // Год
            if (format.compare(i, 4, "yyyy") == 0)
            {
                result += std::to_string(Year());
                i += 3;
            }
            else if (format.compare(i, 2, "yy") == 0)
            {
                int y = Year() % 100;
                result += (y < 10 ? "0" : "") + std::to_string(y);
                i += 1;
            }
            // Месяц
            else if (format.compare(i, 2, "MM") == 0)
            {
                int m = Month();
                result += (m < 10 ? "0" : "") + std::to_string(m);
                i += 1;
            }
            // День
            else if (format.compare(i, 2, "dd") == 0)
            {
                int d = Day();
                result += (d < 10 ? "0" : "") + std::to_string(d);
                i += 1;
            }
            // Часы
            else if (format.compare(i, 2, "HH") == 0)
            {
                int h = Hour();
                result += (h < 10 ? "0" : "") + std::to_string(h);
                i += 1;
            }
            // Минуты
            else if (format.compare(i, 2, "mm") == 0)
            {
                int m = Minute();
                result += (m < 10 ? "0" : "") + std::to_string(m);
                i += 1;
            }
            // Секунды
            else if (format.compare(i, 2, "ss") == 0)
            {
                int s = Second();
                result += (s < 10 ? "0" : "") + std::to_string(s);
                i += 1;
            }
            // Если ничего не подошло — просто копируем символ (пробел, точку, двоеточие или букву)
            else
            {
                result += format[i];
            }
        }
        return result;
    }

    std::vector<std::string> DateTime::DateTimeFormats() const
    {
        static const char formats[] = { 'd', 'D', 't', 'T', 'g', 'G', 'f', 'F', 'm', 'y', 's', 'u','r' };

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

        return DateTime::Zero;
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
                // Поиск месяца.
                for (int i = 0; i < 12; ++i)
                {
                    if (input.find(short_months_table[i]) != std::string::npos)
                    {
                        month = i + 1;
                        break;
                    }
                }

                if (month != 0)
                {
                    // Получаем компоненты дат и времени.
                    day    = std::stoi(input.substr(5, 2));
                    year   = std::stoi(input.substr(12, 4));
                    hour   = std::stoi(input.substr(17, 2));
                    minute = std::stoi(input.substr(20, 2));
                    second = std::stoi(input.substr(23, 2));

                    // Миллисекунды если есть(.fff)
                    size_t dot = input.find('.', 18);
                    if (dot != std::string::npos && input.length() > dot + 3)
                    {
                        millisecond = std::stoi(input.substr(dot + 1, 3));
                    }

                    result = DateTime(year, month, day, hour, minute, second, millisecond, DateTimeKind::Utc);
                    return true;
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

                if (input.length() >= 19 && (input[10] == 'T' || input[10] == ' '))
                {
                    hour   = std::stoi(input.substr(11, 2));
                    minute = std::stoi(input.substr(14, 2));
                    second = std::stoi(input.substr(17, 2));

                    // Миллисекунды если есть(.fff)
                    size_t dot = input.find('.', 18);
                    if (dot != std::string::npos && input.length() > dot + 3)
                    {
                        millisecond = std::stoi(input.substr(dot + 1, 3));
                    }
                }

                // Обработка Z или смещения (+03:00)
                int64_t offsetTicks = 0;
                DateTimeKind kind = DateTimeKind::Unspecified;
                if (input.back() == 'Z')
                {
                    kind = DateTimeKind::Utc;
                }
                else
                {
                    size_t signPos = input.find_first_of("+-", 10);
                    if (signPos != std::string::npos && input.length() >= signPos + 5)
                    {
                        int offH = std::stoi(input.substr(signPos + 1, 2));
                        int offM = std::stoi(input.substr(signPos + 4, 2));

                        offsetTicks = (offH * TICKS_PER_HOUR) + (offM * TICKS_PER_MINUTE);
                        if (input[signPos] == '+')
                            offsetTicks = -offsetTicks; // UTC = Local - Offset
                        kind = DateTimeKind::Utc;
                    }
                }

                result = DateTime(DateTime(year, month, day, hour, minute, second, millisecond, kind).Ticks() + offsetTicks, kind);
                return true;
            }
            
            // --- 3. Текстовые форматы (Monday, 12 January 2026) ---
            int foundMonthIdx = FindIndex(input); // Поиск месяца
            if (foundMonthIdx != -1)
            {
                month = foundMonthIdx + 1;
                std::istringstream iss(input);
                std::string word;

                // Перебираем слова и числа
                while (iss >> word) 
                {
                    if (isdigit(word[0])) 
                    {
                        int val = std::stoi(word);
                        if (val > 100) 
                            year = val; // Год
                        else if (day == 0) 
                            day = val;  // День
                        else if (word.find(':') != std::string::npos) 
                        {
                            // Если в слове есть ':', пробуем достать время
                            m_sscanf(word.c_str(), "%2d:%2d:%2d", &hour, &minute, &second);
                        }
                    }
                }

                // Дозаполняем отсутствующее
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
                if (m_sscanf(input.c_str(), "%d:%d:%d", &hour, &minute, &second) >= 2)
                {
                    DateTime now = DateTime::Now();

                    // Для чистого времени используем сегодняшнюю дату.
                    result = DateTime(now.Year(), now.Month(), now.Day(), hour, minute, second);
                    return true;
                }
            }

            // --- ПРОВЕРКА 5: Стандартный числовой (DD.MM.YYYY) ---
            std::istringstream iss(input);
            if (iss >> day >> sep1 >> month >> sep2 >> year)
            {
                // Проверяем, что разделители даты одинаковые и входят в список разрешенных (DD.MM.YYYY, DD-MM-YYYY)
                if (sep1 != sep2 || !IsDateSeparator(sep1))
                    return false;

                // Считываем часы и минуты
                if (iss >> hour >> col1 >> minute)
                {
                    // Считываем секунды если они есть.
                    if (IsTimeSeparator(col1))
                    {
                        if (iss >> col2 >> second)
                        {
                            // Секунды считаны
                        }
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

        return false;
    }

    DateTime DateTime::ParseExact(const std::string& input, const std::string& format)
    {
        DateTime result;
        if (TryParseExact(input, format, result))
            return result;

        return DateTime::Zero;
    }
	
	DateTime DateTime::ParseExact(const std::string& input, const std::vector<std::string>& formats)
    {
        DateTime result;

        // Перебираем каждый формат в векторе
        for (const std::string& format : formats)
        {
            // Используем уже реализованный TryParseExact для проверки без исключений
            if (TryParseExact(input, format, result))
            {
                // Как только нашли совпадение — возвращаем результат
                return result;
            }
        }

        return DateTime::Zero;
    }

    bool DateTime::TryParseExact(const std::string& input, const std::string& format, DateTime& result)
    {
        if (input.empty() || format.empty())
            return false;

        try
        {
            // Подготовка формата (преобразование токенов)
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

            std::tm tm = { 0 };

#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
            std::istringstream ss(input);
            ss >> std::get_time(&tm, cFormat.c_str());
            if (ss.fail())
            {
                result = DateTime::Zero;
                return false;
            }
#elif LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_UNIX || LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_APPLE
            char* res = strptime(input.c_str(), cFormat.c_str(), &tm);
            if (res == nullptr)
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
        // Если входная строка или список форматов пусты, парсинг невозможен
        if (input.empty() || formats.empty())
            return false;
        
        // Перебор форматов
        for (const std::string& format : formats)
        {
            // Пропускаем пустые строки форматов внутри вектора
            if (format.empty()) 
                continue;

            // Вызываем атомарный TryParseExact для конкретного формата
            if (DateTime::TryParseExact(input, format, result))
            {
                // Если успех, переменная result уже содержит значение
                return true;
            }
        }

        // Если цикл завершился и ни один формат не подошел
        return false;
    }

    int64_t DateTime::ToUnixTimeSeconds() const
    {
        // Вычитаем эпоху и переводим тики в секунды
        int64_t seconds = ((_ticks - TICKS_UNIX_EPOCH) / TICKS_PER_SECOND);
        return seconds;
    }
	
	int64_t DateTime::ToUnixTimeMilliseconds() const
	{
		// Вычитаем эпоху и переводим тики в секунды
        int64_t milliseconds = ((_ticks - TICKS_UNIX_EPOCH) / TICKS_PER_MILLISECOND);
        return milliseconds;
	}
	
    DateTime DateTime::FromUnixTimeSeconds(int64_t seconds)
    {
        // Обратный процесс
        int64_t ticks = ((seconds * TICKS_PER_SECOND) + TICKS_UNIX_EPOCH);
        return DateTime(ticks, DateTimeKind::Utc);
    }
	
	DateTime DateTime::FromUnixTimeMilliseconds(int64_t milliseconds)
	{
		// Обратный процесс
        int64_t ticks = ((milliseconds * TICKS_PER_MILLISECOND) + TICKS_UNIX_EPOCH);
        return DateTime(ticks, DateTimeKind::Utc);
	}

    int64_t DateTime::ToBinary() const
    {
        // Вычитаем 1, чтобы привести (1, 2, 3) к стандарту (0, 1, 2)
        // 1 (Unspecified) -> 0 (00 в битах 62-63)
        // 2 (Utc)         -> 1 (01 в битах 62-63)
        // 3 (Local)       -> 2 (10 в битах 62-63, где 63-й бит — флаг Local)
        int64_t kindValue = static_cast<int64_t>(_kind) - 1;
        int64_t kindData = kindValue << 62;

        // Маска 0x3FFFFFFFFFFFFFFF гарантирует, что мы используем только 62 бита для тактов
        return (kindData | (_ticks & 0x3FFFFFFFFFFFFFFFLL));
    }

    DateTime DateTime::FromBinary(int64_t dateData)
    {
        // Извлекаем 2 верхних бита (62 и 63)
        int64_t kindRaw = (dateData >> 62) & 0x03;

        // Извлекаем тики (нижние 62 бита)
        int64_t ticks = dateData & 0x3FFFFFFFFFFFFFFFLL;

        // Возвращаем смещение (+1), чтобы получить enum (1, 2, 3)
        DateTimeKind kind = static_cast<DateTimeKind>(static_cast<int>(kindRaw) + 1);

        return DateTime(ticks, kind);
    }

    int64_t DateTime::ToFileTime() const
    {
        return ToFileTimeUtc();
    }

    DateTime DateTime::FromFileTime(int64_t fileTime)
    {
        // Получаем UTC время и конвертируем его в локальное
        return FromFileTimeUtc(fileTime).ToLocalTime();
    }

    int64_t DateTime::ToFileTimeUtc() const
    {
        int64_t ticks = _ticks;

        // Если время локальное, мы ОБЯЗАНЫ привести его к UTC перед сохранением в FileTime,
        // так как стандарт Windows File Time не поддерживает локальные смещения.
        // Примечание: если Kind == Unspecified (1), трактуем его как UTC
        if (_kind == DateTimeKind::Local)
        {
            ticks = ToUniversalTime().Ticks();
        }

        int64_t result = ticks - FILE_TIME_OFFSET;

        // Согласно спецификации, если результат отрицательный (дата до 1601 года), выкидывается исключение или возвращается 0.
        return (result < 0) ? 0 : result;
    }

    DateTime DateTime::FromFileTimeUtc(int64_t fileTime)
    {
        // Проверка на отрицательное значение
        if (fileTime < 0)
            throw std::out_of_range("fileTime is less than 0.");

        try 
        {
            // Добавляем смещение к времени файла, чтобы получить системные тики
            int64_t universalTicks = fileTime + FILE_TIME_OFFSET;

            // Проверяем верхнюю границу (MaxTicks)
            if (universalTicks > MaxTicksDateTime)
                throw std::out_of_range("fileTime exceeds MaxValue.");

            // Возвращаем объект с флагом Utc
            return DateTime(universalTicks, DateTimeKind::Utc);
        }
        catch (...) 
        {
            // Выбрасываем ошибку переполнения
            throw std::out_of_range("fileTime is too large for DateTime.");
        }
    }

    double DateTime::ToOADate() const
    {
        int64_t ticks = _ticks;

        // Проверка диапазона (OADate не поддерживает даты раньше 01.01.0100)
        // Минимум: 315537888000000000 тактов (100 год)
        if (ticks < 315537888000000000LL) 
        {
            throw std::overflow_error("Ticks are too small for OLE Automation Date.");
        }

        // Если дата 0001-01-01, OLE считает это нулевым значением в некоторых контекстах, то возвращаем 0.0 именно для 30.12.1899.
        if (ticks == 0) 
			return 0.0;

        // Смещение эпохи OLE (30.12.1899) относительно 01.01.0001 в миллисекундах
        static const int64_t DoubleDateOffset = 59926435200000LL;
        static const int64_t MillisPerDay = 86400000LL;

        // Переводим текущие тики в миллисекунды
        int64_t millis = ticks / TICKS_PER_MILLISECOND;

        // Вычисляем разницу относительно эпохи OLE
        int64_t diffMillis = millis - DoubleDateOffset;
        if (diffMillis < 0) 
        {
            // Специфическая логика OLE для отрицательных дат:
            // Дробная часть должна представлять время дня, поэтому нельзя просто 
            // разделить на MillisPerDay.
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
        // Проверка границ (01.01.0100 - 31.12.9999)
        // Эти числа соответствуют минимальному и максимальному значению OADate.
        if (value >= 2958466.0 || value <= -657435.0) 
        {
            throw std::out_of_range("OADate is out of range (must be between 0100 and 10000 year).");
        }

        // Конвертируем double в миллисекунды с округлением, 86400000 — количество миллисекунд в сутках.
        int64_t millis = static_cast<int64_t>(value * 86400000.0 + (value >= 0 ? 0.5 : -0.5));

        // Логика коррекции для отрицательных дат
        if (millis < 0) 
        {
            int64_t remainder = millis % 86400000LL;
            if (remainder != 0) 
            {
                // В OLE-датах дробная часть отрицательного числа — это время, которое идет "вперед" от начала этих суток. 
                // Мы корректируем миллисекунды, чтобы они соответствовали линейному времени.
                millis -= (86400000LL + remainder) * 2;
            }
        }

        // Добавляем смещение эпохи (30.12.1899) к полученным миллисекундам
        // DoubleDateOffset в миллисекундах: 59926435200000
        static const int64_t DoubleDateOffsetMillis = 59926435200000LL;

        // Переводим миллисекунды в тики (1 мс = 10 000 тактов)
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
    DateTime DateTime::operator+(const DateTime& value) const { return DateTime((_ticks + value._ticks), _kind); }
    DateTime DateTime::operator-(const DateTime& value) const { return DateTime((_ticks - value._ticks), _kind); }
    DateTime DateTime::operator+(const TimeSpan& value) const { return DateTime((_ticks + value.Ticks()), _kind); }
    DateTime DateTime::operator-(const TimeSpan& value) const { return DateTime((_ticks - value.Ticks()), _kind); }
    //////////////////////////////////////////////////////////////////////
    ///           Арифметические операторы с присваиванием             ///
    //////////////////////////////////////////////////////////////////////
    DateTime& DateTime::operator+=(const DateTime& value) 
    {
        if (this != &value)
            _ticks += value._ticks;
        return *this;
    }
    DateTime& DateTime::operator-=(const DateTime& value)
    { 
        if (this != &value)
            _ticks -= value._ticks; 
        return *this; 
    }
    DateTime& DateTime::operator+=(const TimeSpan& value) { _ticks += value.Ticks(); return *this; }
    DateTime& DateTime::operator-=(const TimeSpan& value) { _ticks -= value.Ticks(); return *this; }
    DateTime& DateTime::operator=(const DateTime& value)
    {
        if (this != &value)
        {
            // Копируем значение из временного объекта.
            _ticks = value._ticks;
            _kind  = value._kind;
        }
        return *this;
    }
    DateTime& DateTime::operator=(DateTime&& value) noexcept
    {
        if (this != &value)
        {
            // Копируем значение из временного объекта.
            _ticks = value._ticks;
            _kind  = value._kind;

            // "Сбрасываем" временный объект.
            value._ticks = 0;
            value._kind  = static_cast<DateTimeKind>(0);
        }
        return *this;
    }

    std::ostream& operator<<(std::ostream& os, const DateTime& ts)
    {
        // Мы просто передаем строковое представление в поток.
        // Если ToString() возвращает формат [d.]hh:mm:ss[.fffffff], 
        // он будет выведен и здесь.
        os << ts.ToString();

        // Обязательно возвращаем поток, чтобы можно было писать:
        // std::cout << ts1 << " и " << ts2;
        return os;
    }

    std::istream& operator>>(std::istream& is, DateTime& ts)
    {
        std::string temp;
        // Считываем строку из потока до первого пробельного символа
        if (is >> temp)
        {
            // Пытаемся распарсить полученную строку
            if (!DateTime::TryParse(temp, ts))
            {
                // Если формат неверный, выставляем failbit.
                // Это стандартное поведение для C++ при ошибке ввода.
                is.setstate(std::ios::failbit);
            }
        }
        return is;
    }

    /**
     * @brief Внутренняя вспомогательная функция для быстрой сборки стандартных форматов.
     */
    std::string InternalFormatByChar(const DateTime& dt, char format)
    {
        std::ostringstream oss;

        // Предварительно получаем все компоненты, чтобы не вызывать методы многократно
        int year   = dt.Year();
        int month  = dt.Month();
        int day    = dt.Day();
        int hour   = dt.Hour();
        int minute = dt.Minute();
        int second = dt.Second();
        int dow    = static_cast<int>(dt.GetDayOfWeek());

        // Вспомогательные лямбда-функции для часто повторяющихся элементов (внутри функции)
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
			<< std::setw(2) << hour    << ":" 
			<< std::setw(2) << minute  << ":" 
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
                << year << " " << format_time_long() << " GMT";
            return oss.str();
        }

        default: // По умолчанию возвращаем короткую дату 'd'
            return format_date_short();
        }
    }
}
