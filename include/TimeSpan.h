#ifndef _OTHER_TIMESPAN_H_
#define _OTHER_TIMESPAN_H_

#include <string>
#include <vector>
#include <fstream>


namespace other::time
{
    /**@class TimeSpan
     * @brief Представляет интервал времени (длительность), измеренный в тактах.
     * Класс позволяет выполнять арифметические операции, сравнения и преобразования
     * между различными единицами времени (дни, часы, минуты и т.д.).
     */
    class TimeSpan
    {
    private:
        int64_t _ticks;                    // Внутреннее хранилище времени в тактах (1 такт = 100 нс).

    public:
        static const TimeSpan Zero;        // Нулевой временной интервал.
        static const TimeSpan MaxValue;    // Максимально возможное значение TimeSpan.
        static const TimeSpan MinValue;    // Минимально возможное значение TimeSpan.
        /**@brief Инициализирует новый экземпляр TimeSpan заданным числом тактов.
		 * Допускаются отрицательные значения (для создания отрицательного интервала).
         *
         * @param [in] ticks: Интервал времени, выраженный в единицах измерения, равных 100 нс.
         * 
         * @exception std::out_of_range Если значение ticks меньше MinValue или больше MaxValue.
         */
        explicit TimeSpan(int64_t ticks = 0);
        /**@brief Инициализирует новый экземпляр TimeSpan заданным количеством часов, минут и секунд.
         * Суммирует все компоненты, переводя их в такты.
         * Допускаются отрицательные значения параметров (для создания отрицательных интервалов).
         *
         * @param [in] hours:   Количество часов.
         * @param [in] minutes: Количество минут.
         * @param [in] seconds: Количество секунд.
		 *
         * @exception std::out_of_range Если итоговая сумма тиков выходит за пределы MinValue/MaxValue.
         */
        TimeSpan(int hours, int minutes, int seconds);
        /**@brief Инициализирует новый экземпляр TimeSpan заданным количеством дней, часов, минут, секунд и миллисекунд.
         * Суммирует все компоненты, переводя их в такты.
         * Допускаются отрицательные значения параметров (для создания отрицательных интервалов).
         *
         * @param [in] days:         Количество дней.
         * @param [in] hours:        Количество часов.
         * @param [in] minutes:      Количество минут.
         * @param [in] seconds:      Количество секунд.
         * @param [in] milliseconds: Количество миллисекунд.
		 *
         * @exception std::out_of_range Если итоговая сумма тиков выходит за пределы MinValue/MaxValue.
         */
        TimeSpan(int days, int hours, int minutes, int seconds, int milliseconds = 0);
        /**@brief Конструктор копирования (Copy Constructor).
         * 
         * @param [in] value: Ссылка на существующий объект TimeSpan.
         */
        TimeSpan(const TimeSpan& value);
        /**@brief Конструктор перемещения (Move Constructor).
         * 
         * @param [in] value: Ссылка на временный объект TimeSpan.
         */
        TimeSpan(TimeSpan&& value) noexcept;
        ~TimeSpan() = default;

        /** @return Общее количество тактов. */
        int64_t Ticks() const;
        /** @return Количество целых дней. */
        int Days() const;
        /** @return Количество целых часов (от -23 до 23). */
        int Hours() const;
        /** @return Количество целых минут (от -59 до 59). */
        int Minutes() const;
        /** @return Количество целых секунд (от -59 до 59). */
        int Seconds() const;
        /** @return Количество целых миллисекунд (от -999 до 999). */
        int Milliseconds() const;

        /** @return Общее количество дней в виде дробного числа. */
        double TotalDays() const;
        /** @return Общее количество часов в виде дробного числа. */
        double TotalHours() const;
        /** @return Общее количество минут в виде дробного числа. */
        double TotalMinutes() const;
        /** @return Общее количество секунд в виде дробного числа. */
        double TotalSeconds() const;
        /** @return Общее количество миллисекунд в виде дробного числа. */
        double TotalMilliseconds() const;

        /**@brief Складывает текущий интервал с другим.
         *
         * @param [in] value: Интервал для сложения.
         * 
         * @return Новый объект TimeSpan, представляющий сумму.
         */
        TimeSpan Add(const TimeSpan& value) const;
        /**@brief Вычитает указанный интервал из текущего.
         * 
         * @param [in] value: Вычитаемый интервал.
         * 
         * @return Новый объект TimeSpan, представляющий разность.
         */
        TimeSpan Subtract(const TimeSpan& value) const;
        /**@brief Возвращает абсолютное значение интервала (модуль).
         * @return Новый объект TimeSpan с положительным значением Ticks.
         */
        TimeSpan Duration() const;
        /**@brief Возвращает интервал с отрицательным знаком.
         * @return Новый объект TimeSpan со значением -Ticks.
         * @exception std::out_of_range Если значение равно MinValue (отсутствует положительный эквивалент в доп. коде).
         */
        TimeSpan Negate() const;
        /**@brief Сравнивает текущий обьект с другим объектом.
         * @return -1 (меньше), 0 (равны), 1 (больше).
         */
        int Compare(const TimeSpan& value) const;
        /**@brief Сравнивает текущий обьект с другим объектом на равенство.
         * @return true, если интервалы равны; иначе false.
         */
        bool Equals(const TimeSpan& value) const;
        /**@brief Преобразует интервал в строковое представление.
         * @return Строка в формате [d.]hh:mm:ss[.fff].
         */
        std::string ToString() const;
        /** @brief Преобразует текущее значение TimeSpan в его эквивалентное строковое представление с использованием указанного формата.
         * 
         * @param [in] format: Строка настраиваемого формата.
         * Доступные токены:
         * - "d": количество дней;
         * - "hh": часы (00-23);
         * - "mm": минуты (00-59);
         * - "ss": секунды (00-59);
         * - "fff": миллисекунды (000-999).
         * @return Строковое представление текущего значения TimeSpan, определенное параметром format.
         */
		std::string ToString(const std::string &format) const;
        /**@brief Возвращает хэш-код для данного экземпляра.
         * @return Уникальное числовое представление объекта.
         */
        size_t GetHashCode() const;
        /**@brief Сравнить два обьекта времени между собой.
		 *
         * @param [in] t1 Первый интервал.
         * @param [in] t2 Второй интервал.
		 *
         * @return Результат сравнения (-1, 0, 1).
         */
        static int Compare(const TimeSpan& t1, const TimeSpan& t2);
        /**@brief Проверить два обьекта на равенство.
		 *
         * @param [in] t1 Первый интервал.
         * @param [in] t2 Второй интервал.
		 *
         * @return true, если интервалы равны; иначе false.
         */
        static bool Equals(const TimeSpan& t1, const TimeSpan& t2);
        // Создаёт TimeSpan из указанного количества дробных или целых дней.
        static TimeSpan FromDays(double value);
        // Создаёт TimeSpan из указанного количества дробных или целых часов.
        static TimeSpan FromHours(double value);
        // Создаёт TimeSpan из указанного количества дробных или целых минут.
        static TimeSpan FromMinutes(double value);
        // Создаёт TimeSpan из указанного количества дробных или целых секунд.
        static TimeSpan FromSeconds(double value);
		// Создаёт TimeSpan из указанного количества дробных или целых миллисекунд.
        static TimeSpan FromMilliseconds(double value);
        // Создаёт TimeSpan из указанного количества тактов.
        static TimeSpan FromTicks(int64_t ticks);
		
        /**@brief Преобразует строковое представление интервала времени в TimeSpan.
		 *
         * @param [in] input: Строка, которая указывает преобразуемый интервал времени ([d.hh:mm:ss.fff] 10.13:23:45.500, [hh:mm:ss] 13:23:45).
		 *
         * @return Интервал времени, соответствующий параметру input.
         * @exception std::invalid_argument Если формат строки неверен.
         */
        static TimeSpan Parse(const std::string& input);
        /**@brief Преобразует строковое представление интервала времени в TimeSpan и возвращает значение, позволяющее определить успешность преобразования.
		 *
         * @param [in]  input:  Строка, которая указывает преобразуемый интервал времени.
         * @param [out] result: После возврата из этого метода содержит объект, представляющий интервал времени, заданный в параметре input.
		 *
         * @return true,  если преобразование успешно; иначе false.
         */
        static bool TryParse(const std::string& input, TimeSpan& result);
        /**@brief Преобразует строковое представление интервала времени в TimeSpan, используя указанные формат. 
		 * Формат строкового представления должен полностью соответствовать заданному формату.
		 *
         * @param [in] input:  Строка, которая указывает преобразуемый интервал времени.
         * @param [in] format: Стандартная или пользовательская строка формата, которая определяет требуемый формат параметра input (например, "hh:mm:ss").
		 *
         * @return Интервал времени, соответствующий параметру input, в виде, заданном параметрами format.
         */
        static TimeSpan ParseExact(const std::string& input, const std::string& format);
        /**@brief Преобразует строковое представление интервала времени в TimeSpan, используя список допустимых форматов.
		 *
         * @param [in] input:   Строка с интервалом времени.
         * @param [in] formats: Вектор строк-форматов (напр., {"hh:mm:ss", "d.hh:mm", "h:m:s"}).
		 *
         * @return TimeSpan Объект, полученный из первого подошедшего формата.
         * @exception std::invalid_argument Если строка не подошла ни к одному из форматов.
         */
        static TimeSpan ParseExact(const std::string& input, const std::vector<std::string>& formats);
        /**@brief Преобразует строковое представление интервала времени в TimeSpan, строго по заданному формату.
		 *
         * @param [in]  input:  Входная строка.
         * @param [in]  format: Строка формата (например, "hh:mm:ss").
         * @param [out] result: Ссылка на объект для записи результата.
		 *
         * @return true, если преобразование прошло успешно; иначе false.
         */
        static bool TryParseExact(const std::string& input, const std::string& format, TimeSpan& result);
        /**@brief Преобразует строковое представление интервала времени в TimeSpan, перебирая список форматов.
		 *
         * @param [in]  input:   Входная строка.
         * @param [in]  formats: Вектор строк-форматов.
         * @param [out] result:  Ссылка на объект, в который будет записан результат при успехе.
		 *
         * @return true, если строка подошла хотя бы к одному формату; иначе false.
         * @exception std::invalid_argument Если строка не подошла ни к одному из форматов.
         */
        static bool TryParseExact(const std::string& input, const std::vector<std::string> &formats, TimeSpan& result);
		
        //////////////////////////////////////////////////////////////////////
        ///                     Операторы сравнения                        ///
        //////////////////////////////////////////////////////////////////////
        bool operator==(const TimeSpan& value) const;
        bool operator!=(const TimeSpan& value) const;
        bool operator<(const TimeSpan& value)  const;
        bool operator>(const TimeSpan& value)  const;
        bool operator<=(const TimeSpan& value) const;
        bool operator>=(const TimeSpan& value) const;
        //////////////////////////////////////////////////////////////////////
        ///                   Арифметические операторы                     ///
        //////////////////////////////////////////////////////////////////////
        TimeSpan operator+(const TimeSpan& value) const;
        TimeSpan operator-(const TimeSpan& value) const;
        //////////////////////////////////////////////////////////////////////
        ///           Арифметические операторы с присваиванием             ///
        //////////////////////////////////////////////////////////////////////
        TimeSpan& operator+=(const TimeSpan& value);
        TimeSpan& operator-=(const TimeSpan& value);
        TimeSpan& operator=(const TimeSpan& value);
        TimeSpan& operator=(TimeSpan&& value) noexcept;
        /**@brief Записывает текущий экземпляр TimeSpan в файл в бинарном виде.
         * 
         * @param [in] filename Путь к файлу для записи.
         * 
         * @exception std::ios_base::failure Выбрасывается, если файл не удалось открыть или произошла ошибка записи.
         */
        void Serialize(const std::string& filename);
        /**@brief Читает TimeSpan из указанного бинарного файла.
         * 
         * @param [in] filename Путь к файлу для чтения.
         * 
         * @exception std::ios_base::failure Выбрасывается, если файл не найден или данные повреждены.
         */
        void Deserialize(const std::string& filename);
        /**@brief Записывает текстовое представление интервала в поток вывода.
		 *
         * @param os Ссылка на поток вывода (например, std::cout или std::ofstream).
         * @param ts Объект TimeSpan для вывода.
		 *
         * @return Ссылка на поток вывода для построения цепочек (chaining).
         */
        friend std::ostream& operator<<(std::ostream& os, const TimeSpan& ts);
        /**@brief Читает текстовое представление интервала из потока ввода.
         *
         * @param is Ссылка на поток ввода (например, std::cin).
         * @param ts Объект для записи результата.
		 *
         * @return Ссылка на поток ввода.
         */
        friend std::istream& operator>>(std::istream& is, TimeSpan& ts);
    };
}

#endif
