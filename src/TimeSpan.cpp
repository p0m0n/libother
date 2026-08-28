#include "TimeSpan.h"
#include "private/PrivateTime.hpp"

namespace other::time
{
    // Инициализация нулевого значения (0 тиков).
    const TimeSpan TimeSpan::Zero(0LL);
    // Инициализация максимального значения.
    const TimeSpan TimeSpan::MaxValue(MaxTicksTimeSpan);
    // Инициализация минимального значения.
    const TimeSpan TimeSpan::MinValue(MinTicksTimeSpan);

    // Основной конструктор по тактам.
    TimeSpan::TimeSpan(int64_t ticks) : _ticks(ticks)
    {
        // Проверка границ допустимого диапазона.
        if (ticks < MinTicksTimeSpan || ticks > MaxTicksTimeSpan)
        {
#if LIBOTHER_DEBUG
            std::cout << "ERROR: TimeSpan, ticks are out of range." << std::endl;
#endif
            throw;
        }
    }

    // Делегирующий конструктор (Ч:М:С).
    TimeSpan::TimeSpan(int hours, int minutes, int seconds) 
        : TimeSpan(0, hours, minutes, seconds, 0)   // Перенаправляем вызов в полный конструктор.
    {
    }

    // Полный конструктор со всеми компонентами.
    TimeSpan::TimeSpan(int days, int hours, int minutes, int seconds, int milliseconds)
    {
        int64_t ticks = 0;
        
        // Используем макрос/функцию для сложения компонентов.
        ADDED_TICKS(ticks, days, hours, minutes, seconds, milliseconds);
        
        // Проверка границ допустимого диапазона.
        if (ticks < MinTicksTimeSpan || ticks > MaxTicksTimeSpan)
        {
#if LIBOTHER_DEBUG
            std::cout << "ERROR: TimeSpan, ticks are out of range." << std::endl;
#endif
            throw;
        }

        // Сохраняем результат.
        _ticks = ticks;
    }

    // Конструктор копирования.
    TimeSpan::TimeSpan(const TimeSpan& value) : _ticks(value._ticks)
    {
    }

    // Конструктор перемещения.
    TimeSpan::TimeSpan(TimeSpan&& value) noexcept : _ticks(value._ticks)
    {
        value._ticks = 0; // Сбрасываем старый объект.
    }

    int64_t TimeSpan::Ticks() const    { return _ticks; }
    int TimeSpan::Days() const         { return (int)(_ticks / TICKS_PER_DAY); }
    int TimeSpan::Hours() const        { return (int)((_ticks / TICKS_PER_HOUR) % 24); }
    int TimeSpan::Minutes() const      { return (int)((_ticks / TICKS_PER_MINUTE) % 60); }
    int TimeSpan::Seconds() const      { return (int)((_ticks / TICKS_PER_SECOND) % 60); }
    int TimeSpan::Milliseconds() const { return (int)((_ticks / TICKS_PER_MILLISECOND) % 1000); }

    double TimeSpan::TotalDays() const         { return static_cast<double>(_ticks) / TICKS_PER_DAY; }
    double TimeSpan::TotalHours() const        { return static_cast<double>(_ticks) / TICKS_PER_HOUR;}
    double TimeSpan::TotalMinutes() const      { return static_cast<double>(_ticks) / TICKS_PER_MINUTE;}
    double TimeSpan::TotalSeconds() const      { return static_cast<double>(_ticks) / TICKS_PER_SECOND; }
    double TimeSpan::TotalMilliseconds() const { return static_cast<double>(_ticks) / TICKS_PER_MILLISECOND; }

    // Создает новый TimeSpan путем сложения тиков.
    TimeSpan TimeSpan::Add(const TimeSpan& value) const
    {
        return TimeSpan(_ticks + value._ticks);
    }

    // Создает новый TimeSpan путем вычитания тиков.
    TimeSpan TimeSpan::Subtract(const TimeSpan& value) const
    {
        return TimeSpan(_ticks - value._ticks);
    }

    // Возвращает абсолютное значение.
    TimeSpan TimeSpan::Duration() const
    {
        // Если тики положительные - возвращаем как есть, иначе инвертируем.
        // Осторожно: если _ticks == MinValue, здесь может быть переполнение, но конструктор это отловит.
        return TimeSpan(_ticks >= 0 ? _ticks : -_ticks);
    }

    // Инвертирует знак (плюс на минус).
    TimeSpan TimeSpan::Negate() const
    {
        if (_ticks == MinTicksTimeSpan)
        {
#if LIBOTHER_DEBUG
            std::cout << "ERROR: TimeSpan, overflow negate." << std::endl;
#endif
            throw;
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
	
    // Преобразование в строку. Формат: [d.]hh:mm:ss[.fff]
    std::string TimeSpan::ToString() const
    {
        std::stringstream ss;

        // Если значение отрицательное, вручную добавляем знак.
        if (_ticks < 0)
            ss << "-";

        // Работаем с абсолютными значениями компонентов.
        int days = std::abs(Days());
        // Дни выводятся только если они есть (не ноль).
        if (days > 0)
            ss << days << ".";

        // Вывод времени с ведущими нулями (напр. 05:01:09).
        ss << std::setfill('0') << std::setw(2) << std::abs(Hours()) << ":"
            << std::setfill('0') << std::setw(2) << std::abs(Minutes()) << ":"
            << std::setfill('0') << std::setw(2) << std::abs(Seconds());

        // Выводим миллисекунды только если они есть.
        int milliseconds = std::abs(Milliseconds());
        if (milliseconds > 0)
            ss << "." << milliseconds;

        return ss.str();
    }
	
    std::string TimeSpan::ToString(const std::string& format) const
    {
        std::stringstream ss;

        // Обработка знака
        int64_t ticks = _ticks;
        if (_ticks < 0) 
        {
            ss << "-";
            // Защита от переполнения при инверсии MinTicks
            ticks = (_ticks == MinTicksTimeSpan) ? MaxTicksTimeSpan : -_ticks;
        }

        int64_t day = 0, hour = 0, minute = 0, second = 0, millisecond = 0;

        // Предварительный расчет компонентов (всегда положительные)
        FROM_TICKS(ticks, day, hour, minute, second, millisecond);

        for (size_t i = 0; i < format.length(); ++i)
        {
            // Поиск токенов d, hh, mm, ss, fff
            if (format[i] == 'd')
            {
                ss << day;
            }
            else if (format.compare(i, 2, "hh") == 0) 
            {
                ss << std::setw(2) << std::setfill('0') << hour;
                i++;
            }
            else if (format.compare(i, 2, "mm") == 0) 
            {
                ss << std::setw(2) << std::setfill('0') << minute;
                i++;
            }
            else if (format.compare(i, 2, "ss") == 0) 
            {
                ss << std::setw(2) << std::setfill('0') << second;
                i++;
            }
            else if (format.compare(i, 3, "fff") == 0) 
            {
                ss << std::setw(3) << std::setfill('0') << millisecond;
                i += 2;
            }
            else 
            {
                // Любой другой символ (например, ':' или '.') выводим как есть
                ss << format[i];
            }
        }

        return ss.str();
    }

    // Получение хэш-кода для использования в std::map или std::unordered_map.
    size_t TimeSpan::GetHashCode() const
    {
        // Используем стандартный хэшер для int64_t.
        return std::hash<int64_t>{}(_ticks);
    }

    TimeSpan TimeSpan::FromDays(double value)
    {
        double  ticks = value * TICKS_PER_DAY;

        // Добавляем 0.5 для округления к ближайшему целому, а не просто отбрасываем дробь.
        // Если число положительное, добавляем 0.5, если отрицательное — вычитаем 0.5.
        int64_t result = static_cast<int64_t>(ticks + (value >= 0 ? 0.5 : -0.5));

        // Возвращаем объект.
        // Конструктор проверит валидность result.
        return TimeSpan(result);
    }

    TimeSpan TimeSpan::FromHours(double value)
    {
        double  ticks = value * TICKS_PER_HOUR;
        int64_t result = static_cast<int64_t>(ticks + (value >= 0 ? 0.5 : -0.5));
        return TimeSpan(result);
    }

    TimeSpan TimeSpan::FromMinutes(double value)
    {
        double  ticks = value * TICKS_PER_MINUTE;
        int64_t result = static_cast<int64_t>(ticks + (value >= 0 ? 0.5 : -0.5));
        return TimeSpan(result);
    }

    TimeSpan TimeSpan::FromSeconds(double value)
    {
        double  ticks = value * TICKS_PER_SECOND;
        int64_t result = static_cast<int64_t>(ticks + (value >= 0 ? 0.5 : -0.5));
        return TimeSpan(result);
    }

    TimeSpan TimeSpan::FromMilliseconds(double value)
    {
        double ticks = value * TICKS_PER_MILLISECOND;
        int64_t result = static_cast<int64_t>(ticks + (value >= 0 ? 0.5 : -0.5));
        return TimeSpan(result);
    }

    TimeSpan TimeSpan::FromTicks(int64_t ticks) { return TimeSpan(ticks); }

    int TimeSpan::Compare(const TimeSpan& t1, const TimeSpan& t2) { return t1.Compare(t2); }
    bool TimeSpan::Equals(const TimeSpan& t1, const TimeSpan& t2) { return t1.Equals(t2); }

    TimeSpan TimeSpan::Parse(const std::string& input)
    {
        TimeSpan result;
        if (TimeSpan::TryParse(input, result))
            return result;

        return TimeSpan::Zero;
    }

    bool TimeSpan::TryParse(const std::string& input, TimeSpan& result)
    {
		// Базовая проверка на 0.
        if (input.empty())
            return false;

        size_t inputPos = 0;
        bool negative = false;

        // Проверяем первый символ на наличие знака минус.
        if (input[inputPos] == '-')
        {
            negative = true;
            inputPos = 1;
        }

        int64_t days = 0, hours = 0, minutes = 0, seconds = 0, milliseconds = 0;

        try
        {
            // Подготовка к разбиению строки на компоненты.
            std::vector<std::string> parts;
            std::string current;

            // Разбиваем строку на части по любым разделителям (:, .)
            for (size_t i = inputPos; i < input.length(); ++i)
            {
                // Ищем разделители (дней ".", времени ":")
                if (input[i] == '.' || IsTimeSeparator(input[i]))
                {
                    // Если нашли разделитель и буфер не пуст — сохраняем число.
                    if (!current.empty())
                    {
                        parts.push_back(current);
                        current.clear();
                    }
                }
                else
                {
                    // Накапливаем цифры.
                    current += input[i];
                }
            }

            // Добавляем последнее число после цикла.
            if (!current.empty())
                parts.push_back(current);

            // Минимальный формат hh:mm:ss требует 3 части.
            if (parts.size() < 3)
                return false;

            if (parts.size() >= 4) 
            {
                days    = std::stoll(parts[0]);
                hours   = std::stoll(parts[1]);
                minutes = std::stoll(parts[2]);
                seconds = std::stoll(parts[3]);

                if (parts.size() == 5) 
                {
                    // Обработка миллисекунд
                    std::string msPart = parts[4];
                    
                    if (msPart.length() > 3)
                        msPart = msPart.substr(0, 3);

                    milliseconds = std::stoll(msPart);
                }
            }
            else 
            {
                hours   = std::stoll(parts[0]);
                minutes = std::stoll(parts[1]);
                seconds = std::stoll(parts[2]);
            }

            int64_t ticks = 0;
			// Используем макрос/функцию для сложения компонентов.
			ADDED_TICKS(ticks, days, hours, minutes, seconds, milliseconds);

            // Применяем знак, если он был.
            if (negative)
            {
                ticks = -ticks;
            }
			
			// Создаем итоговый объект.
            result = TimeSpan(ticks);
            return true;
        }
        catch (...) // Ловим любые исключения.
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

        return TimeSpan::Zero;
    }

    TimeSpan TimeSpan::ParseExact(const std::string& input, const std::vector<std::string>& formats)
    {
        TimeSpan result;

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

        return TimeSpan::Zero;
    }

    bool TimeSpan::TryParseExact(const std::string& input, const std::string& format, TimeSpan& result)
    {
        // Базовая проверка на 0.
        if (input.empty() || format.empty()) 
            return false;

        int64_t days = 0, hours = 0, minutes = 0, seconds = 0, milliseconds = 0;
        size_t inputPos = 0;
        bool negative = false;

        // Проверяем первый символ на наличие знака минус.
        if (input[inputPos] == '-')
        {
            negative = true;
            inputPos = 1;
        }

        // Лямбда для чтения чисел
        auto read_number = [&](size_t& pos) -> int64_t
        {
            size_t start = pos;
            while (pos < input.length() && std::isdigit(static_cast<uint8_t>(input[pos])))
            {
                pos++;
            }
            
            if (start == pos) 
                return -1;
            
            std::string numStr = input.substr(start, pos - start);

            try 
            { 
                return std::stoll(numStr); 
            }
            catch (...) 
            { 
                return -1; 
            }
        };

        try
        {
            // Цикл по формату
            for (size_t fPos = 0; fPos < format.length(); ++fPos)
            {
                // Если формат еще требует данных, а строка закончилась
                if (inputPos >= input.length())
                    return false;

                char fChar = format[fPos];

                // Поиск дней.
                if (fChar == 'd')
                {
                    days = read_number(inputPos);
                    if (days < 0)
                        return false;
                }
                // Поиск часов.
                else if (fChar == 'h' && format.compare(fPos, 2, "hh") == 0)
                {
                    hours = read_number(inputPos);
                    fPos++; // Пропуск второй 'h'
                    if (hours < 0)
                        return false;
                }
                // Поиск минут.
                else if (fChar == 'm' && format.compare(fPos, 2, "mm") == 0)
                {
                    minutes = read_number(inputPos);
                    fPos++;
                    if (minutes < 0)
                        return false;
                }
                // Поиск секунд.
                else if (fChar == 's' && format.compare(fPos, 2, "ss") == 0)
                {
                    seconds = read_number(inputPos);
                    fPos++;
                    if (seconds < 0)
                        return false;
                }
                // Поиск миллисекунд.
                else if (fChar == 'f')
                {
                    size_t count = 0;
                    while (fPos < format.length() && format[fPos] == 'f')
                    {
                        count++;
                        fPos++;
                    }

                    fPos--; // Возвращаем указатель на последнюю f

                    milliseconds = read_number(inputPos); // true активирует нормализацию до тиков
                    if (milliseconds < 0)
                        return false;
                }
                else
                {
                    // Обработка разделителей или фиксированных символов
                    if (input[inputPos] == '.' || IsTimeSeparator(input[inputPos]) || input[inputPos] == fChar)
                    {
                        inputPos++;
                    }
                    else
                    {
                        // Разделитель не совпал
                        return false;
                    }
                }
            }

            // КРИТИЧЕСКАЯ ПРОВЕРКА: Если после обработки формата в строке остались лишние символы
            // Это предотвращает ситуацию, когда "12:45:00:99" успешно парсится как "12:45:00"
            if (inputPos < input.length())
                return false;
			
            int64_t ticks = 0;
			// Используем макрос/функцию для сложения компонентов.
            ADDED_TICKS(ticks, days, hours, minutes, seconds, milliseconds);

            // Применяем знак, если он был.
            if (negative)
            {
                ticks = -ticks;
            }

            // Создаем итоговый объект.
            result = TimeSpan(ticks);
            return true;
        }
        catch (...) // Ловим любые исключения.
        {
            result = TimeSpan::Zero;
            return false;
        }
    }

    bool TimeSpan::TryParseExact(const std::string& input, const std::vector<std::string>& formats, TimeSpan& result)
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
            if (TimeSpan::TryParseExact(input, format, result))
            {
                // Если успех, переменная result уже содержит значение
                return true;
            }
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
    TimeSpan TimeSpan::operator+(const TimeSpan& value) const { return TimeSpan(_ticks + value._ticks); }
    TimeSpan TimeSpan::operator-(const TimeSpan& value) const { return TimeSpan(_ticks - value._ticks); }
    //////////////////////////////////////////////////////////////////////
    ///           Арифметические операторы с присваиванием             ///
    //////////////////////////////////////////////////////////////////////
    TimeSpan& TimeSpan::operator+=(const TimeSpan& value) 
    { 
        if (this != &value)
        {
            _ticks += value._ticks;
        }
        return *this;
    }
    TimeSpan& TimeSpan::operator-=(const TimeSpan& value)
    {
        if (this != &value)
        {
            _ticks -= value._ticks;
        }
        return *this;
    }
    TimeSpan& TimeSpan::operator=(const TimeSpan& value)
    {
        if (this != &value)
        {
            // Копируем значение из временного объекта.
            _ticks = value._ticks;
        }
        return *this;
    }
    TimeSpan& TimeSpan::operator=(TimeSpan&& value) noexcept
    {
        if (this != &value)
        {
            // Копируем значение из временного объекта.
            _ticks = value._ticks;

            // "Сбрасываем" временный объект.
            value._ticks = 0;
        }
        return *this;
    }
	
    std::ostream& operator<<(std::ostream& os, const TimeSpan& ts)
    {
        // Мы просто передаем строковое представление в поток.
        // Если ToString() возвращает формат [d.]hh:mm:ss[.fffffff], 
        // он будет выведен и здесь.
        os << ts.ToString();

        // Обязательно возвращаем поток, чтобы можно было писать:
        // std::cout << ts1 << " и " << ts2;
        return os;
    }

    std::istream& operator>>(std::istream& is, TimeSpan& ts)
    {
        std::string temp;
        // Считываем строку из потока до первого пробельного символа
        if (is >> temp)
        {
            // Пытаемся распарсить полученную строку
            if (!TimeSpan::TryParse(temp, ts))
            {
                // Если формат неверный, выставляем failbit.
                // Это стандартное поведение для C++ при ошибке ввода.
                is.setstate(std::ios::failbit);
            }
        }
        return is;
    }
}
