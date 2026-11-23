#include "TimeSpan.h"
#include "private/PrivateTime.hpp"

namespace other::time
{
    // Безопасное преобразование дробного значения в тики с проверкой NaN/Inf и переполнения.
    static TimeSpan FromDoubleInterval(double value, int64_t scaleTicks);
    // Строгий разбор неотрицательного целого числа из строки.
    static bool ParseStrictNonNegativeInt64(const std::string& str, int64_t& out);
    // Разбор дробной части секунд (от 1 до 7 цифр) в тики (100 нс).
    static bool ParseFractionalSecondsToTicks(const std::string& fracStr, int64_t& outTicks);

    // Инициализация нулевого значения (0 тиков).
    const TimeSpan TimeSpan::Zero(0LL);
    // Инициализация максимального значения.
    const TimeSpan TimeSpan::MaxValue(MaxTicksTimeSpan);
    // Инициализация минимального значения.
    const TimeSpan TimeSpan::MinValue(MinTicksTimeSpan);

    // Основной конструктор по тактам.
    TimeSpan::TimeSpan(int64_t ticks) : _ticks(ticks)
    {
    }

    // Делегирующий конструктор (Ч:М:С).
    TimeSpan::TimeSpan(int hours, int minutes, int seconds)
        : TimeSpan(0, hours, minutes, seconds, 0)
    {
    }

    // Полный конструктор со всеми компонентами.
    TimeSpan::TimeSpan(int days, int hours, int minutes, int seconds, int milliseconds)
    {
        int64_t ticks = 0;
        ADDED_TICKS(ticks, days, hours, minutes, seconds, milliseconds);
        _ticks = ticks;
    }

    // Конструктор копирования.
    TimeSpan::TimeSpan(const TimeSpan& value) : _ticks(value._ticks)
    {
    }

    // Конструктор перемещения.
    TimeSpan::TimeSpan(TimeSpan&& value) noexcept : _ticks(value._ticks)
    {
        value._ticks = 0;
    }

    int64_t TimeSpan::Ticks() const    { return _ticks; }
    int TimeSpan::Days() const         { return static_cast<int>(_ticks / TICKS_PER_DAY); }
    int TimeSpan::Hours() const        { return static_cast<int>((_ticks / TICKS_PER_HOUR) % 24); }
    int TimeSpan::Minutes() const      { return static_cast<int>((_ticks / TICKS_PER_MINUTE) % 60); }
    int TimeSpan::Seconds() const      { return static_cast<int>((_ticks / TICKS_PER_SECOND) % 60); }
    int TimeSpan::Milliseconds() const { return static_cast<int>((_ticks / TICKS_PER_MILLISECOND) % 1000); }

    double TimeSpan::TotalDays() const         { return static_cast<double>(_ticks) / TICKS_PER_DAY; }
    double TimeSpan::TotalHours() const        { return static_cast<double>(_ticks) / TICKS_PER_HOUR; }
    double TimeSpan::TotalMinutes() const      { return static_cast<double>(_ticks) / TICKS_PER_MINUTE; }
    double TimeSpan::TotalSeconds() const      { return static_cast<double>(_ticks) / TICKS_PER_SECOND; }
    double TimeSpan::TotalMilliseconds() const { return static_cast<double>(_ticks) / TICKS_PER_MILLISECOND; }

    // Создает новый TimeSpan путем сложения тиков.
    TimeSpan TimeSpan::Add(const TimeSpan& value) const
    {
        return TimeSpan(CheckedAddInt64(_ticks, value._ticks, "TimeSpan: addition overflow."));
    }

    // Создает новый TimeSpan путем вычитания тиков.
    TimeSpan TimeSpan::Subtract(const TimeSpan& value) const
    {
        return TimeSpan(CheckedSubInt64(_ticks, value._ticks, "TimeSpan: subtraction overflow."));
    }

    // Возвращает абсолютное значение.
    TimeSpan TimeSpan::Duration() const
    {
        if (_ticks == MinTicksTimeSpan)
        {
            throw std::out_of_range("TimeSpan: Duration overflow on MinValue.");
        }
        return TimeSpan(_ticks >= 0 ? _ticks : -_ticks);
    }

    // Инвертирует знак (плюс на минус).
    TimeSpan TimeSpan::Negate() const
    {
        if (_ticks == MinTicksTimeSpan)
        {
            throw std::out_of_range("TimeSpan: Negate overflow on MinValue.");
        }
        return TimeSpan(-_ticks);
    }

    // Сравнение двух интервалов (-1, 0, 1).
    int TimeSpan::Compare(const TimeSpan& value) const
    {
        if (_ticks > value._ticks) return 1;
        if (_ticks < value._ticks) return -1;
        return 0;
    }

    bool TimeSpan::Equals(const TimeSpan& value) const
    {
        return (_ticks == value._ticks);
    }

    // Преобразование в строку. Формат: [-][d.]hh:mm:ss[.fff]
    std::string TimeSpan::ToString() const
    {
        std::ostringstream ss;

        if (_ticks < 0)
            ss << "-";

        const uint64_t absTicks = (_ticks < 0)
            ? (0ULL - static_cast<uint64_t>(_ticks))
            : static_cast<uint64_t>(_ticks);

        const uint64_t days         = absTicks / static_cast<uint64_t>(TICKS_PER_DAY);
        const uint64_t hours        = (absTicks / static_cast<uint64_t>(TICKS_PER_HOUR)) % 24ULL;
        const uint64_t minutes      = (absTicks / static_cast<uint64_t>(TICKS_PER_MINUTE)) % 60ULL;
        const uint64_t seconds      = (absTicks / static_cast<uint64_t>(TICKS_PER_SECOND)) % 60ULL;
        const uint64_t milliseconds = (absTicks / static_cast<uint64_t>(TICKS_PER_MILLISECOND)) % 1000ULL;

        if (days > 0)
            ss << days << ".";

        ss << std::setfill('0')
           << std::setw(2) << hours << ":"
           << std::setw(2) << minutes << ":"
           << std::setw(2) << seconds;

        if (milliseconds > 0)
            ss << "." << std::setw(3) << milliseconds;

        return ss.str();
    }

    std::string TimeSpan::ToString(const std::string& format) const
    {
        std::ostringstream ss;

        if (_ticks < 0)
            ss << "-";

        const uint64_t absTicks = (_ticks < 0)
            ? (0ULL - static_cast<uint64_t>(_ticks))
            : static_cast<uint64_t>(_ticks);

        const uint64_t day         = absTicks / static_cast<uint64_t>(TICKS_PER_DAY);
        const uint64_t hour        = (absTicks / static_cast<uint64_t>(TICKS_PER_HOUR)) % 24ULL;
        const uint64_t minute      = (absTicks / static_cast<uint64_t>(TICKS_PER_MINUTE)) % 60ULL;
        const uint64_t second      = (absTicks / static_cast<uint64_t>(TICKS_PER_SECOND)) % 60ULL;
        const uint64_t millisecond = (absTicks / static_cast<uint64_t>(TICKS_PER_MILLISECOND)) % 1000ULL;

        for (size_t i = 0; i < format.length(); ++i)
        {
            if (format[i] == 'd')
            {
                ss << day;
            }
            else if (format.compare(i, 2, "hh") == 0)
            {
                ss << std::setw(2) << std::setfill('0') << hour;
                i++;
            }
            else if (format[i] == 'h')
            {
                ss << hour;
            }
            else if (format.compare(i, 2, "mm") == 0)
            {
                ss << std::setw(2) << std::setfill('0') << minute;
                i++;
            }
            else if (format[i] == 'm')
            {
                ss << minute;
            }
            else if (format.compare(i, 2, "ss") == 0)
            {
                ss << std::setw(2) << std::setfill('0') << second;
                i++;
            }
            else if (format[i] == 's')
            {
                ss << second;
            }
            else if (format.compare(i, 3, "fff") == 0)
            {
                ss << std::setw(3) << std::setfill('0') << millisecond;
                i += 2;
            }
            else
            {
                ss << format[i];
            }
        }

        return ss.str();
    }

    size_t TimeSpan::GetHashCode() const
    {
        return std::hash<int64_t>{}(_ticks);
    }

    TimeSpan TimeSpan::FromDays(double value)         { return FromDoubleInterval(value, TICKS_PER_DAY); }
    TimeSpan TimeSpan::FromHours(double value)        { return FromDoubleInterval(value, TICKS_PER_HOUR); }
    TimeSpan TimeSpan::FromMinutes(double value)      { return FromDoubleInterval(value, TICKS_PER_MINUTE); }
    TimeSpan TimeSpan::FromSeconds(double value)      { return FromDoubleInterval(value, TICKS_PER_SECOND); }
    TimeSpan TimeSpan::FromMilliseconds(double value) { return FromDoubleInterval(value, TICKS_PER_MILLISECOND); }
    TimeSpan TimeSpan::FromTicks(int64_t ticks)       { return TimeSpan(ticks); }

    int TimeSpan::Compare(const TimeSpan& t1, const TimeSpan& t2) { return t1.Compare(t2); }
    bool TimeSpan::Equals(const TimeSpan& t1, const TimeSpan& t2) { return t1.Equals(t2); }

    TimeSpan TimeSpan::Parse(const std::string& input)
    {
        TimeSpan result;
        if (TimeSpan::TryParse(input, result))
            return result;

        throw std::invalid_argument("TimeSpan::Parse: invalid time span string.");
    }

    bool TimeSpan::TryParse(const std::string& input, TimeSpan& result)
    {
        if (input.empty())
            return false;

        size_t pos = 0;
        bool negative = false;

        if (input[pos] == '-')
        {
            negative = true;
            pos = 1;
        }
        else if (input[pos] == '+')
        {
            pos = 1;
        }

        if (pos >= input.size())
            return false;

        try
        {
            std::string body = input.substr(pos);

            // Поддерживаемый формат: [d.]hh:mm[:ss[.fffffff]]
            const size_t firstColon = body.find(':');
            if (firstColon == std::string::npos)
            {
                // Если двоеточий нет — допускаем целое количество дней.
                int64_t daysOnly = 0;
                if (!ParseStrictNonNegativeInt64(body, daysOnly))
                    return false;

                int64_t ticks = CheckedMulPosInt64(daysOnly, TICKS_PER_DAY, "TimeSpan: overflow.");
                if (negative)
                    ticks = -ticks;

                result = TimeSpan(ticks);
                return true;
            }

            int64_t days = 0;
            int64_t hours = 0;
            int64_t minutes = 0;
            int64_t seconds = 0;
            int64_t fracTicks = 0;

            // Проверяем наличие дней перед первым двоеточием (d.hh).
            std::string beforeFirstColon = body.substr(0, firstColon);
            const size_t dayDot = beforeFirstColon.find('.');
            if (dayDot != std::string::npos)
            {
                if (!ParseStrictNonNegativeInt64(beforeFirstColon.substr(0, dayDot), days) ||
                    !ParseStrictNonNegativeInt64(beforeFirstColon.substr(dayDot + 1), hours))
                {
                    return false;
                }
            }
            else
            {
                if (!ParseStrictNonNegativeInt64(beforeFirstColon, hours))
                    return false;
            }

            // Разбираем остаток после первого двоеточия: mm[:ss[.fffffff]]
            std::string afterFirstColon = body.substr(firstColon + 1);
            const size_t secondColon = afterFirstColon.find(':');
            if (secondColon == std::string::npos)
            {
                // Формат [d.]hh:mm
                if (!ParseStrictNonNegativeInt64(afterFirstColon, minutes))
                    return false;
            }
            else
            {
                if (!ParseStrictNonNegativeInt64(afterFirstColon.substr(0, secondColon), minutes))
                    return false;

                std::string secAndFrac = afterFirstColon.substr(secondColon + 1);
                if (secAndFrac.find(':') != std::string::npos)
                    return false;

                const size_t fracDot = secAndFrac.find('.');
                if (fracDot == std::string::npos)
                {
                    if (!ParseStrictNonNegativeInt64(secAndFrac, seconds))
                        return false;
                }
                else
                {
                    if (!ParseStrictNonNegativeInt64(secAndFrac.substr(0, fracDot), seconds) ||
                        !ParseFractionalSecondsToTicks(secAndFrac.substr(fracDot + 1), fracTicks))
                    {
                        return false;
                    }
                }
            }

            if (minutes >= 60 || seconds >= 60 || (dayDot != std::string::npos && hours >= 24))
                return false;

            int64_t ticks = CheckedComponentsToTicks(days, hours, minutes, seconds, 0);
            ticks = CheckedAddInt64(ticks, fracTicks, "TimeSpan: overflow.");

            if (negative)
            {
                if (ticks == MinTicksTimeSpan)
                    return false;
                ticks = -ticks;
            }

            result = TimeSpan(ticks);
            return true;
        }
        catch (...)
        {
            result = TimeSpan::Zero;
            return false;
        }
    }

    TimeSpan TimeSpan::ParseExact(const std::string& input, const std::string& format)
    {
        TimeSpan result;
        if (TryParseExact(input, format, result))
            return result;

        throw std::invalid_argument("TimeSpan::ParseExact: string does not match the specified format.");
    }

    TimeSpan TimeSpan::ParseExact(const std::string& input, const std::vector<std::string>& formats)
    {
        TimeSpan result;
        if (TryParseExact(input, formats, result))
            return result;

        throw std::invalid_argument("TimeSpan::ParseExact: string does not match any of the specified formats.");
    }

    bool TimeSpan::TryParseExact(const std::string& input, const std::string& format, TimeSpan& result)
    {
        if (input.empty() || format.empty())
            return false;

        int64_t days = 0, hours = 0, minutes = 0, seconds = 0, fracTicks = 0;
        size_t inputPos = 0;
        bool negative = false;

        if (input[inputPos] == '-')
        {
            negative = true;
            inputPos = 1;
        }

        auto read_digits = [&](size_t& pos, size_t minDigits, size_t maxDigits, std::string& outDigits) -> bool
        {
            size_t start = pos;
            while (pos < input.length() && (pos - start) < maxDigits &&
                   std::isdigit(static_cast<unsigned char>(input[pos])))
            {
                pos++;
            }

            size_t count = pos - start;
            if (count < minDigits)
                return false;

            outDigits = input.substr(start, count);
            return true;
        };

        auto read_number = [&](size_t& pos, size_t minDigits, size_t maxDigits, int64_t& outVal) -> bool
        {
            std::string digits;
            if (!read_digits(pos, minDigits, maxDigits, digits))
                return false;
            return ParseStrictNonNegativeInt64(digits, outVal);
        };

        try
        {
            for (size_t fPos = 0; fPos < format.length(); ++fPos)
            {
                if (inputPos >= input.length())
                    return false;

                char fChar = format[fPos];

                if (fChar == 'd')
                {
                    if (!read_number(inputPos, 1, 10, days))
                        return false;
                }
                else if (fChar == 'h' && format.compare(fPos, 2, "hh") == 0)
                {
                    if (!read_number(inputPos, 2, 2, hours) || hours >= 24)
                        return false;
                    fPos++;
                }
                else if (fChar == 'h')
                {
                    if (!read_number(inputPos, 1, 2, hours) || hours >= 24)
                        return false;
                }
                else if (fChar == 'm' && format.compare(fPos, 2, "mm") == 0)
                {
                    if (!read_number(inputPos, 2, 2, minutes) || minutes >= 60)
                        return false;
                    fPos++;
                }
                else if (fChar == 'm')
                {
                    if (!read_number(inputPos, 1, 2, minutes) || minutes >= 60)
                        return false;
                }
                else if (fChar == 's' && format.compare(fPos, 2, "ss") == 0)
                {
                    if (!read_number(inputPos, 2, 2, seconds) || seconds >= 60)
                        return false;
                    fPos++;
                }
                else if (fChar == 's')
                {
                    if (!read_number(inputPos, 1, 2, seconds) || seconds >= 60)
                        return false;
                }
                else if (fChar == 'f')
                {
                    size_t fCount = 0;
                    while (fPos < format.length() && format[fPos] == 'f')
                    {
                        fCount++;
                        fPos++;
                    }
                    fPos--;

                    if (fCount > 7)
                        return false;

                    std::string fracDigits;
                    if (!read_digits(inputPos, 1, fCount, fracDigits))
                        return false;

                    if (!ParseFractionalSecondsToTicks(fracDigits, fracTicks))
                        return false;
                }
                else
                {
                    // Строгое совпадение разделителя / литерального символа.
                    if (input[inputPos] != fChar)
                        return false;
                    inputPos++;
                }
            }

            if (inputPos != input.length())
                return false;

            int64_t ticks = CheckedComponentsToTicks(days, hours, minutes, seconds, 0);
            ticks = CheckedAddInt64(ticks, fracTicks, "TimeSpan: overflow.");

            if (negative)
            {
                if (ticks == MinTicksTimeSpan)
                    return false;
                ticks = -ticks;
            }

            result = TimeSpan(ticks);
            return true;
        }
        catch (...)
        {
            result = TimeSpan::Zero;
            return false;
        }
    }

    bool TimeSpan::TryParseExact(const std::string& input, const std::vector<std::string>& formats, TimeSpan& result)
    {
        if (input.empty() || formats.empty())
            return false;

        for (const std::string& format : formats)
        {
            if (format.empty())
                continue;

            if (TimeSpan::TryParseExact(input, format, result))
                return true;
        }

        return false;
    }

    //////////////////////////////////////////////////////////////////////
    ///                     Операторы сравнения                        ///
    //////////////////////////////////////////////////////////////////////
    bool TimeSpan::operator==(const TimeSpan& value) const { return _ticks == value._ticks; }
    bool TimeSpan::operator!=(const TimeSpan& value) const { return _ticks != value._ticks; }
    bool TimeSpan::operator<(const TimeSpan& value)  const { return _ticks < value._ticks; }
    bool TimeSpan::operator>(const TimeSpan& value)  const { return _ticks > value._ticks; }
    bool TimeSpan::operator<=(const TimeSpan& value) const { return _ticks <= value._ticks; }
    bool TimeSpan::operator>=(const TimeSpan& value) const { return _ticks >= value._ticks; }

    //////////////////////////////////////////////////////////////////////
    ///                   Арифметические операторы                     ///
    //////////////////////////////////////////////////////////////////////
    TimeSpan TimeSpan::operator+(const TimeSpan& value) const { return Add(value); }
    TimeSpan TimeSpan::operator-(const TimeSpan& value) const { return Subtract(value); }

    //////////////////////////////////////////////////////////////////////
    ///           Арифметические операторы с присваиванием             ///
    //////////////////////////////////////////////////////////////////////
    TimeSpan& TimeSpan::operator+=(const TimeSpan& value)
    {
        _ticks = CheckedAddInt64(_ticks, value._ticks, "TimeSpan: addition overflow.");
        return *this;
    }

    TimeSpan& TimeSpan::operator-=(const TimeSpan& value)
    {
        _ticks = CheckedSubInt64(_ticks, value._ticks, "TimeSpan: subtraction overflow.");
        return *this;
    }

    TimeSpan& TimeSpan::operator=(const TimeSpan& value)
    {
        if (this != &value)
        {
            _ticks = value._ticks;
        }
        return *this;
    }

    TimeSpan& TimeSpan::operator=(TimeSpan&& value) noexcept
    {
        if (this != &value)
        {
            _ticks = value._ticks;
            value._ticks = 0;
        }
        return *this;
    }

    std::ostream& operator<<(std::ostream& os, const TimeSpan& ts)
    {
        os << ts.ToString();
        return os;
    }

    std::istream& operator>>(std::istream& is, TimeSpan& ts)
    {
        std::string temp;
        if (is >> temp)
        {
            if (!TimeSpan::TryParse(temp, ts))
            {
                is.setstate(std::ios::failbit);
            }
        }
        return is;
    }
	
	// Безопасное преобразование дробного значения в тики с проверкой NaN/Inf и переполнения.
    TimeSpan FromDoubleInterval(double value, int64_t scaleTicks)
    {
        if (!std::isfinite(value))
        {
            throw std::out_of_range("TimeSpan: value must be finite (not NaN or Inf).");
        }

        const double ticksDouble = value * static_cast<double>(scaleTicks);
        const double rounded = ticksDouble + (ticksDouble >= 0.0 ? 0.5 : -0.5);

        // Верхняя граница 2^63 непредставима в int64_t (max = 2^63 - 1).
        constexpr double kMinDouble = -9223372036854775808.0; // -2^63
        constexpr double kMaxDouble =  9223372036854775808.0; //  2^63 (исключительно)

        if (rounded < kMinDouble || rounded >= kMaxDouble)
        {
            throw std::out_of_range("TimeSpan: resulting ticks out of range.");
        }

        return TimeSpan(static_cast<int64_t>(rounded));
    }

    // Строгий разбор неотрицательного целого числа из строки.
    bool ParseStrictNonNegativeInt64(const std::string& str, int64_t& out)
    {
        if (str.empty())
            return false;

        for (char c : str)
        {
            if (!std::isdigit(static_cast<unsigned char>(c)))
                return false;
        }

        try
        {
            size_t idx = 0;
            out = std::stoll(str, &idx);
            return idx == str.size() && out >= 0;
        }
        catch (...)
        {
            return false;
        }
    }

    // Разбор дробной части секунд (от 1 до 7 цифр) в тики (100 нс).
    bool ParseFractionalSecondsToTicks(const std::string& fracStr, int64_t& outTicks)
    {
        if (fracStr.empty() || fracStr.size() > 7)
            return false;

        for (char c : fracStr)
        {
            if (!std::isdigit(static_cast<unsigned char>(c)))
                return false;
        }

        std::string padded = fracStr;
        while (padded.size() < 7)
            padded.push_back('0');

        outTicks = std::stoll(padded);
        return true;
    }
}
