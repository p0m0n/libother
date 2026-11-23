#ifndef LIBOTHER_NET_BYTE_BUFFER_H
#define LIBOTHER_NET_BYTE_BUFFER_H

#include "Define.h"


namespace other::net::sockets
{
    /**
     * @enum ByteOrder
     * @brief Порядок байт (Endianness), используемый при сериализации и десериализации многобайтовых числовых типов.
     */
    enum class ByteOrder
    {
        LittleEndian, // Младший байт хранится первым (стандарт x86/x64 и многих бинарных протоколов).
        BigEndian,    // Старший байт хранится первым (сетевой порядок байт — Network Byte Order).
        Native        // Использовать нативный порядок байт текущей платформы без перестановки.
    };

    /**
     * @concept BufferScalar
     * @brief Ограничение шаблона для базовых числовых типов (целые и числа с плавающей точкой, исключая bool).
     * Тип bool обрабатывается отдельно через WriteBool/ReadBool для гарантированного 1-байтового формата (0 или 1).
     */
    template <typename T>
    concept BufferScalar = std::is_arithmetic_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool>;

    /**
     * @class ByteBuffer
     * @brief Кроссплатформенный динамический буфер для бинарной сериализации и десериализации сетевых пакетов.
     *
     * Класс управляет непрерывным массивом байтов (`std::vector<uint8_t>`) и предоставляет:
     * - независимые курсоры чтения (`_readCursor`) и записи (`_writeCursor`);
     * - настраиваемый порядок байт (`LittleEndian`, `BigEndian`, `Native`);
     * - раздельные механизмы побитовой записи и чтения (от 1 до 64 бит за операцию);
     * - безопасное чтение и запись строк (сырые строки, C-строки с `\0`, строки с 32-битным префиксом длины) с опциональной проверкой UTF-8;
     * - строгую проверку границ с генерацией `std::out_of_range` вместо неопределённого поведения;
     * - потоковые операторы `<<` и `>>` для всех базовых типов и строк, а также утилиты отладочного вывода (Hex/Decimal/ASCII).
     */
    class ByteBuffer
    {
    public:
        /** @brief Начальная зарезервированная емкость буфера по умолчанию (4 КБ). */
        static constexpr size_t DefaultCapacity = 4096;

    private:
        std::vector<uint8_t> _data;          // Внутреннее хранилище сырых байтов буфера.
        size_t               _readCursor;    // Текущая позиция чтения (смещение в байтах от начала _data).
        size_t               _writeCursor;   // Текущая позиция записи (смещение в байтах от начала _data).
        ByteOrder            _byteOrder;     // Активный порядок байт для числовых операций.

        uint8_t              _writeBitAcc;   // Аккумулятор битов для побитовой записи (собирает до 8 бит).
        uint8_t              _writeBitCount; // Количество накопленных, но еще не сброшенных в _data бит (0..7).

        uint8_t              _readBitCache;  // Кэш текущего байта при побитовом чтении.
        uint8_t              _readBitLeft;   // Количество непрочитанных бит, оставшихся в _readBitCache (0..8).

        /**
         * @brief Конвертирует порядок байт скалярного значения в соответствии с заданным ByteOrder.
         * @tparam T Арифметический тип значения.
         * @param [in] value Исходное значение.
         * @param [in] order Целевой порядок байт.
         * @return Значение с переставленными байтами (если требуется) либо исходное значение.
         */
        template <typename T>
        static T ConvertEndian(T value, ByteOrder order) noexcept
        {
            // Для 1-байтовых типов порядок байт не имеет значения.
            if constexpr (sizeof(T) == 1)
            {
                LIBOTHER_UNUSED(order); // Подавляем предупреждение о неиспользуемом параметре.
                return value;           // Возвращаем байт без изменений.
            }
            else
            {
                // Определяем, отличается ли целевой порядок байт от нативного порядка процессора.
                const bool needSwap =
                    (order == ByteOrder::BigEndian    && std::endian::native == std::endian::little) ||
                    (order == ByteOrder::LittleEndian && std::endian::native == std::endian::big);

                // Если перестановка не нужна, возвращаем исходное значение.
                if (!needSwap)
                    return value;

                uint8_t bytes[sizeof(T)];               // Временный байтовый буфер для безопасного разворота.
                std::memcpy(bytes, &value, sizeof(T));  // Копируем байтовое представление значения.
                std::reverse(bytes, bytes + sizeof(T)); // Разворачиваем порядок байт.
                T swapped{};                            // Переменная для результата.
                std::memcpy(&swapped, bytes, sizeof(T));// Восстанавливаем значение из развернутых байтов.
                return swapped;                         // Возвращаем сконвертированное значение.
            }
        }

        /**
         * @brief Формирует сообщение об ошибке и выбрасывает std::out_of_range при выходе за границы буфера.
         * @param [in] offset    Запрошенное смещение в буфере.
         * @param [in] requested Запрошенный размер данных в байтах.
         * @exception std::out_of_range Выбрасывается всегда.
         */
        [[noreturn]] void ThrowOutOfBounds(size_t offset, size_t requested) const;

    public:
        /**
         * @brief Создает пустой буфер с предварительно зарезервированной емкостью.
         * @param [in] initialCapacity Начальный объем резервируемой памяти в байтах.
         * @param [in] order           Порядок байт для числовых операций.
         */
        explicit ByteBuffer(size_t initialCapacity = DefaultCapacity, ByteOrder order = ByteOrder::LittleEndian);

        /**
         * @brief Создает буфер, принимая во владение готовый вектор байтов (без копирования).
         * Курсор чтения устанавливается в 0, курсор записи — в конец вектора.
         * @param [in] initialData Вектор исходных байтов.
         * @param [in] order       Порядок байт для числовых операций.
         */
        explicit ByteBuffer(std::vector<uint8_t> initialData, ByteOrder order = ByteOrder::LittleEndian) noexcept;

        /**
         * @brief Создает буфер и копирует в него указанный блок сырых байтов.
         * @param [in] data  Указатель на исходный массив байтов.
         * @param [in] size  Количество байтов для копирования.
         * @param [in] order Порядок байт для числовых операций.
         */
        ByteBuffer(const uint8_t* data, size_t size, ByteOrder order = ByteOrder::LittleEndian);

        /** @brief Конструктор копирования (создает независимую копию данных и курсоров). */
        ByteBuffer(const ByteBuffer&) = default;
        /** @brief Оператор копирующего присваивания. */
        ByteBuffer& operator=(const ByteBuffer&) = default;
        /** @brief Конструктор перемещения (переносит буфер за O(1) и обнуляет исходный объект). */
        ByteBuffer(ByteBuffer&& other) noexcept;
        /** @brief Оператор перемещающего присваивания (переносит буфер за O(1) и очищает исходный объект). */
        ByteBuffer& operator=(ByteBuffer&& other) noexcept;
        /** @brief Деструктор по умолчанию. */
        ~ByteBuffer() = default;

        // ==========================================
        // НАСТРОЙКИ И УПРАВЛЕНИЕ ПАМЯТЬЮ
        // ==========================================

        /** @brief Возвращает текущий порядок байт буфера. */
        ByteOrder GetByteOrder() const noexcept { return _byteOrder; }
        /** @brief Устанавливает новый порядок байт для последующих операций чтения/записи. */
        void SetByteOrder(ByteOrder order) noexcept { _byteOrder = order; }

        /** @brief Возвращает изменяемый указатель на начало сырого массива байтов. */
        uint8_t* Data() noexcept { return _data.data(); }
        /** @brief Возвращает константный указатель на начало сырого массива байтов. */
        const uint8_t* Data() const noexcept { return _data.data(); }

        /** @brief Возвращает текущее количество записанных байтов в буфере. */
        size_t Size() const noexcept { return _data.size(); }
        /** @brief Возвращает текущую зарезервированную емкость хранилища в байтах. */
        size_t Capacity() const noexcept { return _data.capacity(); }
        /** @brief Проверяет, пуст ли буфер (Size() == 0). */
        bool Empty() const noexcept { return _data.empty(); }
        /** @brief Возвращает количество байтов, оставшихся для чтения от _readCursor до конца буфера. */
        size_t Remaining() const noexcept { return (_readCursor < _data.size()) ? (_data.size() - _readCursor) : 0; }

        /** @brief Возвращает текущее смещение курсора чтения. */
        size_t ReadOffset() const noexcept { return _readCursor; }
        /**
         * @brief Устанавливает новую позицию курсора чтения и сбрасывает битовый кэш чтения.
         * @param [in] offset Новая позиция (от 0 до Size()).
         * @exception std::out_of_range Если offset > Size().
         */
        void SetReadOffset(size_t offset);

        /** @brief Возвращает текущее смещение курсора записи. */
        size_t WriteOffset() const noexcept { return _writeCursor; }
        /**
         * @brief Выравнивает недописанные биты и устанавливает новую позицию курсора записи.
         * @param [in] offset Новая позиция (от 0 до Size()).
         * @exception std::out_of_range Если offset > Size().
         */
        void SetWriteOffset(size_t offset);

        /** @brief Полностью очищает содержимое буфера и сбрасывает все курсоры и битовые буферы. */
        void Clear() noexcept;
        /** @brief Сбрасывает курсор чтения и битовый кэш чтения в начало буфера без удаления данных. */
        void Rewind() noexcept;
        /** @brief Резервирует память под не менее чем newCapacity байтов без изменения Size(). */
        void Reserve(size_t newCapacity);
        /** @brief Изменяет размер буфера до newSize (заполняя новые байты нулями) и обновляет курсоры. */
        void Resize(size_t newSize);
        /** @brief Освобождает неиспользуемую зарезервированную память внутреннего вектора. */
        void ShrinkToFit();

        /**
         * @brief Выравнивает недописанные биты, отдает владение внутренним вектором байтов и очищает буфер.
         * @return Вектор байтов, ранее хранившийся в буфере.
         */
        std::vector<uint8_t> TakeData() noexcept;

        // ==========================================
        // ЗАПИСЬ ДАННЫХ
        // ==========================================

        /**
         * @brief Записывает скалярное числовое значение с учетом текущего ByteOrder.
         * @tparam T Арифметический тип (удовлетворяющий BufferScalar).
         * @param [in] value Записываемое значение.
         */
        template <BufferScalar T>
        void Write(T value)
        {
            T encoded = ConvertEndian(value, _byteOrder);                   // Приводим значение к нужному порядку байт.
            WriteBytes(reinterpret_cast<const uint8_t*>(&encoded), sizeof(T)); // Записываем байты в буфер.
        }

        /** @brief Записывает логическое значение как 1 байт (1 для true, 0 для false). */
        void WriteBool(bool value);
        /**
         * @brief Записывает произвольный массив байтов в текущую позицию записи.
         * @param [in] src   Указатель на исходные байты.
         * @param [in] count Количество байтов для записи.
         * @exception std::invalid_argument Если count > 0 и src == nullptr.
         * @exception std::length_error     При переполнении размера буфера.
         */
        void WriteBytes(const uint8_t* src, size_t count);
        /** @brief Дописывает всё содержимое другого ByteBuffer в текущий буфер. */
        void WriteBuffer(const ByteBuffer& other);

        /** @brief Записывает сырые байты строки без завершающего нуля. */
        void WriteString(std::string_view text);
        /** @brief Записывает строку с завершающим нулевым байтом ('\0'). */
        void WriteCString(std::string_view text);
        /** @brief Записывает 32-битную длину строки (uint32_t), а затем её байты без '\0'. */
        void WriteLengthPrefixedString(std::string_view text);

        /**
         * @brief Перезаписывает скалярное значение по заданному смещению без изменения _writeCursor.
         * @tparam T Арифметический тип (BufferScalar).
         * @param [in] offset Смещение в байтах от начала буфера.
         * @param [in] value  Новое значение.
         * @exception std::out_of_range Если запись выходит за границы Size().
         */
        template <BufferScalar T>
        void WriteAt(size_t offset, T value)
        {
            T encoded = ConvertEndian(value, _byteOrder);                             // Конвертируем порядок байт.
            WriteBytesAt(offset, reinterpret_cast<const uint8_t*>(&encoded), sizeof(T)); // Записываем по смещению.
        }

        /**
         * @brief Перезаписывает блок байтов по заданному смещению без сдвига курсора записи.
         * @param [in] offset Смещение в байтах от начала буфера.
         * @param [in] src    Указатель на источник данных.
         * @param [in] count  Количество байтов.
         * @exception std::invalid_argument Если count > 0 и src == nullptr.
         * @exception std::out_of_range     Если [offset, offset + count) выходит за границы буфера.
         */
        void WriteBytesAt(size_t offset, const uint8_t* src, size_t count);

        // ==========================================
        // ЧТЕНИЕ ДАННЫХ
        // ==========================================

        /**
         * @brief Читает скалярное числовое значение из текущей позиции и сдвигает _readCursor.
         * @tparam T Арифметический тип (BufferScalar).
         * @return Прочитанное значение с учетом ByteOrder.
         * @exception std::out_of_range Если в буфере недостаточно байтов.
         */
        template <BufferScalar T>
        T Read()
        {
            AlignReadToByte();             // Сбрасываем остаток битового кэша перед байтовым чтением.
            T value = Peek<T>(_readCursor);// Считываем значение с проверкой границ и конвертацией Endianness.
            _readCursor += sizeof(T);      // Сдвигаем курсор чтения на размер типа.
            return value;                  // Возвращаем прочитанное значение.
        }

        /**
         * @brief Читает скалярное значение по заданному смещению без сдвига _readCursor.
         * @tparam T Арифметический тип (BufferScalar).
         * @param [in] offset Смещение в байтах от начала буфера.
         * @return Значение по указанному смещению.
         * @exception std::out_of_range Если чтение выходит за границы буфера.
         */
        template <BufferScalar T>
        T Peek(size_t offset) const
        {
            // Проверяем границы безопасным способом без переполнения offset + sizeof(T).
            if (offset > _data.size() || sizeof(T) > _data.size() - offset)
                ThrowOutOfBounds(offset, sizeof(T));

            T raw{};                                       // Временная переменная для сырых байтов.
            std::memcpy(&raw, &_data[offset], sizeof(T));  // Копируем байты из хранилища.
            return ConvertEndian(raw, _byteOrder);         // Приводим к нативному порядку байт.
        }

        /** @brief Читает 1 байт и возвращает true, если он не равен 0. */
        bool ReadBool();
        /**
         * @brief Читает указанное количество байтов в предоставленный буфер dst.
         * @param [out] dst   Указатель на целевой буфер.
         * @param [in]  count Количество байтов для чтения.
         * @exception std::invalid_argument Если count > 0 и dst == nullptr.
         * @exception std::out_of_range     Если в буфере недостаточно байтов.
         */
        void ReadBytes(uint8_t* dst, size_t count);
        /** @brief Читает count байтов и возвращает их в новом std::vector<uint8_t>. */
        std::vector<uint8_t> ReadBytes(size_t count);

        /**
         * @brief Читает строку фиксированной длины length байтов.
         * @param [in] length       Количество байтов для чтения.
         * @param [in] validateUtf8 Если true, проверяет валидность UTF-8 и бросает std::invalid_argument при ошибке.
         * @return Прочитанная строка.
         */
        std::string ReadString(size_t length, bool validateUtf8 = false);
        /**
         * @brief Читает нуль-терминированную C-строку до первого байта '\0'.
         * @param [in] validateUtf8 Если true, проверяет валидность UTF-8.
         * @return Прочитанная строка (без завершающего '\0').
         * @exception std::out_of_range Если терминатор '\0' не найден до конца буфера.
         */
        std::string ReadCString(bool validateUtf8 = false);
        /**
         * @brief Читает 32-битную длину (uint32_t) и последующую строку этой длины.
         * @param [in] validateUtf8 Если true, проверяет валидность UTF-8.
         * @return Прочитанная строка.
         */
        std::string ReadLengthPrefixedString(bool validateUtf8 = false);

        /**
         * @brief Пропускает указанное количество байтов, сдвигая курсор чтения вперед.
         * @param [in] bytes Количество пропускаемых байтов.
         * @exception std::out_of_range Если пропуск выходит за границы буфера.
         */
        void Skip(size_t bytes);

        // ==========================================
        // ПОБИТОВАЯ УПАКОВКА / РАСПАКОВКА
        // ==========================================

        /** @brief Записывает один бит (true = 1, false = 0) в битовый аккумулятор. */
        void WriteBit(bool bit);
        /**
         * @brief Записывает младшие bitCount бит из value (от старшего к младшему).
         * @param [in] value    Исходное значение.
         * @param [in] bitCount Количество записываемых бит (1..64).
         * @exception std::out_of_range Если bitCount == 0 или bitCount > 64.
         */
        void WriteBits(uint64_t value, uint8_t bitCount);
        /** @brief Сбрасывает недописанный байт битового аккумулятора в буфер, дополняя младшие биты нулями. */
        void AlignWriteToByte();

        /** @brief Читает один бит из потока (при необходимости подгружая следующий байт). */
        bool ReadBit();
        /**
         * @brief Читает bitCount бит и возвращает их как uint64_t (в младших битах результата).
         * @param [in] bitCount Количество читаемых бит (1..64).
         * @return Прочитанное беззнаковое значение.
         * @exception std::out_of_range Если bitCount == 0, bitCount > 64 или не хватает данных в буфере.
         */
        uint64_t ReadBits(uint8_t bitCount);
        /** @brief Сбрасывает остаток битового кэша чтения, выравнивая чтение по границе следующего байта. */
        void AlignReadToByte() noexcept;

        //////////////////////////////////////////////////////////////////////
        ///              ПОТОКОВЫЕ ОПЕРАТОРЫ ЗАПИСИ                        ///
        //////////////////////////////////////////////////////////////////////

        /** @brief Запрещает неявную запись произвольных указателей через приведение к bool. */
        ByteBuffer& operator<<(const void*) = delete;
        /** @brief Записывает bool (1 байт) и возвращает ссылку на буфер для цепочки вызовов. */
        ByteBuffer& operator<<(bool value);
        /** @brief Записывает char (1 байт). */
        ByteBuffer& operator<<(char value);
        /** @brief Записывает беззнаковое 8-битное целое. */
        ByteBuffer& operator<<(uint8_t value);
        /** @brief Записывает беззнаковое 16-битное целое с учетом ByteOrder. */
        ByteBuffer& operator<<(uint16_t value);
        /** @brief Записывает беззнаковое 32-битное целое с учетом ByteOrder. */
        ByteBuffer& operator<<(uint32_t value);
        /** @brief Записывает беззнаковое 64-битное целое с учетом ByteOrder. */
        ByteBuffer& operator<<(uint64_t value);
        /** @brief Записывает знаковое 8-битное целое. */
        ByteBuffer& operator<<(int8_t value);
        /** @brief Записывает знаковое 16-битное целое с учетом ByteOrder. */
        ByteBuffer& operator<<(int16_t value);
        /** @brief Записывает знаковое 32-битное целое с учетом ByteOrder. */
        ByteBuffer& operator<<(int32_t value);
        /** @brief Записывает знаковое 64-битное целое с учетом ByteOrder. */
        ByteBuffer& operator<<(int64_t value);
        /** @brief Записывает 32-битное число с плавающей точкой (float). */
        ByteBuffer& operator<<(float value);
        /** @brief Записывает 64-битное число с плавающей точкой (double). */
        ByteBuffer& operator<<(double value);
        /** @brief Записывает строку std::string_view как нуль-терминированную C-строку. */
        ByteBuffer& operator<<(std::string_view value);
        /** @brief Записывает строку std::string как нуль-терминированную C-строку. */
        ByteBuffer& operator<<(std::string const& str);
        /** @brief Записывает C-строку const char* (nullptr трактуется как пустая строка ""). */
        ByteBuffer& operator<<(char const* str);

        //////////////////////////////////////////////////////////////////////
        ///              ПОТОКОВЫЕ ОПЕРАТОРЫ ЧТЕНИЯ                        ///
        //////////////////////////////////////////////////////////////////////

        /** @brief Читает bool (1 байт: 0 -> false, не 0 -> true). */
        ByteBuffer& operator>>(bool& value);
        /** @brief Читает char (1 байт). */
        ByteBuffer& operator>>(char& value);
        /** @brief Читает беззнаковое 8-битное целое. */
        ByteBuffer& operator>>(uint8_t& value);
        /** @brief Читает беззнаковое 16-битное целое с учетом ByteOrder. */
        ByteBuffer& operator>>(uint16_t& value);
        /** @brief Читает беззнаковое 32-битное целое с учетом ByteOrder. */
        ByteBuffer& operator>>(uint32_t& value);
        /** @brief Читает беззнаковое 64-битное целое с учетом ByteOrder. */
        ByteBuffer& operator>>(uint64_t& value);
        /** @brief Читает знаковое 8-битное целое. */
        ByteBuffer& operator>>(int8_t& value);
        /** @brief Читает знаковое 16-битное целое с учетом ByteOrder. */
        ByteBuffer& operator>>(int16_t& value);
        /** @brief Читает знаковое 32-битное целое с учетом ByteOrder. */
        ByteBuffer& operator>>(int32_t& value);
        /** @brief Читает знаковое 64-битное целое с учетом ByteOrder. */
        ByteBuffer& operator>>(int64_t& value);
        /** @brief Читает 32-битное число с плавающей точкой (float). */
        ByteBuffer& operator>>(float& value);
        /** @brief Читает 64-битное число с плавающей точкой (double). */
        ByteBuffer& operator>>(double& value);
        /** @brief Читает нуль-терминированную C-строку в std::string. */
        ByteBuffer& operator>>(std::string& value);

        // ==========================================
        // ИНДЕКСАЦИЯ И ДИАГНОСТИКА
        // ==========================================

        /**
         * @brief Возвращает изменяемую ссылку на байт по индексу с проверкой границ.
         * @exception std::out_of_range Если index >= Size().
         */
        uint8_t& operator[](size_t index);
        /**
         * @brief Возвращает константную ссылку на байт по индексу с проверкой границ.
         * @exception std::out_of_range Если index >= Size().
         */
        const uint8_t& operator[](size_t index) const;

        /** @brief Форматирует содержимое буфера в читаемую шестнадцатеричную строку (по 16 байт в строке). */
        std::string ToHexString() const;
        /** @brief Форматирует содержимое буфера в строку десятичных значений байтов через пробел. */
        std::string ToDecimalString() const;
        /** @brief Форматирует содержимое буфера как ASCII-строку (непечатаемые байты заменяются на '.'). */
        std::string ToAsciiString() const;
    };
}

#endif // LIBOTHER_NET_BYTE_BUFFER_H
