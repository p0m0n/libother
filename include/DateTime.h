#ifndef _OTHER_DATETIME_H_
#define _OTHER_DATETIME_H_

#include "TimeSpan.h"


namespace other::time
{
    /**
     * @brief Тип времени: локальное, UTC или неопределенное.
     */
    enum class DateTimeKind
    {
        Unspecified = 1,
        Utc = 2,
        Local = 3
    };

    /**
     * @brief Дни недели.
     */
    enum class DayOfWeek
    {
        Sunday = 0,       // Воскресенье
        Monday = 1,       // Понедельник
        Tuesday = 2,      // Вторник
        Wednesday = 3,    // Среда
        Thursday = 4,     // Четверг
        Friday = 5,       // Пятница
        Saturday = 6      // Суббота
    };

    /**
     * @brief Месяцы года.
     */
    enum class MonthOfYear
    {
        January = 1,       // Январь
        February = 2,      // Февраль
        March = 3,         // Март
        April = 4,         // Апрель
        May = 5,           // Май
        June = 6,          // Июнь
        July = 7,          // Июль
        August = 8,        // Август
        September = 9,     // Сентябрь
        October = 10,      // Октябрь
        November = 11,     // Ноябрь
        December = 12      // Декабрь
    };

    /**
     * @class DateTime
     * @brief Представляет момент времени, обычно выраженный в виде даты и времени суток.
     * Класс позволяет работать с датами, вычислять временные интервалы, форматировать
     * вывод и выполнять арифметические операции с учетом тактов (100-наносекундных интервалов).
     */
    class DateTime
    {
    private:
        int64_t      _ticks;    // 100-наносекундные интервалы с 01.01.0001.
        DateTimeKind _kind;     // Указывает на часовой пояс (UTC/Local/Unspecified).

    public:
        static const DateTime Zero;        // Нулевое значение {NULL}.
        static const DateTime MinValue;    // Минимально возможное значение DateTime, соответствует дате 00:00:00.0000000 UTC, 1 января 0001 года.
        static const DateTime MaxValue;    // Максимально возможное значение DateTime, соответствует дате 23:59 : 59.9999999 UTC, 31 декабря 9999 года.
        /**
         * @brief Инициализирует новый экземпляр DateTime заданным количеством тиков и типом времени.
         * Каждый тик представляет собой интервал времени, равный 100 наносекундам.
         * Значение этого параметра представляет количество стонаносекундных интервалов, прошедших с 00:00:00 1 января 0001 года.
         *
         * @param [in] ticks: Количество тиков (от MinValue до MaxValue).
         * @param [in] kind:  Задает тип времени (локальное, UTC или неопределенное).
		 *
         * @exception std::out_of_range Выбрасывается, если значение ticks меньше MinValue или больше MaxValue.
         */
        explicit DateTime(int64_t ticks = 0, const DateTimeKind& kind = DateTimeKind::Unspecified);
        /**
         * @brief Инициализирует новый экземпляр DateTime заданными значениями года, месяца и дня.
         * Время по умолчанию устанавливается на начало дня (00:00:00.000), а тип времени — на DateTimeKind::Unspecified.
         *
         * @param [in] year:  Год (от 1 до 9999).
         * @param [in] month: Месяц (от 1 до 12).
         * @param [in] day:   День (от 1 до количества дней в месяце).
		 *
         * @exception std::out_of_range Выбрасывается, если параметры года, месяца или дня находятся вне допустимого диапазона.
         */
        DateTime(int year, int month, int day);
        /**
         * @brief Инициализирует новый экземпляр DateTime заданными компонентами даты, времени и типом времени.
         * Вычисляет общее количество тиков (100-наносекундных интервалов), прошедших с 00:00:00 1 января 0001 года.
         * Алгоритм учитывает високосные года по григорианскому календарю.
         *
         * @param [in] year:        Год (от 1 до 9999).
         * @param [in] month:       Месяц (от 1 до 12).
         * @param [in] day:         День (от 1 до количества дней в конкретном месяце и году).
         * @param [in] hour:        Часы (от 0 до 23).
         * @param [in] minute:      Минуты (от 0 до 59).
         * @param [in] second:      Секунды (от 0 до 59).
         * @param [in] millisecond: Миллисекунды (от 0 до 999).
         * @param [in] kind:        Тип времени (Utc, Local или Unspecified).
		 *
         * @exception std::out_of_range Выбрасывается, в следующих случаях, если один из параметров (year, month, hour, minute, second, millisecond) вне допустимого диапазона.
         */
        DateTime(int year, int month, int day, int hour, int minute, int second, int millisecond = 0, const DateTimeKind& kind = DateTimeKind::Unspecified);
        /**
         * @brief Конструктор копирования (Copy Constructor).
         * 
         * @param [in] value: Ссылка на существующий объект DateTime.
         */
        DateTime(const DateTime& value);
        /**
         * @brief Конструктор перемещения (Move Constructor).
         * 
         * @param [in] value: Ссылка на временный объект DateTime.
         */
        DateTime(DateTime&& value) noexcept;
        ~DateTime() = default;
        /**
         * @brief Возвращает объект DateTime, установленный на текущию дату и время данного компьютера (Local Time).
         * Метод получает текущее универсальное время (UTC) и преобразует его в местное время в соответствии с настройками часового пояса и правилами перехода на летнее время текущей системы.
         *
         * @return DateTime Объект, значением которого являются текущие местные дата и время.
         */
        static DateTime Now();
        /**
         * @brief Возвращает объект DateTime, установленный на текущие дату и время в формате UTC.
         * Использует системные часы (std::chrono::system_clock) для получения времени с максимально возможной точностью. Значение корректируется с учетом разницы между эпохой Unix (1970) и эпохой DateTime (0001).
         *
         * @return DateTime Объект с текущим универсальным временем (UTC).
         */
        static DateTime UtcNow();
        /**
         * @brief Возвращает текущую дату.
         * Возвращает объект, в котором дата соответствует текущему дню, а время установлено на полночь (00:00:00). Тип времени соответствует локальному.
         *
         * @return DateTime Объект, представляющий начало текущего дня.
         */
        static DateTime Today();
        /**
         * @brief Получает количество тиков, представляющих дату и время этого экземпляра.
         *
         * @return Количество 100-наносекундных интервалов, прошедших с 00:00:00 1 января 0001 года.
         */
        int64_t Ticks() const;
        /**
         * @brief Получает значение, указывающее, представляет ли данное значение местное время, время UTC или ни то, ни другое(Unspecified).
         *
         * @return Одно из значений DateTimeKind, определяющее интерпретацию текущего времени.
         */
        DateTimeKind Kind() const;
        /**
         * @brief Возвращает год.
         *
         * @return Год в диапазоне от 1 до 9999.
         */
        int Years() const;
        /**
         * @brief Возвращает месяц.
         *
         * @return Месяц в диапазоне от 1 до 12.
         */
        int Months() const;
        /**
         * @brief Возвращает день.
         *
         * @return День в диапазоне от 1 до 31.
         */
        int Days() const;
        /**
         * @brief Возвращает час.
         *
         * @return Часы в диапазоне от 0 до 23.
         */
        int Hours() const;
        /**
         * @brief Возвращает минуты.
         *
         * @return Минуты в диапазоне от 0 до 59.
         */
        int Minutes() const;
        /**
         * @brief Возвращает секунды.
         *
         * @return Секунды в диапазоне от 0 до 59.
         */
        int Seconds() const;
        /**
         * @brief Возвращает миллисекунды.
         *
         * @return Миллисекунды в диапазоне от 0 до 999.
         */
        int Milliseconds() const;
        /**
         * @brief Возвращает день недели.
         *
         * @return Значение перечисления DayOfWeek (от Sunday до Saturday).
         */
        DayOfWeek GetDayOfWeek() const;
        /**
         * @brief Возвращает день года.
         *
         * @return Целое число в диапазоне от 1 до 366.
         */
        int DayOfYear() const;
        /**
         * @brief Возвращает месяц года.
         *
         * @return Значение перечисления MonthOfYear (от January до December).
         */
        MonthOfYear GetMonthOfYear() const;
        /**
         * @brief Возвращает новый объект DateTime, представляющий дату текущего экземпляра с временем 00:00:00.
         * Обнуляет временную составляющую (часы, минуты, секунды), сохраняя количество полных дней.
         *
         * @return Объект DateTime с тем же значением Kind и временем, установленным на полночь.
         * @exception Только те что определенны в первом конструкторе.
         */
        DateTime Date() const;
        /**
         * @brief Возвращает время суток для данного экземпляра.
         * Извлекает интервал времени, прошедший с начала текущего дня.
         *
         * @return Объект TimeSpan, представляющий дробную часть суток текущего экземпляра.
         * @exception Только те что определенны в конструкторе TimeSpan.
         */
        TimeSpan TimeOfDay() const;
        
		/**
         * @brief Возвращает новый объект DateTime, который является результатом прибавления значения заданного TimeSpan к значению данного экземпляра.
         *
         * @param [in] value: Объект TimeSpan, содержащий добавляемый интервал (может быть отрицательным).
         * 
         * @return Объект, значение которого равно сумме даты и времени текущего экземпляра и интервала value.
         * @exception std::out_of_range Если результирующее значение меньше MinValue или больше MaxValue.
         */
        DateTime Add(const TimeSpan& value) const;
        /**
         * @brief Добавляет указанное количество дней к значению данного экземпляра.
         *
         * @param [in] value: Дробное количество дней (например, 1.5 добавит 1 день и 12 часов).
         * 
         * @return Новый объект DateTime.
         * @exception std::out_of_range Если результирующее значение меньше MinValue или больше MaxValue.
         */
        DateTime AddDays(double value) const;
        /**
         * @brief Добавляет указанное количество часов к значению данного экземпляра.
         *
         * @param [in] value: Дробное количество часов (например, 2.5 добавит 2 часа и 30 минут).
         * 
         * @return Новый объект DateTime.
         * @exception std::out_of_range Если результирующее значение меньше MinValue или больше MaxValue.
         */
        DateTime AddHours(double value) const;
        /**
         * @brief Добавляет указанное количество минут к значению данного экземпляра.
         *
         * @param [in] value: Дробное количество минут (например, 1.25 добавит 1 минуту и 15 секунд).
         * 
         * @return Новый объект DateTime.
         * @exception std::out_of_range Если результирующее значение меньше MinValue или больше MaxValue.
         */
        DateTime AddMinutes(double value) const;
        /**
         * @brief Добавляет указанное количество секунд к значению данного экземпляра.
         *
         * @param [in] value: Дробное количество секунд (например, 0.5 добавит 500 миллисекунд).
         * 
         * @return Новый объект DateTime.
         * @exception std::out_of_range Если результирующее значение меньше MinValue или больше MaxValue.
         */
        DateTime AddSeconds(double value) const;
        /**
         * @brief Добавляет указанное количество миллисекунд к значению данного экземпляра.
         *
         * @param [in] value: Дробное количество миллисекунд (например, 1.5 добавит 1 миллисекунду и 5000 тиков).
         * 
         * @return Новый объект DateTime.
         * @exception std::out_of_range Если результирующее значение меньше MinValue или больше MaxValue.
         */
        DateTime AddMilliseconds(double value) const;
        /**
         * @brief Добавляет указанное количество тиков к значению данного экземпляра.
         * Тик является минимальной единицей времени в данном классе и равен 100 наносекундам.
         * Этот метод наиболее эффективен для точных вычислений, так как не требует преобразования типов.
         *
         * @param [in] value: Количество тиков (может быть положительным или отрицательным).
         * 
         * @return Новый объект DateTime с добавленным интервалом.
         * @exception std::out_of_range Если результирующее значение меньше MinValue или больше MaxValue.
         */
        DateTime AddTicks(int64_t value) const;
        /**
         * @brief Добавляет указанное количество месяцев к значению данного экземпляра.
         * Если текущий день месяца превышает количество дней в целевом месяце, он автоматически уменьшается до последнего дня этого месяца (например, 31 августа + 6 месяцев = 28/29 февраля).
         *
         * @param [in] months: Количество месяцев (от -120000 до 120000).
         * 
         * @return Новый объект DateTime.
         * @exception std::out_of_range Если результирующий год выходит за пределы 1-9999.
         */
        DateTime AddMonths(int months) const;
        /**
         * @brief Добавляет указанное количество лет к значению данного экземпляра.
         * Корректно обрабатывает високосные годы: если исходная дата 29 февраля, а целевой год не является високосным, результатом будет 28 февраля.
         *
         * @param [in] years: Количество лет (от -10000 до 10000) или std::out_of_range.
         * 
         * @return Новый объект DateTime.
         * @exception Только те что определенны в конструкторе.
         */
        DateTime AddYears(int years) const;
        /**
         * @brief Вычитает указанный временной интервал из данного экземпляра.
         *
         * @param [in] value: Интервал TimeSpan.
         * 
         * @return Новый объект DateTime.
         * @exception Только те что определенны в конструкторе.
         */
        DateTime Subtract(const TimeSpan& value) const;
        /**
         * @brief Вычисляет временной интервал между двумя датами.
         *
         * @param [in] value: Дата, которую необходимо вычесть.
         * 
         * @return Объект TimeSpan, представляющий разницу во времени.
         * @exception Только те что определенны в конструкторе.
         */
        TimeSpan Subtract(const DateTime& value) const;
        /**
         * @brief Преобразует значение текущего объекта DateTime в местное время.
         * Если объект уже имеет тип Local, возвращается копия. В противном случае
         * вычисляется смещение часового пояса системы и флаг летнего времени (DST).
         *
         * @return Объект DateTime с типом Local, значение которого соответствует местному времени.
         * @exception std::out_of_range Если результирующее значение меньше MinValue или больше MaxValue.
         */
        DateTime ToLocalTime() const;
        /**
         * @brief Преобразует значение текущего объекта DateTime в универсальное координированное время (UTC).
         * Выполняет операцию, обратную ToLocalTime, корректируя тики на смещение часового пояса.
         *
         * @return Объект DateTime с типом Utc.
         * @exception std::out_of_range Если результирующее значение меньше MinValue или больше MaxValue.
         */
        DateTime ToUniversalTime() const;
        /**
         * @brief Сравнивает текущий экземпляр с другим объектом DateTime.
         *
         * @param [in] value: Объект для сравнения.
         * 
         * @return 1, если текущий объект позже; -1, если раньше; 0, если значения равны.
         */
        int Compare(const DateTime& value) const;
        /**
         * @brief Проверяет, совпадает ли значение текущего экземпляра с другим объектом DateTime.
         *
         * @return true, если количество тиков совпадает, иначе false.
         */
        bool Equals(const DateTime& value) const;
        /**
         * @brief Возвращает хэш-код текущего экземпляра.
         * Позволяет использовать DateTime в качестве ключа в std::unordered_map.
         *
         * @return Хэш-код на основе общего количества тиков.
         */
        size_t GetHashCode() const;
        /**
         * @brief Определяет, относится ли дата и время текущего экземпляра к периоду летнего времени.
         *
         * @return true, если летнее время активно для данного момента в текущем часовом поясе.
         */
        bool IsDaylightSavingTime() const;
        /**
         * @brief Возвращает количество дней в указанном месяце указанного года.
         * Учитывает особенности високосных годов для февраля.
         *
         * @param [in] year:  Год (от 1 до 9999).
         * @param [in] month: Месяц (от 1 до 12).
         * 
         * @return Количество дней (например, 28, 29, 30 или 31).
         */
        static int DaysInMonth(int year, int month);
        /**
         * @brief Определяет, является ли заданный год високосным.
         * Согласно григорианскому календарю, год является високосным, если он делится на 4, но не делится на 100, за исключением тех лет, которые делятся на 400.
         *
         * @param [in] year: Год для проверки.
         * 
         * @return true, если год високосный; иначе false.
         */
        static bool IsLeapYear(int year);
        /**
         * @brief Сравнивает два экземпляра DateTime и возвращает признак их относительного порядка.
         *
         * @param [in] t1: Первый сравниваемый объект.
         * @param [in] t2: Второй сравниваемый объект.
         * 
         * @return 1, если t1 позже t2; -1, если t1 раньше t2; 0, если они равны.
         */
        static int Compare(const DateTime& t1, const DateTime& t2);
        /**
         * @brief Определяет, представляют ли два экземпляра DateTime один и тот же момент времени.
         *
         * @return true, если количество тиков совпадает; иначе false.
         */
        static bool Equals(const DateTime& t1, const DateTime& t2);
        /**
          * @brief Создает новый объект DateTime, имеющий то же количество тиков, что и указанный DateTime, но с заданным значением DateTimeKind.
          *
          * @param [in] value: Исходная дата.
          * @param [in] kind:  Новый тип времени (Local, Utc или Unspecified).
          * 
          * @return Новый экземпляр DateTime с тем же временем, но другим флагом типа.
          * @exception std::out_of_range Если результирующее значение меньше MinValue или больше MaxValue.
          */
        static DateTime SpecifyKind(const DateTime& value, const DateTimeKind& kind);
        /**
         * @brief Преобразует значение текущего объекта DateTime в эквивалентное строковое представление.
         *
         * @return Строка в формате "dd.MM.yyyy HH:mm:ss".
         */
        std::string ToString() const;
        /**
         * @brief Преобразует объект DateTime в строковое представление.
         * Метод поддерживает два типа форматирования: стандартные спецификаторы (одиночные символы) и пользовательские шаблоны (произвольные строки).
         * 
         *   ### Стандартные форматы (Standard Format Strings):
         * |  Символ |             Описание         |  Пример (на 12.01.2026 19:24:08)   |
         * |:-------:|:-----------------------------|:-----------------------------------|
         * |  **d**  |           Короткая дата      | 12.01.2026                         |
         * |  **D**  |            Полная дата       | Monday, 12 January 2026            |
         * |  **t**  |          Короткое время      | 19:24                              |
         * |  **T**  |            Полное время      | 19:24:08                           |
         * |  **g**  |            Общий (short)     | 12.01.2026 19:24                   |
         * |  **G**  |            Общий (long)      | 12.01.2026 19:24:08 (по умолчанию) |
         * |  **f**  | Полная дата + короткое время | Monday, 12 January 2026 19:24      |
         * |  **F**  | Полная дата + долгое время   | Monday, 12 January 2026 19:24:08   |
         * | **M/m** |            День и месяц      | 12 January                         |
         * | **Y/y** |             Месяц и год      | January 2026                       |
         * 
         *   ### Технические форматы (ISO / RFC):
         * - **'s'**    (Sortable): Стандарт ISO 8601, не зависит от локали. Пример: `2026-01-12T19:24:08`.
         * - **'u'**   (Universal): Универсальный формат в UTC. Пример: `2026-01-12 19:24:08Z`.
         * - **'R'/'r'** (RFC1123): Стандарт для HTTP-заголовков. **Всегда на английском**. Пример: `Mon, 12 Jan 2026 19:24:08 GMT`.
         * 
         *   ### Пользовательские форматы (Custom Format Strings):
         * Вы можете комбинировать следующие токены для создания своего формата:
         * - `yyyy` - год (4 цифры), `yy` - год (2 цифры)
         * - `MMMM` - полное имя месяца, `MMM` - краткое, `MM` - число (01-12)
         * - `dddd` - день недели, `dd` - день месяца (01-31)
         * - `HH` - часы (00-23), `mm` - минуты (00-59), `ss` - секунды (00-59)
         * 
         * @param [in] format: Строка формата или одиночный спецификатор.
         * 
         * @return std::string Сформированная строка даты/времени.
         * 
         * При использовании технических спецификаторов ('s', 'u', 'r') локальные настройки игнорируются.
         */
        std::string ToString(const std::string& format) const;
        // Возвращает массив форматов, поддерживаемых этим классом.
        std::vector<std::string> DateTimeFormats() const;
        /**
         * @brief Возвращает строку, содержащую только дату в коротком формате.
         *
         * @return Строка в формате "dd.MM.yyyy".
         */
        std::string ToShortDateString() const;
        /**
         * @brief Возвращает строку с полным названием дня недели, числом, названием месяца и годом.
         *
         * @example "Thursday, 25 December 2025"
         */
        std::string ToLongDateString() const;
        /**
         * @brief Возвращает строку, содержащую только время в коротком формате.
         * Формирует строку вида "HH:mm". Использует 24-часовой формат.
         * 
         * @return Строка, представляющая часы и минуты данного экземпляра.
         */
        std::string ToShortTimeString() const;
        /**
         * @brief Возвращает строку, содержащую время в полном формате (с секундами).
         * Формирует строку вида "HH:mm:ss". Использует 24-часовой формат.
         * 
         * @return Строка, представляющая часы, минуты и секунды данного экземпляра.
         */
        std::string ToLongTimeString() const;
        /**
         * @brief Преобразует строковое представление даты и времени в эквивалент DateTime.
         *
         * @param [in] input: Строка для анализа (формат DD.MM.YYYY HH:mm:ss).
         * 
         * @return Объект DateTime.
         * @exception std::invalid_argument Если строка не является допустимым представлением даты.
         */
        static DateTime Parse(const std::string& input);
        /**
         * @brief Пытается преобразовать строку в DateTime. Не генерирует исключений.
         *
         * @param [in] input:  Входная строка.
         * @param [in] result: Выходной параметр для записи результата.
         * 
         * @return true, если преобразование прошло успешно; иначе false.
         */
        static bool TryParse(const std::string& input, DateTime& result);
        /**
         * @brief Преобразует строку в DateTime, используя строго заданный шаблон формата.
         *
         * @param [in] input: Входная строка.
         * @param [in] format: Шаблон формата (например, "yyyy-MM-dd").
         * 
         * @return Объект DateTime.
         */
        static DateTime ParseExact(const std::string& input, const std::string& format);
		static DateTime ParseExact(const std::string& input, const std::vector<std::string>& formats);
        /**
         * @brief Преобразует строку в DateTime, используя строго заданный шаблон формата.
         * Использует системные вызовы POSIX (strptime) на Unix и стандартные потоки на Windows.
         *
         * @param [in]  input:  Входная строка с датой.
         * @param [in]  format: Строка формата в стиле (yyyy, MM, dd, HH, mm, ss).
         * @param [out] result: Выходной параметр для записи результата.
         * @return true, если парсинг прошел успешно; иначе false.
         */
        static bool TryParseExact(const std::string& input, const std::string& format, DateTime& result);
		static bool TryParseExact(const std::string& input, const std::vector<std::string> &formats, DateTime& result);
        /**
         * Преобразует текущий экземпляр DateTime в формат Unix Time (секунд).
         * Unix-время — это количество секунд, прошедших с 1 января 1970 года 00:00:00 UTC.
		 *
         * @return Количество секунд с начала эпохи Unix.
         */
        int64_t ToUnixTimeSeconds() const;
		/**
         * Преобразует текущий экземпляр DateTime в формат Unix Time (миллисекунды).
         * Unix-время — это количество миллисекунд, прошедших с 1 января 1970 года 00:00:00 UTC.
		 *
         * @return Количество миллисекунд с начала эпохи Unix.
         */
		int64_t ToUnixTimeMilliseconds() const;
        /**
         * Создает объект DateTime из количества секунд Unix Time.
		 *
         * @param [in] seconds: Количество секунд, прошедших с 1 января 1970 года 00:00:00 UTC.
		 *
         * @return Объект DateTime, представляющий указанный момент времени в формате UTC.
         */
        static DateTime FromUnixTimeSeconds(int64_t seconds);
		/**
         * Создает объект DateTime из количества миллисекунд Unix Time.
		 *
         * @param [in] milliseconds: Количество миллисекунд, прошедших с 1 января 1970 года 00:00:00 UTC.
		 *
         * @return Объект DateTime, представляющий указанный момент времени в формате UTC.
         */
		static DateTime FromUnixTimeMilliseconds(int64_t milliseconds);
        /**
         * @brief Сериализует текущий объект в 64-битное целое число для компактного хранения или передачи.
         * Использует битовую маску: 2 верхних бита хранят DateTimeKind, остальные 62 бита — количество тиков.
         *
         * @return Компактное бинарное представление объекта.
         */
        int64_t ToBinary() const;
        /**
         * @brief Восстанавливает объект DateTime из его 64-битного бинарного представления.
         *
         * @param [in] dateData: Данные, полученные методом ToBinary.
         * 
         * @return Восстановленный экземпляр DateTime с сохраненным Kind и Ticks.
         * @exception std::out_of_range Если результирующее значение меньше MinValue или больше MaxValue.
         */
        static DateTime FromBinary(int64_t dateData);
        /**
         * @brief Преобразует значение текущего объекта DateTime в формат Windows FileTime.
         * Представляет количество 100-наносекундных интервалов, прошедших с 00:00:00 UTC 1 января 1601 года.
         * Согласно стандартам системных вызовов Windows, этот метод всегда возвращает время в формате UTC, автоматически выполняя преобразование, если текущий экземпляр имеет тип Local.
         *
         * @return 64-битное целое число, представляющее FileTime.
         */
        int64_t ToFileTime() const;
        /**
         * @brief Преобразует системное время Windows (FILETIME) в объект DateTime, выраженный в местном (локальном) времени.
		 *
         * Логика работы метода:
         * 1. Входное значение fileTime (тики с 1601 года) переводится в системные тики (с 0001 года).
         * 2. Создается временный объект в формате UTC.
         * 3. Выполняется автоматическая конвертация из UTC в локальный часовой пояс системы.
         * 
         * @param [in] fileTime: Количество 100-наносекундных интервалов с 00:00:00 1 января 1601 года (UTC).
         * 
         * @return Объект DateTime с Kind = Local, представляющий местное время.
         * @exception std::out_of_range Если fileTime < 0 или итоговая дата выходит за пределы 0001-9999 гг.
         * 
         * @note Этот метод эквивалентен вызову FromFileTimeUtc(fileTime).ToLocalTime().
         */
        static DateTime FromFileTime(int64_t fileTime);
        /**
         * @brief Преобразует текущее значение в формат системного времени Windows (FileTime).
         * FileTime отсчитывается от 1 января 1601 года. Если время локальное, оно будет автоматически приведено к UTC.
         *
         * @return 64-битное число тиков для Windows API или 0, если дата ранее 1601 года.
         */
        int64_t ToFileTimeUtc() const;
        /**
         * @brief Преобразует системное время Windows (FILETIME) в объект DateTime (UTC).
         * В Windows FILETIME — это 64-битное значение, представляющее количество
         * 100-наносекундных интервалов (тактов), прошедших с 00:00:00 1 января 1601 года (UTC).
         * 
         * @param [in] fileTime Количество тактов с начала эпохи Windows (1601 год).
         * 
         * @return Объект DateTime в формате UTC.
         * @exception std::out_of_range Если fileTime < 0 или результат превышает MaxValue.
         * Разница между началом эры DateTime (0001г) и эры Windows (1601г)
         * составляет ровно 504,911,232,000,000,000 тактов.
         */
        static DateTime FromFileTimeUtc(int64_t fileTime);
        /**
         * @brief Преобразует текущий экземпляр в формат даты OLE-автоматизации (используется в Excel, COM, VB).
         * Формат OLE Automation Date представляет собой число с плавающей запятой (double):
         * - Целая часть: количество дней, прошедших с 30 декабря 1899 года.
         * - Дробная часть: время дня (например, 0.5 — это 12:00 дня).
         * 
         * @return Число double в формате OLE.
         * @exception std::overflow_error Если дата раньше 1 января 100 года (минимум для OLE).
         * 
         * Для дат до 30.12.1899 (отрицательные значения) используется особая логика:
         * дробная часть времени всегда считается "вперед" от начала суток, что требует
         * математической коррекции при вычислении.
         */
        double ToOADate() const;
        /**
         * @brief Создает экземпляр DateTime из значения даты OLE-автоматизации.
         * Этот метод выполняет "расклейку" отрицательных чисел. Например, OLE-дата -1.25 означает 29 декабря 1899 года 06:00 утра (1 день назад от эпохи + 0.25 дня вперед).
         * 
         * @param [in] value Число double (OLE Date). 0.0 соответствует 30 декабря 1899 года.
         * 
         * @return Объект DateTime с типом Unspecified.
         * @exception std::out_of_range Если значение выходит за пределы (0100г - 9999г).
         */
        static DateTime FromOADate(double value);

        //////////////////////////////////////////////////////////////////////
        ///                     Операторы сравнения                        ///
        //////////////////////////////////////////////////////////////////////
        bool operator==(const DateTime& value) const;
        bool operator!=(const DateTime& value) const;
        bool operator<(const DateTime& value)  const;
        bool operator>(const DateTime& value)  const;
        bool operator<=(const DateTime& value) const;
        bool operator>=(const DateTime& value) const;
        //////////////////////////////////////////////////////////////////////
        ///                   Арифметические операторы                     ///
        //////////////////////////////////////////////////////////////////////
        DateTime operator+(const DateTime& value) const;
        DateTime operator-(const DateTime& value) const;
        DateTime operator+(const TimeSpan& value) const;
        DateTime operator-(const TimeSpan& value) const;
        //////////////////////////////////////////////////////////////////////
        ///           Арифметические операторы с присваиванием             ///
        //////////////////////////////////////////////////////////////////////
        DateTime& operator+=(const DateTime& value);
        DateTime& operator-=(const DateTime& value);
        DateTime& operator+=(const TimeSpan& value);
        DateTime& operator-=(const TimeSpan& value);
        DateTime& operator=(const DateTime& value);
        DateTime& operator=(DateTime&& value) noexcept;

        /**@brief Записывает текстовое представление интервала в поток вывода.
         *
         * @param os Ссылка на поток вывода (например, std::cout или std::ofstream).
         * @param ts Объект TimeSpan для вывода.
         *
         * @return Ссылка на поток вывода для построения цепочек (chaining).
         */
        friend std::ostream& operator<<(std::ostream& os, const DateTime& ts);
        /**@brief Читает текстовое представление интервала из потока ввода.
         *
         * @param is Ссылка на поток ввода (например, std::cin).
         * @param ts Объект для записи результата.
         *
         * @return Ссылка на поток ввода.
         */
        friend std::istream& operator>>(std::istream& is, DateTime& ts);
    };
}

#endif
