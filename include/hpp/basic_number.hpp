#ifndef _OTHER_BASIC_NUMBER_HPP_
#define _OTHER_BASIC_NUMBER_HPP_


#include <array>            // std::array
#include <iomanip>          // std::setprecision, std::setfill, std::setw
#include <sstream>          // std::ostream, std::istream
#include <numeric>          // std::numeric_limits
#include <stdint.h>
#include <type_traits>
#include <cstring>


namespace other::hpp
{
    /**
     * @brief Универсальный шаблон-обёртка для арифметических типов данных.
     *
     * @tparam _Ty Базовый арифметический тип (целые числа или числа с плавающей точкой: int, float, char и т.д.).
     *
     * @details
     * Класс `basic_number` расширяет стандартные числовые типы C++, объединяя их
     * с возможностями низкоуровневой работы с памятью, бинарной сериализации и
     * удобного текстового форматирования.
     *
     * Основные возможности:
     * - **Математика и логика**: Полный набор арифметических, логических операторов,
     *   операций сравнения и побитовых сдвигов (с защитой типов на этапе компиляции).
     * - **Низкоуровневый доступ к памяти**: Внутреннее объединение (`union`) предоставляет
     *   прямой доступ к байтам (`std::array`) для анализа порядка байтов (Endianness).
     * - **Сериализация**: Встроенные методы для быстрой записи и чтения бинарных данных из файлов.
     * - **Работа с HEX и строками**: Конвертация в десятичный вид, шестнадцатеричный формат
     *   (`to_hex`, байтовый вывод через пробел) и гибкий парсинг из HEX-строк (`setHexValue`).
     * - **Потоковый ввод-вывод**: Полноценная интеграция со стандартными потоками `std::ostream` и `std::istream`.
     */

#undef min
#undef max

    template <typename _Ty>
    class basic_number
    {
        static_assert(std::is_arithmetic_v<_Ty>, "basic_number supports only arithmetic types");
    public:
        using value_type = _Ty;                      // Тип самого значения
        using basic_type = basic_number<_Ty>;        // Тип текущего класса

    protected:
        /**
         * @brief Внутреннее хранилище данных.
         * Использует union для эффективного маппинга значения на массив байтов.
         */
        union
        {
            value_type value;
            std::array<uint8_t, sizeof(value_type)> bytes;
        } storage;
    public:
        /**
         * @brief Конструктор по умолчанию и конструктор инициализации значением.
         * @param value Начальное значение (по умолчанию 0).
         */
        basic_number(value_type value = 0) noexcept
        {
            storage.value = value;
        }
        /**
         * @brief Конструктор инициализации из сырого указателя на байты.
         * @param bytes Указатель на буфер данных.
         * @exception std::invalid_argument Если передан nullptr.
         */
        basic_number(const void* bytes)
        {
            if (!bytes)
                throw std::invalid_argument("Source buffer is null");

            // Безопасное копирование байтов.
            std::memcpy(storage.bytes.data(), bytes, sizeof(value_type));
        }
        /**
         * @brief Конструктор инициализации из массива байтов std::array.
         * @param bytes Массив байтов соответствующего размера.
         */
        basic_number(const std::array<uint8_t, sizeof(value_type)> &bytes) noexcept
        {
            std::memcpy(storage.bytes.data(), bytes.data(), sizeof(value_type));
        }
        /**
         * @brief Копирующий конструктор.
         * @param other Ссылка на копируемый объект.
         */
        basic_number(const basic_type& other) noexcept = default;
        /**
         * @brief Перемещающий конструктор.
         * @param other R-value ссылка на перемещаемый объект.
         */
        basic_number(basic_type&& other) noexcept = default;
        /**
         * @brief Деструктор.
         * Исполняется по умолчанию, так как класс не владеет динамическими ресурсами.
         */
        ~basic_number() = default;
        /**
         * @brief Возвращает текущее числовое значение.
         * @return Значение типа value_type.
         */
        value_type value() const
        {
            return storage.value;
        }
        /**
         * @brief Экспортирует внутреннее бинарное представление числа.
         * Возвращает копию массива байтов. Благодаря Return Value Optimization (RVO), лишнего копирования при возврате из функции не происходит.
         * @return std::array<uint8_t, sizeof(_Ty)> Массив байтов в формате Little-endian (на большинстве систем).
         */
        std::array<uint8_t, sizeof(_Ty)> bytes() const noexcept
        {
            // std::array поддерживает копирование "из коробки", 
            // поэтому достаточно просто вернуть поле.
            return storage.bytes;
        }
        /**
         * @brief Преобразует значение в строковый формат.
         * Для дробных чисел используется фиксированная точность (6 знаков).
         * @return Строковое представление числа.
         */
        std::string to_string() const
        {
            if constexpr (std::is_floating_point_v<value_type>)
            {
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(6) << storage.value;
                return oss.str();
            }
            else
            {
                return std::to_string(storage.value);
            }
        }
        /**
         * @brief Преобразует число в шестнадцатеричную строку (цельное значение).
         * @return Строка в формате HEX (например, "000000FF").
         */
        std::string to_hex() const
        {
            std::stringstream ss;
            ss << std::hex << std::uppercase << std::setfill('0') << std::setw(sizeof(_Ty) * 2) << storage.value;
            return ss.str();
        }
        /**
         * @brief Преобразует число в HEX-строку с разделением по байтам.
         * Полезно для анализа порядка байтов (Endianness).
         * @return Строка байтов через пробел (например, "FF 00 00 00 ").
         */
        std::string to_hexWithSymbols() const
        {
            std::stringstream ss;
            for (int i = 0; i < sizeof(_Ty); ++i)
            {
                ss << std::setw(2) << std::setfill('0') << std::hex << static_cast<int>(storage.bytes[i]) << " ";
            }
            return ss.str();
        }
        /**
         * @brief Инициализирует значение числа из шестнадцатеричной строки.
         * Поддерживает префиксы 0x/0X и пробелы.
         *
         * @param hex Строка в формате HEX.
         * @exception std::invalid_argument Если строка пуста или содержит некорректные символы.
         */
        void setHexValue(const std::string& hex)
        {
            if (hex.empty())
                throw std::invalid_argument("Hex string is empty.");

            std::string cleanHex = hex;

            // Убираем пробелы с начала и конца строки.
            cleanHex.erase(0, cleanHex.find_first_not_of(" \t\n\r"));
            cleanHex.erase(cleanHex.find_last_not_of(" \t\n\r") + 1);

            if (cleanHex.empty())
                throw std::invalid_argument("Hex string is empty after trimming.");

            // Проверка на наличие "0x" или "0X" в начале строки.
            if (cleanHex.length() >= 2 && (cleanHex.substr(0, 2) == "0x" || cleanHex.substr(0, 2) == "0X"))
            {
                // Удаляем префиксы "0x" или "0X".
                cleanHex = cleanHex.substr(2);
            }

            // Проверка на пустую строку после удаления префикса и пробелов.
            if (cleanHex.empty())
                throw std::invalid_argument("Hex string contains only prefix.");


            // Проверка, что строка состоит только из допустимых шестнадцатеричных символов.
            for (char c : cleanHex)
            {
                if (!std::isxdigit(static_cast<uint8_t>(c)))
                {
                    throw std::invalid_argument("Invalid hex character detected.");
                }
            }

            // Парсим шестнадцатеричное значение с использованием std::stringstream.
            std::stringstream ss;
            ss << std::hex << cleanHex;

            // Используем более общий тип данных для хранения большого значения.
            std::uint64_t parsedValue = 0;
            if (!(ss >> parsedValue))
                throw std::runtime_error("Failed to parse hex string.");

            // Преобразуем полученное значение в нужный тип.
            storage.value = static_cast<value_type>(parsedValue);
        }
        /**
         * @brief Вычисляет хеш-код текущего значения.
         * @return Хеш в формате std::size_t.
         */
        std::size_t hash() const
        {
            return std::hash<value_type>{}(storage.value);
        }
        /**
         * @brief Возвращает хеш-код в виде шестнадцатеричной строки.
         * @return Строка с HEX-представлением хеша.
         */
        std::string hashStr() const
        {
            std::ostringstream oss;
            oss << std::hex << std::uppercase << storage.value;
            return oss.str();
        }
        /**
         * @brief Сравнивает текущий объект с другим на равенство значений.
         * @param other Объект для сравнения.
         * @return true если значения идентичны, иначе false.
         */
        bool equals(const basic_type& other) const
        {
            return (storage.value == other.storage.value);
        }
        /**
         * @brief Записывает бинарное представление числа в файл.
         * @param filename Путь к файлу.
         * @exception std::ios_base::failure При ошибках открытия или записи.
         */
        void serialize(const std::string& filename)
        {
            std::ofstream file(filename, std::ios::binary);
            if (!file)
                throw std::ios_base::failure("Failed to open file for writing");

            file.write(reinterpret_cast<const char*>(storage.bytes.data()), sizeof(_Ty));
        }
        /**
         * @brief Считывает бинарное представление числа из файла.
         * @param filename Путь к файлу.
         * @exception std::ios_base::failure При ошибках открытия или чтения.
         */
        void deserialize(const std::string& filename)
        {
            std::ifstream file(filename, std::ios::binary);
            if (!file)
                throw std::ios_base::failure("Failed to open file for reading");

            file.read(reinterpret_cast<char*>(storage.bytes.data()), sizeof(_Ty));
        }

        //////////////////////////////////////////////////////////////////////
        ///                   Арифметические операторы                     ///
        //////////////////////////////////////////////////////////////////////
        // Оператор сложения
        basic_type operator+(const basic_type& other) const { return basic_type(storage.value + other.storage.value); }
        // Оператор вычитания
        basic_type operator-(const basic_type& other) const { return basic_type(storage.value - other.storage.value); }
        // Оператор умножения
        basic_type operator*(const basic_type& other) const { return basic_type(storage.value * other.storage.value); }
        // Оператор деления с проверкой на ноль
        basic_type operator/(const basic_type& other) const
        {
            if (other.storage.value == 0)
                throw std::runtime_error("Division by zero");

            return basic_type(storage.value / other.storage.value);
        }
        // Оператор взятия остатка (только для целочисленных типов)
        basic_type operator%(const basic_type& other) const
        {
            if constexpr (std::is_integral_v<value_type>)
            {
                if (other.storage.value == 0)
                    throw std::runtime_error("Modulo by zero");

                return basic_type(storage.value % other.storage.value);
            }
            else
            {
                throw std::runtime_error("Modulo operation is only available for integers");
            }
        }

        //////////////////////////////////////////////////////////////////////
        ///           Арифметические операторы с присваиванием             ///
        //////////////////////////////////////////////////////////////////////
        // Сложение с присваиванием
        basic_type& operator+=(const basic_type& other) { storage.value += other.storage.value; return *this; }
        // Вычитание с присваиванием
        basic_type& operator-=(const basic_type& other) { storage.value -= other.storage.value; return *this; }
        // Умножение с присваиванием
        basic_type& operator*=(const basic_type& other) { storage.value *= other.storage.value; return *this; }
        // Деление с присваиванием
        basic_type& operator/=(const basic_type& other)
        {
            if (other.storage.value == 0)
                throw std::runtime_error("Division by zero");

            storage.value /= other.storage.value;
            return *this;
        }
        // Остаток с присваиванием
        basic_type& operator%=(const basic_type& other)
        {
            if constexpr (std::is_integral_v<value_type>)
            {
                if (other.storage.value == 0)
                    throw std::runtime_error("Modulo by zero");

                storage.value %= other.storage.value;
            }
            else
            {
                throw std::runtime_error("Modulo operation is only available for integers");
            }
            return *this;
        }
        // Оператор копирующего присваивания
        basic_type& operator=(const basic_type& other)
        {
            if (this != &other)
            {
                storage.value = other.storage.value;
            }
            return *this;
        }
        // Оператор перемещающего присваивания
        basic_type& operator=(basic_type&& other) noexcept
        {
            if (this != &other)
            {
                storage.value = other.storage.value;
                other.storage.value = 0;
            }
            return *this;
        }

        //////////////////////////////////////////////////////////////////////
        ///                     Операторы сравнения                        ///
        //////////////////////////////////////////////////////////////////////
        bool operator==(const basic_type& other) const { return storage.value == other.storage.value; }
        bool operator!=(const basic_type& other) const { return storage.value != other.storage.value; }
        bool operator<(const basic_type& other)  const { return storage.value < other.storage.value; }
        bool operator>(const basic_type& other)  const { return storage.value > other.storage.value; }
        bool operator<=(const basic_type& other) const { return storage.value <= other.storage.value; }
        bool operator>=(const basic_type& other) const { return storage.value >= other.storage.value; }

        //////////////////////////////////////////////////////////////////////
        ///    Побитовые операторы (только для целых чисел)                ///
        //////////////////////////////////////////////////////////////////////
        // Побитовое И
        basic_type operator&(const basic_type& other) const
        {
            if constexpr (std::is_integral_v<value_type>)
            {
                return basic_type(storage.value & other.storage.value);
            }
            else
            {
                throw std::runtime_error("Bitwise operations are only available for integers");
            }
        }
        // Побитовое ИЛИ
        basic_type operator|(const basic_type& other) const
        {
            if constexpr (std::is_integral_v<value_type>)
            {
                return basic_type(storage.value | other.storage.value);
            }
            else
            {
                throw std::runtime_error("Bitwise operations are only available for integers");
            }
        }
        // Побитовый исключающий ИЛИ (XOR)
        basic_type operator^(const basic_type& other) const
        {
            if constexpr (std::is_integral_v<value_type>)
            {
                return basic_type(storage.value ^ other.storage.value);
            }
            else
            {
                throw std::runtime_error("Bitwise operations are only available for integers");
            }
        }
        // Побитовая инверсия (NOT)
        basic_type operator~() const
        {
            if constexpr (std::is_integral_v<value_type>)
            {
                return basic_type(~storage.value);
            }
            else
            {
                throw std::runtime_error("Bitwise operations are only available for integers");
            }
        }
        // Побитовый сдвиг влево
        basic_type operator<<(int shift) const
        {
            if constexpr (std::is_integral_v<value_type>)
            {
                return basic_type(storage.value << shift);
            }
            else
            {
                throw std::runtime_error("Bitwise operations are only available for integers");
            }
        }
        // Побитовый сдвиг вправо
        basic_type operator>>(int shift) const
        {
            if constexpr (std::is_integral_v<value_type>)
            {
                return basic_type(storage.value >> shift);
            }
            else
            {
                throw std::runtime_error("Bitwise operations are only available for integers");
            }
        }

        //////////////////////////////////////////////////////////////////////
        ///  Побитовые операторы с присваиванием (только для целых чисел)  ///
        //////////////////////////////////////////////////////////////////////
        basic_type& operator&=(const basic_type& other)
        {
            if constexpr (std::is_integral_v<value_type>)
            {
                storage.value &= other.storage.value;
            }
            else
            {
                throw std::runtime_error("Bitwise operations are only available for integers");
            }
            return *this;
        }
        basic_type& operator|=(const basic_type& other)
        {
            if constexpr (std::is_integral_v<value_type>)
            {
                storage.value |= other.storage.value;
            }
            else
            {
                throw std::runtime_error("Bitwise operations are only available for integers");
            }
            return *this;
        }
        basic_type& operator^=(const basic_type& other)
        {
            if constexpr (std::is_integral_v<value_type>)
            {
                storage.value ^= other.storage.value;
            }
            else
            {
                throw std::runtime_error("Bitwise operations are only available for integers");
            }
            return *this;
        }
        basic_type& operator<<=(int shift)
        {
            if constexpr (std::is_integral_v<value_type>)
            {
                storage.value <<= shift;
            }
            else
            {
                throw std::runtime_error("Bitwise operations are only available for integers");
            }
            return *this;
        }
        basic_type& operator>>=(int shift)
        {
            if constexpr (std::is_integral_v<value_type>)
            {
                storage.value >>= shift;
            }
            else
            {
                throw std::runtime_error("Bitwise operations are only available for integers");
            }
            return *this;
        }

        //////////////////////////////////////////////////////////////////////
        ///                    Логические операторы                        ///
        //////////////////////////////////////////////////////////////////////
        // Логическое И
        bool operator&&(const basic_type& other) const { return storage.value && other.storage.value; }
        // Логическое ИЛИ
        bool operator||(const basic_type& other) const { return storage.value || other.storage.value; }
        // Логическое НЕ
        bool operator!() const { return !storage.value; }

        //////////////////////////////////////////////////////////////////////
        ///                    Инкремент и декремент                       ///
        //////////////////////////////////////////////////////////////////////
        // Префиксный инкремент
        basic_type& operator++() { ++storage.value; return *this; }
        // Постфиксный инкремент
        basic_type operator++(int) { basic_type temp = *this; ++storage.value; return temp; }
        // Префиксный декремент
        basic_type& operator--() { --storage.value; return *this; }
        // Постфиксный декремент
        basic_type operator--(int) { basic_type temp = *this; --storage.value; return temp; }

        // Оператор явного преобразования к базовому типу value_type.
        explicit operator value_type() const { return storage.value; }

        //////////////////////////////////////////////////////////////////////
        ///           Операторы ввода и вывода в потоки                    ///
        //////////////////////////////////////////////////////////////////////
        // Оператор вывода в поток.
        friend std::ostream& operator<<(std::ostream& os, const basic_type& num)
        {
            return os << num.value();
        }
        // Оператор ввода из потока.
        friend std::istream& operator>>(std::istream& is, basic_type& num)
        {
            value_type temp;
            is >> temp;

            if (!is.fail())
            {
                num = temp;
            }
            return is;
        }
    };
} // !END namespace hpp



//static void _test_basic_number_()
//{
//    using namespace other::hpp;
//
//    // Тестируем различные типы данных
//    // 1. Тест int8_t
//    basic_number<int8_t> numint8(65);
//    std::cout << "\nTest with int8_t:" << std::endl;
//    assert(numint8.to_string() == "65");
//    std::cout << "Decimal: " << numint8.to_string() << std::endl;
//    std::cout << "Hex: " << numint8.to_hex() << std::endl;
//    std::cout << "Hex with symbols: " << numint8.to_hexWithSymbols() << std::endl;
//    std::cout << "Hash: " << numint8.hash() << std::endl;
//    std::cout << "Hash (HEX): 0x" << numint8.hashStr() << std::endl;
//    numint8.setHexValue("0x41");
//    std::cout << "Updated Decimal: " << numint8.to_string() << std::endl;
//
//    //// 2. Тест int16_t
//    basic_number<int16_t> numint16(65);
//    std::cout << "\nTest with int16_t:" << std::endl;
//    assert(numint16.to_string() == "65");
//    std::cout << "Decimal: " << numint16.to_string() << std::endl;
//    std::cout << "Hex: " << numint16.to_hex() << std::endl;
//    std::cout << "Hex with symbols: " << numint16.to_hexWithSymbols() << std::endl;
//    std::cout << "Hash: " << numint16.hash() << std::endl;
//    std::cout << "Hash (HEX): 0x" << numint16.hashStr() << std::endl;
//    numint16.setHexValue("1E240");
//    std::cout << "Updated Decimal: " << numint16.to_string() << std::endl;
//
//    // 3. Тест int32_t
//    basic_number<int32_t> numint32(123456);
//    std::cout << "\nTest with int32_t:" << std::endl;
//    assert(numint32.to_string() == "123456");
//    std::cout << "Decimal: " << numint32.to_string() << std::endl;
//    std::cout << "Hex: " << numint32.to_hex() << std::endl;
//    std::cout << "Hex with symbols: " << numint32.to_hexWithSymbols() << std::endl;
//    std::cout << "Hash: " << numint32.hash() << std::endl;
//    std::cout << "Hash (HEX): 0x" << numint32.hashStr() << std::endl;
//    numint32.setHexValue("0x1E240");
//    std::cout << "Updated Decimal: " << numint32.to_string() << std::endl;
//
//    // 4. Тест int16_t
//    basic_number<int64_t> numint64(9917381162);
//    std::cout << "\nTest with int64_t:" << std::endl;
//    assert(numint64.to_string() == "9917381162");
//    std::cout << "Decimal: " << numint64.to_string() << std::endl;
//    std::cout << "Hex: " << numint64.to_hex() << std::endl;
//    std::cout << "Hex with symbols: " << numint64.to_hexWithSymbols() << std::endl;
//    std::cout << "Hash: " << numint64.hash() << std::endl;
//    std::cout << "Hash (HEX): 0x" << numint64.hashStr() << std::endl;
//    numint64.setHexValue("0x24F1F3A2A"); // Set from hex
//    std::cout << "Updated Decimal: " << numint64.to_string() << std::endl;
//
//    // 5. Тест int8_t
//    basic_number<uint8_t> numuint8(65);
//    std::cout << "\nTest with uint8_t:" << std::endl;
//    assert(numuint8.to_string() == "65");
//    std::cout << "Decimal: " << numuint8.to_string() << std::endl;
//    std::cout << "Hex: " << numuint8.to_hex() << std::endl;
//    std::cout << "Hex with symbols: " << numuint8.to_hexWithSymbols() << std::endl;
//    std::cout << "Hash: " << numuint8.hash() << std::endl;
//    std::cout << "Hash (HEX): 0x" << numuint8.hashStr() << std::endl;
//    numuint8.setHexValue("0x41");
//    std::cout << "Updated Decimal: " << numuint8.to_string() << std::endl;
//
//    // 6. Тест uint16_t
//    basic_number<uint16_t> numuint16(65);
//    std::cout << "\nTest with uint16_t:" << std::endl;
//    assert(numuint16.to_string() == "65");
//    std::cout << "Decimal: " << numuint16.to_string() << std::endl;
//    std::cout << "Hex: " << numuint16.to_hex() << std::endl;
//    std::cout << "Hex with symbols: " << numuint16.to_hexWithSymbols() << std::endl;
//    std::cout << "Hash: " << numuint16.hash() << std::endl;
//    std::cout << "Hash (HEX): 0x" << numuint16.hashStr() << std::endl;
//    numuint16.setHexValue("0x41");
//    std::cout << "Updated Decimal: " << numuint16.to_string() << std::endl;
//
//    // 7. Тест uint32_t
//    basic_number<uint32_t> numuint32(123456);
//    std::cout << "\nTest with uint32_t:" << std::endl;
//    assert(numuint32.to_string() == "123456");
//    std::cout << "Decimal: " << numuint32.to_string() << std::endl;
//    std::cout << "Hex: " << numuint32.to_hex() << std::endl;
//    std::cout << "Hex with symbols: " << numuint32.to_hexWithSymbols() << std::endl;
//    std::cout << "Hash: " << numuint32.hash() << std::endl;
//    std::cout << "Hash (HEX): 0x" << numuint32.hashStr() << std::endl;
//    numuint32.setHexValue("0x1E240");
//    std::cout << "Updated Decimal: " << numuint32.to_string() << std::endl;
//
//    // 8 Тест uint64_t
//    basic_number<uint64_t> numUint64(9876543210);
//    std::cout << "\nTest with uint64_t:" << std::endl;
//    assert(numUint64.to_string() == "9876543210");
//    std::cout << "Decimal: " << numUint64.to_string() << std::endl;
//    std::cout << "Hex: " << numUint64.to_hex() << std::endl;
//    std::cout << "Hex with symbols: " << numUint64.to_hexWithSymbols() << std::endl;
//    std::cout << "Hash: " << numUint64.hash() << std::endl;
//    std::cout << "Hash (HEX): 0x" << numUint64.hashStr() << std::endl;
//    numUint64.setHexValue("0x24F1F3A2A");
//    std::cout << "Updated Decimal: " << numUint64.to_string() << std::endl;
//
//    // 9. Тест float
//    basic_number<float> numFloat(3.14159f);
//    std::cout << "\nTest with float:" << std::endl;
//    assert(numFloat.to_string() == "3.141590");
//    std::cout << "Decimal: " << numFloat.to_string() << std::endl;
//    std::cout << "Hex: " << numFloat.to_hex() << std::endl;
//    std::cout << "Hex with symbols: " << numFloat.to_hexWithSymbols() << std::endl;
//    std::cout << "Hash: " << numFloat.hash() << std::endl;
//    std::cout << "Hash (HEX): 0x" << numFloat.hashStr() << std::endl;
//    numFloat.setHexValue("40490FDB");
//    std::cout << "Updated Decimal: " << numFloat.to_string() << std::endl;
//
//    // 10. Тест double
//    basic_number<double> numDouble(2.718281828459045);
//    std::cout << "\nTest with double:" << std::endl;
//    assert(numDouble.to_string() == "2.718282");
//    std::cout << "Decimal: " << numDouble.to_string() << std::endl;
//    std::cout << "Hex: " << numDouble.to_hex() << std::endl;
//    std::cout << "Hex with symbols: " << numDouble.to_hexWithSymbols() << std::endl;
//    std::cout << "Hash: " << numDouble.hash() << std::endl;
//    std::cout << "Hash (HEX): 0x" << numDouble.hashStr() << std::endl;
//    numDouble.setHexValue("4005BF0A8B1456D8");
//    std::cout << "Updated Decimal: " << numDouble.to_string() << std::endl;
//
//    // 11. Тест char
//    basic_number<char> numChar(65);
//    std::cout << "\nTest with char:" << std::endl;
//    assert(numChar.to_string() == "65");
//    std::cout << "Decimal: " << numChar.to_string() << std::endl;
//    std::cout << "Hex: " << numChar.to_hex() << std::endl;
//    std::cout << "Hex with symbols: " << numChar.to_hexWithSymbols() << std::endl;
//    std::cout << "Hash: " << numChar.hash() << std::endl;
//    std::cout << "Hash (HEX): 0x" << numChar.hashStr() << std::endl;
//    numChar.setHexValue("0x41");
//    std::cout << "Updated Decimal: " << numChar.to_string() << std::endl;
//
//    // 12. Тест long long
//    basic_number<long long> numLongLong(123456789012345);
//    std::cout << "\nTest with long long:" << std::endl;
//    assert(numLongLong.to_string() == "123456789012345");
//    std::cout << "Decimal: " << numLongLong.to_string() << std::endl;
//    std::cout << "Hex: " << numLongLong.to_hex() << std::endl;
//    std::cout << "Hex with symbols: " << numLongLong.to_hexWithSymbols() << std::endl;
//    std::cout << "Hash: " << numLongLong.hash() << std::endl;
//    std::cout << "Hash (HEX): 0x" << numLongLong.hashStr() << std::endl;
//    numLongLong.setHexValue("0x1B69B4F9D2F85");
//    std::cout << "Updated Decimal: " << numLongLong.to_string() << std::endl;
//
//    // 13. Тест отрицательным числом (int)
//    basic_number<int> numIntNegative(-123456);
//    std::cout << "\nTest with negative int:" << std::endl;
//    assert(numIntNegative.to_string() == "-123456");
//    std::cout << "Decimal: " << numIntNegative.to_string() << std::endl;
//    std::cout << "Hex: " << numIntNegative.to_hex() << std::endl;
//    std::cout << "Hex with symbols: " << numIntNegative.to_hexWithSymbols() << std::endl;
//    std::cout << "Hash: " << numIntNegative.hash() << std::endl;
//    std::cout << "Hash (HEX): 0x" << numIntNegative.hashStr() << std::endl;
//    std::cout << "Updated Decimal: " << numIntNegative.to_string() << std::endl;
//
//    basic_number<int> a(42);
//    basic_number<int> b(10);
//
//    std::cout << "\na: " << a << ", b: " << b << std::endl;
//    std::cout << "a + b = " << (a + b) << std::endl;
//    std::cout << "a - b = " << (a - b) << std::endl;
//    std::cout << "a * b = " << (a * b) << std::endl;
//    std::cout << "a / b = " << (a / b) << std::endl;
//    std::cout << "a % b = " << (a % b) << std::endl;
//
//    std::cout << "a == b: " << (a == b) << std::endl;
//    std::cout << "a != b: " << (a != b) << std::endl;
//    std::cout << "a < b: " << (a < b) << std::endl;
//    std::cout << "a > b: " << (a > b) << std::endl;
//
//    std::cout << "a & b = " << (a & b) << std::endl;
//    std::cout << "a | b = " << (a | b) << std::endl;
//    std::cout << "a ^ b = " << (a ^ b) << std::endl;
//    std::cout << "~a = " << (~a) << std::endl;
//
//    std::cout << "a << 2 = " << (a << 2) << std::endl;
//    std::cout << "a >> 2 = " << (a >> 2) << std::endl;
//
//    std::cout << "Hex of a: " << a.to_hex() << std::endl;
//
//    std::cout << "\nAll tests passed!" << std::endl;
//
//}

#endif // !_BASIC_NUMBER_HPP_

