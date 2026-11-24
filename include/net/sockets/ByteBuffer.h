#ifndef LIBOTHER_NET_BYTE_BUFFER_H
#define LIBOTHER_NET_BYTE_BUFFER_H


#include "Define.h"


namespace other::net::sockets
{
    /**
     * @enum ByteOrder
     * @brief Порядок байтов (Endianness), используемый при сериализации и десериализации многобайтовых числовых типов.
     */
    enum class ByteOrder
    {
        LittleEndian, ///< Младший байт хранится первым (стандарт x86/x64 и многих бинарных протоколов).
        BigEndian,    ///< Старший байт хранится первым (сетевой порядок байтов — Network Byte Order).
        Native        ///< Использовать нативный порядок байтов текущей платформы без перестановки.
    };

    /**
     * @concept BufferScalar
     * @brief Ограничение шаблона для базовых числовых типов (целые и числа с плавающей точкой, исключая `bool`).
     *
     * Тип `bool` обрабатывается отдельно через `WriteBool()` / `ReadBool()` для гарантированного 1-байтового формата (`0` или `1`).
     */
    template <typename T>
    concept BufferScalar = std::is_arithmetic_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool>;

    /**
     * @class ByteBuffer
     * @brief Динамический буфер для бинарной сериализации и десериализации сетевых пакетов.
     *
     * Класс управляет непрерывным массивом байтов (`std::vector<uint8_t>`) и предоставляет:
     * - независимые курсоры чтения (`_readCursor`) и записи (`_writeCursor`);
     * - настраиваемый порядок байтов (`LittleEndian`, `BigEndian`, `Native`);
     * - раздельные механизмы побитовой записи и чтения (от 1 до 64 бит за операцию);
     * - безопасное чтение и запись строк (сырые строки, C-строки с `\0`, строки с 32-битным префиксом длины) с опциональной проверкой UTF-8;
     * - строгую проверку границ с генерацией `std::out_of_range` вместо неопределённого поведения;
     * - потоковые операторы `<<` и `>>` для всех базовых типов и строк, а также утилиты отладочного вывода (Hex/Decimal/ASCII).
     *
     * Политика обработки ошибок:
     * Все нарушения границ, некорректные аргументы и переполнения сигнализируются стандартными исключениями
     * (`std::out_of_range`, `std::invalid_argument`, `std::length_error`, `std::bad_alloc`) с сохранением
     * консистентного состояния курсоров и битовых аккумуляторов.
     */
    class ByteBuffer
    {
    private:
        std::vector<uint8_t> _data;          ///< Внутреннее хранилище сырых байтов буфера.
        size_t               _readCursor;    ///< Текущая позиция чтения (смещение в байтах от начала `_data`).
        size_t               _writeCursor;   ///< Текущая позиция записи (смещение в байтах от начала `_data`).
        ByteOrder            _byteOrder;     ///< Активный порядок байтов для многобайтовых числовых операций.

        uint8_t              _writeBitAcc;   ///< Аккумулятор битов для побитовой записи (собирает до 8 бит).
        uint8_t              _writeBitCount; ///< Количество накопленных, но ещё не сброшенных в `_data` бит (0..7).

        uint8_t              _readBitCache;  ///< Кэш текущего байта при побитовом чтении.
        uint8_t              _readBitLeft;   ///< Количество непрочитанных бит, оставшихся в `_readBitCache` (0..8).

    protected:
        /**
         * @brief Конвертирует порядок байтов скалярного значения в соответствии с заданным `ByteOrder`.
         * @tparam T Арифметический тип значения.
         * @param [in] value Исходное значение.
         * @param [in] order Целевой порядок байтов.
         * @return Значение с переставленными байтами (если требуется) либо исходное значение.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        template <typename T>
        static T ConvertEndian(T value, ByteOrder order) noexcept
        {
            // Для 1-байтовых типов порядок байтов не имеет значения.
            if constexpr (sizeof(T) == 1)
            {
                LIBOTHER_UNUSED(order); // Подавляем предупреждение о неиспользуемом параметре.
                return value;           // Возвращаем байт без изменений.
            }
            else
            {
                // Определяем, отличается ли целевой порядок байтов от нативного порядка процессора.
                const bool needSwap = (order == ByteOrder::BigEndian    && std::endian::native == std::endian::little) ||
                                      (order == ByteOrder::LittleEndian && std::endian::native == std::endian::big);

                // Если перестановка не нужна, возвращаем исходное значение.
                if (!needSwap)
                    return value;

                uint8_t bytes[sizeof(T)];                // Временный байтовый буфер для безопасного разворота.
                std::memcpy(bytes, &value, sizeof(T));   // Копируем байтовое представление значения.
                std::reverse(bytes, bytes + sizeof(T));  // Разворачиваем порядок байтов.
                T swapped{};                             // Переменная для результата.
                std::memcpy(&swapped, bytes, sizeof(T)); // Восстанавливаем значение из развёрнутых байтов.
                return swapped;                          // Возвращаем сконвертированное значение.
            }
        }

        /**
         * @brief Формирует диагностическое сообщение и выбрасывает `std::out_of_range` при выходе за границы буфера.
         * @param [in] offset    Запрошенное смещение в буфере.
         * @param [in] requested Запрошенный размер данных в байтах.
         * @throws std::out_of_range Выбрасывается всегда.
         */
        void ThrowOutOfBounds(size_t offset, size_t requested) const;

    public:
        /** @brief Начальная зарезервированная ёмкость буфера по умолчанию (4 КБ). */
        static constexpr size_t DefaultCapacity = 4096;

        /**
         * @brief Создаёт пустой буфер с предварительно зарезервированной ёмкостью.
         * @param [in] initialCapacity Начальный объём резервируемой памяти в байтах (по умолчанию 4096).
         * @param [in] order           Порядок байтов для числовых операций (по умолчанию `LittleEndian`).
         * @throws std::bad_alloc При нехватке памяти для резервирования `initialCapacity` байтов.
         */
        explicit ByteBuffer(size_t initialCapacity = DefaultCapacity, ByteOrder order = ByteOrder::LittleEndian);
        /**
         * @brief Создаёт буфер, принимая во владение готовый вектор байтов (без копирования).
         *
         * Курсор чтения устанавливается в `0`, курсор записи — в конец вектора (`initialData.size()`).
         * @param [in] initialData Вектор исходных байтов, передаваемый во владение.
         * @param [in] order       Порядок байтов для числовых операций.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        explicit ByteBuffer(std::vector<uint8_t> initialData, ByteOrder order = ByteOrder::LittleEndian) noexcept;
        /**
         * @brief Создаёт буфер и копирует в него указанный блок сырых байтов.
         * @param [in] data  Указатель на исходный массив байтов (не может быть `nullptr` при `size > 0`).
         * @param [in] size  Количество байтов для копирования.
         * @param [in] order Порядок байтов для числовых операций.
         * @throws std::invalid_argument Если `size > 0` и `data == nullptr`.
         * @throws std::bad_alloc        При нехватке памяти для копирования блока.
         */
        ByteBuffer(const uint8_t* data, size_t size, ByteOrder order = ByteOrder::LittleEndian);
        /**
         * @brief Конструктор копирования: создаёт независимую копию данных, курсоров и битовых буферов.
         * @throws std::bad_alloc При нехватке памяти для копирования внутреннего вектора.
         */
        ByteBuffer(const ByteBuffer&) = default;
        /**
         * @brief Оператор копирующего присваивания.
         * @return Ссылка на текущий объект (`*this`).
         * @throws std::bad_alloc При нехватке памяти для копирования внутреннего вектора.
         */
        ByteBuffer& operator=(const ByteBuffer&) = default;
        /**
         * @brief Конструктор перемещения: переносит буфер за O(1) и обнуляет исходный объект.
         * @param [in,out] other Исходный буфер (после перемещения становится пустым).
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        ByteBuffer(ByteBuffer&& other) noexcept;
        /**
         * @brief Оператор перемещающего присваивания: переносит буфер за O(1) и очищает исходный объект.
         * @param [in,out] other Исходный буфер (после перемещения становится пустым).
         * @return Ссылка на текущий объект (`*this`).
         * @note Гарантированно не выбрасывает исключений (`noexcept`). Безопасен при самоприсваивании.
         */
        ByteBuffer& operator=(ByteBuffer&& other) noexcept;
        /**
         * @brief Деструктор по умолчанию: освобождает внутреннюю память буфера.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        ~ByteBuffer() noexcept = default;


		///////////////////////////////////////////////////////////////////////
        ///            НАСТРОЙКИ И УПРАВЛЕНИЕ ПАМЯТЬЮ                       ///
        ///////////////////////////////////////////////////////////////////////
        /**
         * @brief Возвращает текущий порядок байтов буфера.
         * @return Активное значение `ByteOrder`.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        ByteOrder GetByteOrder() const noexcept { return _byteOrder; }
        /**
         * @brief Устанавливает новый порядок байтов для последующих числовых операций чтения и записи.
         * @param [in] order Новый порядок байтов.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        void SetByteOrder(ByteOrder order) noexcept { _byteOrder = order; }
        /**
         * @brief Возвращает изменяемый указатель на начало сырого массива байтов.
         * @return Указатель на первый байт хранилища (или валидный указатель/nullptr для пустого буфера).
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        uint8_t* Data() noexcept { return _data.data(); }
        /**
         * @brief Возвращает константный указатель на начало сырого массива байтов.
         * @return Константный указатель на первый байт хранилища.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        const uint8_t* Data() const noexcept { return _data.data(); }
        /**
         * @brief Возвращает текущее количество записанных байтов в буфере (без учёта незавершённых битов).
         * @return Размер буфера в байтах.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        size_t Size() const noexcept { return _data.size(); }
        /**
         * @brief Возвращает текущую зарезервированную ёмкость внутреннего хранилища в байтах.
         * @return Ёмкость вектора `_data` в байтах.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        size_t Capacity() const noexcept { return _data.capacity(); }
        /**
         * @brief Проверяет, пуст ли буфер (`Size() == 0`).
         * @return `true`, если в `_data` нет записанных байтов; `false` иначе.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        bool Empty() const noexcept { return _data.empty(); }
        /**
         * @brief Возвращает количество байтов, оставшихся для чтения от `_readCursor` до конца буфера.
         * @return Число непрочитанных байтов (`0`, если курсор чтения достиг конца).
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        size_t Remaining() const noexcept { return (_readCursor < _data.size()) ? (_data.size() - _readCursor) : 0; }
        /**
         * @brief Возвращает текущее смещение курсора чтения в байтах.
         * @return Смещение от начала буфера (`0..Size()`).
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        size_t ReadOffset() const noexcept { return _readCursor; }
        /**
         * @brief Устанавливает новую позицию курсора чтения и сбрасывает битовый кэш чтения.
         * @param [in] offset Новая позиция курсора (от `0` до `Size()`).
         * @throws std::out_of_range Если `offset > Size()`.
         */
        void SetReadOffset(size_t offset);
        /**
         * @brief Возвращает текущее смещение курсора записи в байтах.
         * @return Смещение от начала буфера (`0..Size()`).
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        size_t WriteOffset() const noexcept { return _writeCursor; }
        /**
         * @brief Выравнивает недописанные биты и устанавливает новую позицию курсора записи.
         * @param [in] offset Новая позиция курсора (от `0` до `Size()`).
         * @throws std::out_of_range Если `offset > Size()`.
         * @throws std::bad_alloc    При нехватке памяти для сброса незавершённого битового байта.
         * @throws std::length_error При переполнении размера буфера при сбросе битового байта.
         */
        void SetWriteOffset(size_t offset);
        /**
         * @brief Полностью очищает содержимое буфера и сбрасывает все курсоры и битовые буферы.
         * @note Гарантированно не выбрасывает исключений (`noexcept`). Выделенная ёмкость (`Capacity()`) сохраняется.
         */
        void Clear() noexcept;
        /**
         * @brief Сбрасывает курсор чтения и битовый кэш чтения в начало буфера без удаления записанных данных.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        void Rewind() noexcept;
        /**
         * @brief Резервирует память под не менее чем `newCapacity` байтов без изменения `Size()`.
         * @param [in] newCapacity Желаемая минимальная ёмкость буфера в байтах.
         * @throws std::bad_alloc    При нехватке памяти.
         * @throws std::length_error Если `newCapacity > _data.max_size()`.
         */
        void Reserve(size_t newCapacity);
        /**
         * @brief Изменяет размер буфера до `newSize` (заполняя новые байты нулями) и обновляет курсоры.
         *
         * Предварительно сбрасывает недописанные биты записи и битовый кэш чтения; `_writeCursor`
         * устанавливается в `newSize`, а `_readCursor` при необходимости подтягивается к `newSize`.
         * @param [in] newSize Новый логический размер буфера в байтах.
         * @throws std::bad_alloc    При нехватке памяти.
         * @throws std::length_error Если размер превышает допустимый предел вектора.
         */
        void Resize(size_t newSize);
        /**
         * @brief Запрашивает освобождение неиспользуемой зарезервированной памяти внутреннего вектора.
         * @throws std::bad_alloc Может возникнуть при перевыделении памяти (зависит от реализации STL).
         */
        void ShrinkToFit();

        /**
         * @brief Выравнивает недописанные биты, отдаёт владение внутренним вектором байтов и очищает буфер.
         * @return Вектор байтов, ранее хранившийся в буфере.
         * @throws std::bad_alloc    При нехватке памяти для сброса незавершённого битового байта.
         * @throws std::length_error При переполнении размера буфера при сбросе битового байта.
         */
        std::vector<uint8_t> TakeData();

      
		///////////////////////////////////////////////////////////////////////
        ///            ЗАПИСЬ ДАННЫХ                                        ///
        ///////////////////////////////////////////////////////////////////////
        /**
         * @brief Записывает скалярное числовое значение с учётом текущего `ByteOrder`.
         * @tparam T Арифметический тип, удовлетворяющий концепту `BufferScalar`.
         * @param [in] value Записываемое значение.
         * @throws std::length_error При переполнении размера буфера.
         * @throws std::bad_alloc    При нехватке памяти для расширения буфера.
         */
        template <BufferScalar T>
        void Write(T value)
        {
            T encoded = ConvertEndian(value, _byteOrder);                      // Приводим значение к нужному порядку байтов.
            WriteBytes(reinterpret_cast<const uint8_t*>(&encoded), sizeof(T)); // Записываем байты в буфер.
        }
        /**
         * @brief Записывает логическое значение как 1 байт (`1` для `true`, `0` для `false`).
         * @param [in] value Логическое значение.
         * @throws std::length_error При переполнении размера буфера.
         * @throws std::bad_alloc    При нехватке памяти для расширения буфера.
         */
        void WriteBool(bool value);
        /**
         * @brief Записывает произвольный массив байтов в текущую позицию записи.
         *
         * При наличии незавершённых бит предварительно выравнивает запись на границу байта.
         * @param [in] src   Указатель на исходные байты (не может быть `nullptr` при `count > 0`).
         * @param [in] count Количество байтов для записи (`0` — допустимая пустая операция).
         * @throws std::invalid_argument Если `count > 0` и `src == nullptr`.
         * @throws std::length_error     При переполнении размера буфера.
         * @throws std::bad_alloc        При нехватке памяти для расширения буфера.
         */
        void WriteBytes(const uint8_t* src, size_t count);
        /**
         * @brief Дописывает всё содержимое другого `ByteBuffer` в текущий буфер (безопасен при самозаписи `buf.WriteBuffer(buf)`).
         * @param [in] other Исходный буфер-источник байтов.
         * @note Копирует зафиксированные байты `other.Data()[0..other.Size())`. Если в другом буфере `other`
         *       есть незавершённые биты, предварительно вызовите `other.AlignWriteToByte()`.
         * @throws std::length_error При переполнении размера буфера.
         * @throws std::bad_alloc    При нехватке памяти для расширения буфера.
         */
        void WriteBuffer(const ByteBuffer& other);
        /**
         * @brief Записывает сырые байты строки без завершающего нулевого байта.
         * @param [in] text Записываемая строка.
         * @throws std::length_error При переполнении размера буфера.
         * @throws std::bad_alloc    При нехватке памяти для расширения буфера.
         */
        void WriteString(std::string_view text);
        /**
         * @brief Записывает строку с завершающим нулевым байтом (`'\0'`).
         * @param [in] text Записываемая строка (встроенные символы `'\0'` записываются как есть).
         * @throws std::length_error При переполнении размера буфера.
         * @throws std::bad_alloc    При нехватке памяти для расширения буфера.
         */
        void WriteCString(std::string_view text);
        /**
         * @brief Записывает 32-битную длину строки (`uint32_t` в текущем `ByteOrder`), а затем её байты без `'\0'`.
         * @param [in] text Записываемая строка (длиной не более `UINT32_MAX` байтов).
         * @throws std::length_error Если длина строки превышает `UINT32_MAX` или происходит переполнение буфера.
         * @throws std::bad_alloc    При нехватке памяти для расширения буфера.
         */
        void WriteLengthPrefixedString(std::string_view text);
        /**
         * @brief Перезаписывает скалярное значение по заданному смещению без изменения `_writeCursor`.
         * @tparam T Арифметический тип, удовлетворяющий концепту `BufferScalar`.
         * @param [in] offset Смещение в байтах от начала буфера.
         * @param [in] value  Новое значение (конвертируется согласно текущему `ByteOrder`).
         * @throws std::out_of_range Если диапазон `[offset, offset + sizeof(T))` выходит за границы `Size()`.
         */
        template <BufferScalar T>
        void WriteAt(size_t offset, T value)
        {
            T encoded = ConvertEndian(value, _byteOrder);                                // Конвертируем порядок байтов.
            WriteBytesAt(offset, reinterpret_cast<const uint8_t*>(&encoded), sizeof(T)); // Записываем по смещению.
        }
        /**
         * @brief Перезаписывает блок байтов по заданному смещению без сдвига `_writeCursor`.
         * @param [in] offset Смещение в байтах от начала буфера.
         * @param [in] src    Указатель на источник данных (не может быть `nullptr` при `count > 0`).
         * @param [in] count  Количество байтов для перезаписи.
         * @throws std::invalid_argument Если `count > 0` и `src == nullptr`.
         * @throws std::out_of_range     Если диапазон `[offset, offset + count)` выходит за границы буфера.
         */
        void WriteBytesAt(size_t offset, const uint8_t* src, size_t count);


		///////////////////////////////////////////////////////////////////////
        ///            ЧТЕНИЕ ДАННЫХ                                        ///
        ///////////////////////////////////////////////////////////////////////
        /**
         * @brief Читает скалярное числовое значение из текущей позиции и сдвигает `_readCursor`.
         *
         * При наличии остатка в битовом кэше чтения предварительно выравнивает чтение на границу байта.
         * При ошибке выхода за границы состояние курсора и битового кэша не изменяется.
         * @tparam T Арифметический тип, удовлетворяющий концепту `BufferScalar`.
         * @return Прочитанное значение с учётом текущего `ByteOrder`.
         * @throws std::out_of_range Если в буфере недостаточно байтов.
         */
        template <BufferScalar T>
        T Read()
        {
            // Сначала считываем значение и проверяем границы (без модификации состояния при ошибке).
            T value = Peek<T>(_readCursor);
            AlignReadToByte();        // Сбрасываем остаток битового кэша только после успешного чтения.
            _readCursor += sizeof(T); // Сдвигаем курсор чтения на размер типа.
            return value;             // Возвращаем прочитанное значение.
        }
        /**
         * @brief Читает скалярное значение по заданному смещению без сдвига `_readCursor`.
         * @tparam T Арифметический тип, удовлетворяющий концепту `BufferScalar`.
         * @param [in] offset Смещение в байтах от начала буфера.
         * @return Значение по указанному смещению с учётом текущего `ByteOrder`.
         * @throws std::out_of_range Если диапазон `[offset, offset + sizeof(T))` выходит за границы буфера.
         */
        template <BufferScalar T>
        T Peek(size_t offset) const
        {
            // Проверяем границы безопасным способом без переполнения сложения `offset + sizeof(T)`.
            if (offset > _data.size() || sizeof(T) > _data.size() - offset)
                ThrowOutOfBounds(offset, sizeof(T));

            T raw{};                                      // Временная переменная для сырых байтов.
            std::memcpy(&raw, &_data[offset], sizeof(T)); // Копируем байты из хранилища.
            return ConvertEndian(raw, _byteOrder);        // Приводим к нативному порядку байтов.
        }
        /**
         * @brief Читает 1 байт и возвращает `true`, если он не равен `0`.
         * @return Прочитанное логическое значение.
         * @throws std::out_of_range Если в буфере нет доступных байтов.
         */
        bool ReadBool();
        /**
         * @brief Читает указанное количество байтов в предоставленный буфер `dst`.
         * @param [out] dst   Указатель на целевой буфер (не может быть `nullptr` при `count > 0`).
         * @param [in]  count Количество байтов для чтения (`0` — допустимая пустая операция).
         * @throws std::invalid_argument Если `count > 0` и `dst == nullptr`.
         * @throws std::out_of_range     Если в буфере недостаточно байтов.
         */
        void ReadBytes(uint8_t* dst, size_t count);
        /**
         * @brief Читает `count` байтов и возвращает их в новом `std::vector<uint8_t>`.
         *
         * Проверка границ выполняется до выделения вектора (защита от OOM при некорректном `count`).
         * @param [in] count Количество байтов для чтения.
         * @return Вектор из `count` прочитанных байтов.
         * @throws std::out_of_range Если в буфере недостаточно байтов.
         * @throws std::bad_alloc    При нехватке памяти для создания результирующего вектора.
         */
        std::vector<uint8_t> ReadBytes(size_t count);
        /**
         * @brief Читает строку фиксированной длины `length` байтов.
         * @param [in] length       Количество байтов для чтения.
         * @param [in] validateUtf8 Если `true`, проверяет валидность UTF-8 до сдвига курсора.
         * @return Прочитанная строка.
         * @throws std::out_of_range     Если в буфере недостаточно байтов.
         * @throws std::invalid_argument Если `validateUtf8 == true` и данные содержат невалидную последовательность UTF-8.
         * @throws std::bad_alloc        При нехватке памяти для создания строки.
         */
        std::string ReadString(size_t length, bool validateUtf8 = false);
        /**
         * @brief Читает нуль-терминированную C-строку до первого байта `'\0'`.
         * @param [in] validateUtf8 Если `true`, проверяет валидность UTF-8 до сдвига курсора.
         * @return Прочитанная строка (без завершающего `'\0'`).
         * @throws std::out_of_range     Если терминатор `'\0'` не найден до конца буфера.
         * @throws std::invalid_argument Если `validateUtf8 == true` и строка содержит невалидный UTF-8.
         * @throws std::bad_alloc        При нехватке памяти для создания строки.
         */
        std::string ReadCString(bool validateUtf8 = false);
        /**
         * @brief Читает 32-битную длину (`uint32_t` в текущем `ByteOrder`) и последующую строку этой длины.
         *
         * Обеспечивает строгую гарантию исключений: при усечённых данных или ошибке UTF-8 курсор чтения не сдвигается.
         * @param [in] validateUtf8 Если `true`, проверяет валидность UTF-8.
         * @return Прочитанная строка.
         * @throws std::out_of_range     Если не хватает байтов для заголовка длины или тела строки.
         * @throws std::invalid_argument Если `validateUtf8 == true` и строка содержит невалидный UTF-8.
         * @throws std::bad_alloc        При нехватке памяти для создания строки.
         */
        std::string ReadLengthPrefixedString(bool validateUtf8 = false);
        /**
         * @brief Пропускает указанное количество байтов, сдвигая курсор чтения вперёд.
         * @param [in] bytes Количество пропускаемых байтов.
         * @throws std::out_of_range Если пропуск выходит за границы буфера.
         */
        void Skip(size_t bytes);


		///////////////////////////////////////////////////////////////////////
        ///          ПОБИТОВАЯ УПАКОВКА / РАСПАКОВКА                        ///
        ///////////////////////////////////////////////////////////////////////
        /**
         * @brief Записывает один бит (`true = 1`, `false = 0`) в битовый аккумулятор.
         *
         * При накоплении 8 бит автоматически сбрасывает готовый байт в буфер.
         * @param [in] bit Записываемое значение бита.
         * @throws std::length_error При переполнении размера буфера во время сброса полного байта.
         * @throws std::bad_alloc    При нехватке памяти во время сброса полного байта.
         */
        void WriteBit(bool bit);
        /**
         * @brief Записывает младшие `bitCount` бит из `value` (от старшего бита к младшему).
         * @param [in] value    Исходное беззнаковое значение.
         * @param [in] bitCount Количество записываемых бит (`1..64`).
         * @throws std::out_of_range Если `bitCount == 0` или `bitCount > 64`.
         * @throws std::length_error При переполнении размера буфера.
         * @throws std::bad_alloc    При нехватке памяти для расширения буфера.
         */
        void WriteBits(uint64_t value, uint8_t bitCount);
        /**
         * @brief Сбрасывает недописанный байт битового аккумулятора в буфер, дополняя младшие биты нулями.
         * @throws std::length_error При переполнении размера буфера.
         * @throws std::bad_alloc    При нехватке памяти для расширения буфера.
         */
        void AlignWriteToByte();
        /**
         * @brief Читает один бит из потока (при опустошении кэша подгружает следующий байт из буфера).
         * @return Значение очередного бита (`true` для `1`, `false` для `0`).
         * @throws std::out_of_range Если в буфере не осталось доступных бит.
         */
        bool ReadBit();
        /**
         * @brief Читает `bitCount` бит и возвращает их как `uint64_t` (в младших битах результата).
         *
         * Достаточность данных проверяется заранее до модификации курсора и битового кэша.
         * @param [in] bitCount Количество читаемых бит (`1..64`).
         * @return Прочитанное беззнаковое значение.
         * @throws std::out_of_range Если `bitCount == 0`, `bitCount > 64` или в буфере недостаточно бит.
         */
        uint64_t ReadBits(uint8_t bitCount);
        /**
         * @brief Сбрасывает остаток битового кэша чтения, выравнивая дальнейшее чтение по границе следующего байта.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        void AlignReadToByte() noexcept;


        //////////////////////////////////////////////////////////////////////
        ///              ПОТОКОВЫЕ ОПЕРАТОРЫ ЗАПИСИ                        ///
        //////////////////////////////////////////////////////////////////////
        /** @brief Запрещает неявную запись произвольных указателей через приведение к `bool`. */
        ByteBuffer& operator<<(const void*) = delete;
        /**
         * @brief Записывает `bool` (1 байт: `1` или `0`).
         * @param [in] value Записываемое значение.
         * @return Ссылка на текущий буфер (`*this`).
         * @throws std::length_error При переполнении размера буфера.
         * @throws std::bad_alloc    При нехватке памяти.
         */
        ByteBuffer& operator<<(bool value);
        /**
         * @brief Записывает `char` (1 байт).
         * @param [in] value Записываемый символ.
         * @return Ссылка на текущий буфер (`*this`).
         * @throws std::length_error При переполнении размера буфера.
         * @throws std::bad_alloc    При нехватке памяти.
         */
        ByteBuffer& operator<<(char value);
        /**
         * @brief Записывает беззнаковое 8-битное целое (`uint8_t`).
         * @param [in] value Записываемое число.
         * @return Ссылка на текущий буфер (`*this`).
         * @throws std::length_error При переполнении размера буфера.
         * @throws std::bad_alloc    При нехватке памяти.
         */
        ByteBuffer& operator<<(uint8_t value);
        /**
         * @brief Записывает беззнаковое 16-битное целое (`uint16_t`) с учётом `ByteOrder`.
         * @param [in] value Записываемое число.
         * @return Ссылка на текущий буфер (`*this`).
         * @throws std::length_error При переполнении размера буфера.
         * @throws std::bad_alloc    При нехватке памяти.
         */
        ByteBuffer& operator<<(uint16_t value);
        /**
         * @brief Записывает беззнаковое 32-битное целое (`uint32_t`) с учётом `ByteOrder`.
         * @param [in] value Записываемое число.
         * @return Ссылка на текущий буфер (`*this`).
         * @throws std::length_error При переполнении размера буфера.
         * @throws std::bad_alloc    При нехватке памяти.
         */
        ByteBuffer& operator<<(uint32_t value);
        /**
         * @brief Записывает беззнаковое 64-битное целое (`uint64_t`) с учётом `ByteOrder`.
         * @param [in] value Записываемое число.
         * @return Ссылка на текущий буфер (`*this`).
         * @throws std::length_error При переполнении размера буфера.
         * @throws std::bad_alloc    При нехватке памяти.
         */
        ByteBuffer& operator<<(uint64_t value);
        /**
         * @brief Записывает знаковое 8-битное целое (`int8_t`).
         * @param [in] value Записываемое число.
         * @return Ссылка на текущий буфер (`*this`).
         * @throws std::length_error При переполнении размера буфера.
         * @throws std::bad_alloc    При нехватке памяти.
         */
        ByteBuffer& operator<<(int8_t value);
        /**
         * @brief Записывает знаковое 16-битное целое (`int16_t`) с учётом `ByteOrder`.
         * @param [in] value Записываемое число.
         * @return Ссылка на текущий буфер (`*this`).
         * @throws std::length_error При переполнении размера буфера.
         * @throws std::bad_alloc    При нехватке памяти.
         */
        ByteBuffer& operator<<(int16_t value);
        /**
         * @brief Записывает знаковое 32-битное целое (`int32_t`) с учётом `ByteOrder`.
         * @param [in] value Записываемое число.
         * @return Ссылка на текущий буфер (`*this`).
         * @throws std::length_error При переполнении размера буфера.
         * @throws std::bad_alloc    При нехватке памяти.
         */
        ByteBuffer& operator<<(int32_t value);
        /**
         * @brief Записывает знаковое 64-битное целое (`int64_t`) с учётом `ByteOrder`.
         * @param [in] value Записываемое число.
         * @return Ссылка на текущий буфер (`*this`).
         * @throws std::length_error При переполнении размера буфера.
         * @throws std::bad_alloc    При нехватке памяти.
         */
        ByteBuffer& operator<<(int64_t value);
        /**
         * @brief Записывает 32-битное число с плавающей точкой (`float`) с учётом `ByteOrder`.
         * @param [in] value Записываемое число.
         * @return Ссылка на текущий буфер (`*this`).
         * @throws std::length_error При переполнении размера буфера.
         * @throws std::bad_alloc    При нехватке памяти.
         */
        ByteBuffer& operator<<(float value);
        /**
         * @brief Записывает 64-битное число с плавающей точкой (`double`) с учётом `ByteOrder`.
         * @param [in] value Записываемое число.
         * @return Ссылка на текущий буфер (`*this`).
         * @throws std::length_error При переполнении размера буфера.
         * @throws std::bad_alloc    При нехватке памяти.
         */
        ByteBuffer& operator<<(double value);
        /**
         * @brief Записывает строку `std::string_view` как нуль-терминированную C-строку (`'\0'` на конце).
         * @param [in] value Записываемая строка.
         * @return Ссылка на текущий буфер (`*this`).
         * @throws std::length_error При переполнении размера буфера.
         * @throws std::bad_alloc    При нехватке памяти.
         */
        ByteBuffer& operator<<(std::string_view value);
        /**
         * @brief Записывает строку `std::string` как нуль-терминированную C-строку (`'\0'` на конце).
         * @param [in] str Записываемая строка.
         * @return Ссылка на текущий буфер (`*this`).
         * @throws std::length_error При переполнении размера буфера.
         * @throws std::bad_alloc    При нехватке памяти.
         */
        ByteBuffer& operator<<(std::string const& str);
        /**
         * @brief Записывает C-строку `const char*` с завершающим `'\0'` (`nullptr` трактуется как пустая строка `""`).
         * @param [in] str Указатель на нуль-терминированную строку или `nullptr`.
         * @return Ссылка на текущий буфер (`*this`).
         * @throws std::length_error При переполнении размера буфера.
         * @throws std::bad_alloc    При нехватке памяти.
         */
        ByteBuffer& operator<<(char const* str);


        //////////////////////////////////////////////////////////////////////
        ///              ПОТОКОВЫЕ ОПЕРАТОРЫ ЧТЕНИЯ                        ///
        //////////////////////////////////////////////////////////////////////
        /**
         * @brief Читает `bool` (1 байт: `0` -> `false`, не `0` -> `true`).
         * @param [out] value Целевая переменная.
         * @return Ссылка на текущий буфер (`*this`).
         * @throws std::out_of_range Если в буфере недостаточно байтов.
         */
        ByteBuffer& operator>>(bool& value);
        /**
         * @brief Читает `char` (1 байт).
         * @param [out] value Целевая переменная.
         * @return Ссылка на текущий буфер (`*this`).
         * @throws std::out_of_range Если в буфере недостаточно байтов.
         */
        ByteBuffer& operator>>(char& value);
        /**
         * @brief Читает беззнаковое 8-битное целое (`uint8_t`).
         * @param [out] value Целевая переменная.
         * @return Ссылка на текущий буфер (`*this`).
         * @throws std::out_of_range Если в буфере недостаточно байтов.
         */
        ByteBuffer& operator>>(uint8_t& value);
        /**
         * @brief Читает беззнаковое 16-битное целое (`uint16_t`) с учётом `ByteOrder`.
         * @param [out] value Целевая переменная.
         * @return Ссылка на текущий буфер (`*this`).
         * @throws std::out_of_range Если в буфере недостаточно байтов.
         */
        ByteBuffer& operator>>(uint16_t& value);
        /**
         * @brief Читает беззнаковое 32-битное целое (`uint32_t`) с учётом `ByteOrder`.
         * @param [out] value Целевая переменная.
         * @return Ссылка на текущий буфер (`*this`).
         * @throws std::out_of_range Если в буфере недостаточно байтов.
         */
        ByteBuffer& operator>>(uint32_t& value);
        /**
         * @brief Читает беззнаковое 64-битное целое (`uint64_t`) с учётом `ByteOrder`.
         * @param [out] value Целевая переменная.
         * @return Ссылка на текущий буфер (`*this`).
         * @throws std::out_of_range Если в буфере недостаточно байтов.
         */
        ByteBuffer& operator>>(uint64_t& value);
        /**
         * @brief Читает знаковое 8-битное целое (`int8_t`).
         * @param [out] value Целевая переменная.
         * @return Ссылка на текущий буфер (`*this`).
         * @throws std::out_of_range Если в буфере недостаточно байтов.
         */
        ByteBuffer& operator>>(int8_t& value);
        /**
         * @brief Читает знаковое 16-битное целое (`int16_t`) с учётом `ByteOrder`.
         * @param [out] value Целевая переменная.
         * @return Ссылка на текущий буфер (`*this`).
         * @throws std::out_of_range Если в буфере недостаточно байтов.
         */
        ByteBuffer& operator>>(int16_t& value);
        /**
         * @brief Читает знаковое 32-битное целое (`int32_t`) с учётом `ByteOrder`.
         * @param [out] value Целевая переменная.
         * @return Ссылка на текущий буфер (`*this`).
         * @throws std::out_of_range Если в буфере недостаточно байтов.
         */
        ByteBuffer& operator>>(int32_t& value);
        /**
         * @brief Читает знаковое 64-битное целое (`int64_t`) с учётом `ByteOrder`.
         * @param [out] value Целевая переменная.
         * @return Ссылка на текущий буфер (`*this`).
         * @throws std::out_of_range Если в буфере недостаточно байтов.
         */
        ByteBuffer& operator>>(int64_t& value);
        /**
         * @brief Читает 32-битное число с плавающей точкой (`float`) с учётом `ByteOrder`.
         * @param [out] value Целевая переменная.
         * @return Ссылка на текущий буфер (`*this`).
         * @throws std::out_of_range Если в буфере недостаточно байтов.
         */
        ByteBuffer& operator>>(float& value);
        /**
         * @brief Читает 64-битное число с плавающей точкой (`double`) с учётом `ByteOrder`.
         * @param [out] value Целевая переменная.
         * @return Ссылка на текущий буфер (`*this`).
         * @throws std::out_of_range Если в буфере недостаточно байтов.
         */
        ByteBuffer& operator>>(double& value);
        /**
         * @brief Читает нуль-терминированную C-строку в `std::string` (без проверки UTF-8).
         * @param [out] value Целевая строка.
         * @return Ссылка на текущий буфер (`*this`).
         * @throws std::out_of_range Если терминатор `'\0'` не найден до конца буфера.
         * @throws std::bad_alloc    При нехватке памяти для строки.
         */
        ByteBuffer& operator>>(std::string& value);
        
		//////////////////////////////////////////////////////////////////////
        ///               ИНДЕКСАЦИЯ И ДИАГНОСТИКА                         ///
        //////////////////////////////////////////////////////////////////////
        /**
         * @brief Возвращает изменяемую ссылку на байт по индексу с проверкой границ.
         * @param [in] index Индекс байта (`0..Size() - 1`).
         * @return Изменяемая ссылка на байт буфера.
         * @throws std::out_of_range Если `index >= Size()`.
         */
        uint8_t& operator[](size_t index);
        /**
         * @brief Возвращает константную ссылку на байт по индексу с проверкой границ.
         * @param [in] index Индекс байта (`0..Size() - 1`).
         * @return Константная ссылка на байт буфера.
         * @throws std::out_of_range Если `index >= Size()`.
         */
        const uint8_t& operator[](size_t index) const;
        /**
         * @brief Форматирует содержимое буфера в читаемую шестнадцатеричную строку (по 16 байт в строке, с отступом каждые 8 байт).
         * @return Строка с HEX-дампом в верхнем регистре или пустая строка для пустого буфера.
         * @throws std::bad_alloc При нехватке памяти для формирования результирующей строки.
         */
        std::string ToHexString() const;
        /**
         * @brief Форматирует содержимое буфера в строку десятичных значений байтов (`0..255`) через пробел.
         * @return Строка с десятичным представлением или пустая строка для пустого буфера.
         * @throws std::bad_alloc При нехватке памяти для формирования результирующей строки.
         */
        std::string ToDecimalString() const;
        /**
         * @brief Форматирует содержимое буфера как ASCII-строку (непечатаемые байты заменяются на `'.'`).
         * @return ASCII-представление содержимого буфера.
         * @throws std::bad_alloc При нехватке памяти для формирования результирующей строки.
         */
        std::string ToAsciiString() const;
    };
}

#endif // LIBOTHER_NET_BYTE_BUFFER_H
