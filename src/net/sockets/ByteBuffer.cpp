#include "ByteBuffer.h"
#include "Utils.h"

namespace other::net::sockets
{
    // Конструктор по умолчанию / с заданной начальной емкостью и порядком байт.
    ByteBuffer::ByteBuffer(size_t initialCapacity, ByteOrder order)
        : _readCursor(0)     // Устанавливаем курсор чтения на начало буфера.
        , _writeCursor(0)    // Устанавливаем курсор записи на начало буфера.
        , _byteOrder(order)  // Сохраняем выбранный порядок байт.
        , _writeBitAcc(0)    // Обнуляем аккумулятор битов записи.
        , _writeBitCount(0)  // Недописанных бит пока нет.
        , _readBitCache(0)   // Обнуляем кэш битов чтения.
        , _readBitLeft(0)    // Непрочитанных бит в кэше нет.
    {
        // Если запрошена ненулевая емкость, резервируем память заранее для снижения числа аллокаций.
        if (initialCapacity > 0)
            _data.reserve(initialCapacity);
    }

    // Конструктор, принимающий готовый вектор байтов во владение через move-семантику.
    ByteBuffer::ByteBuffer(std::vector<uint8_t> initialData, ByteOrder order) noexcept
        : _data(std::move(initialData)) // Перемещаем владение буфером без копирования байтов.
        , _readCursor(0)                // Чтение начинается с первого байта.
        , _writeCursor(_data.size())    // Запись продолжится с конца переданных данных.
        , _byteOrder(order)             // Сохраняем выбранный порядок байт.
        , _writeBitAcc(0)               // Аккумулятор битов записи пуст.
        , _writeBitCount(0)             // Счетчик битов записи равен 0.
        , _readBitCache(0)              // Кэш битов чтения пуст.
        , _readBitLeft(0)               // Счетчик оставшихся бит чтения равен 0.
    {
    }

    // Конструктор, копирующий сырой массив байтов заданной длины.
    ByteBuffer::ByteBuffer(const uint8_t* data, size_t size, ByteOrder order)
        : ByteBuffer(size, order) // Делегируем базовую инициализацию и резервирование памяти.
    {
        // Если передан непустой блок, копируем его в буфер через WriteBytes (с проверкой nullptr).
        if (size > 0)
            WriteBytes(data, size);
    }

    // Конструктор перемещения: забирает память и все состояния у временного объекта.
    ByteBuffer::ByteBuffer(ByteBuffer&& other) noexcept
        : _data(std::move(other._data))       // Переносим вектор байтов за O(1).
        , _readCursor(other._readCursor)      // Копируем позицию курсора чтения.
        , _writeCursor(other._writeCursor)    // Копируем позицию курсора записи.
        , _byteOrder(other._byteOrder)        // Копируем настройку порядка байт.
        , _writeBitAcc(other._writeBitAcc)    // Копируем аккумулятор битовой записи.
        , _writeBitCount(other._writeBitCount)// Копируем число накопленных бит записи.
        , _readBitCache(other._readBitCache)  // Копируем кэш битового чтения.
        , _readBitLeft(other._readBitLeft)    // Копируем остаток бит в кэше чтения.
    {
        other.Clear(); // Оставляем исходный объект в чистом, валидном пустом состоянии.
    }

    // Оператор перемещающего присваивания.
    ByteBuffer& ByteBuffer::operator=(ByteBuffer&& other) noexcept
    {
        // Защита от самоприсваивания (buf = std::move(buf)).
        if (this != &other)
        {
            _data          = std::move(other._data); // Переносим хранилище байтов.
            _readCursor    = other._readCursor;      // Переносим курсор чтения.
            _writeCursor   = other._writeCursor;     // Переносим курсор записи.
            _byteOrder     = other._byteOrder;       // Переносим порядок байт.
            _writeBitAcc   = other._writeBitAcc;     // Переносим аккумулятор записи.
            _writeBitCount = other._writeBitCount;   // Переносим счетчик бит записи.
            _readBitCache  = other._readBitCache;    // Переносим кэш чтения.
            _readBitLeft   = other._readBitLeft;     // Переносим остаток бит чтения.
            other.Clear();                           // Сбрасываем исходный объект.
        }
        return *this; // Возвращаем ссылку на текущий объект.
    }

    // Формирует подробное сообщение и выбрасывает исключение при выходе за границы буфера.
    void ByteBuffer::ThrowOutOfBounds(size_t offset, size_t requested) const
    {
        std::ostringstream oss; // Строковый поток для сборки диагностического сообщения.
        oss << "ByteBuffer: out of bounds (offset=" << offset
            << ", requested=" << requested << ", size=" << _data.size() << ").";
        throw std::out_of_range(oss.str()); // Выбрасываем стандартное исключение.
    }

    // Устанавливает позицию курсора чтения.
    void ByteBuffer::SetReadOffset(size_t offset)
    {
        if (offset > _data.size())      // Запрещаем установку курсора за пределы существующих данных.
            ThrowOutOfBounds(offset, 0);
        AlignReadToByte();              // Сбрасываем кэш побитового чтения.
        _readCursor = offset;           // Обновляем позицию чтения.
    }

    // Устанавливает позицию курсора записи.
    void ByteBuffer::SetWriteOffset(size_t offset)
    {
        if (offset > _data.size())      // Запрещаем установку курсора записи за пределы буфера.
            ThrowOutOfBounds(offset, 0);
        AlignWriteToByte();             // Сбрасываем недописанные биты перед перемещением курсора.
        _writeCursor = offset;          // Обновляем позицию записи.
    }

    // Полностью очищает буфер и сбрасывает все внутренние счетчики.
    void ByteBuffer::Clear() noexcept
    {
        _data.clear();      // Очищаем содержимое вектора (без обязательного освобождения capacity).
        _readCursor    = 0; // Сбрасываем курсор чтения.
        _writeCursor   = 0; // Сбрасываем курсор записи.
        _writeBitAcc   = 0; // Очищаем битовый аккумулятор записи.
        _writeBitCount = 0; // Обнуляем счетчик бит записи.
        _readBitCache  = 0; // Очищаем битовый кэш чтения.
        _readBitLeft   = 0; // Обнуляем остаток бит чтения.
    }

    // Перематывает чтение в начало буфера, сохраняя записанные данные.
    void ByteBuffer::Rewind() noexcept
    {
        _readCursor   = 0; // Возвращаем курсор чтения на 0.
        _readBitCache = 0; // Сбрасываем битовый кэш чтения.
        _readBitLeft  = 0; // Обнуляем счетчик оставшихся бит.
    }

    // Резервирует память под указанное количество байтов.
    void ByteBuffer::Reserve(size_t newCapacity)
    {
        if (newCapacity > _data.capacity()) // Резервируем только если требуемая емкость больше текущей.
            _data.reserve(newCapacity);
    }

    // Изменяет логический размер буфера.
    void ByteBuffer::Resize(size_t newSize)
    {
        AlignWriteToByte();            // Выравниваем незавершенные биты записи.
        AlignReadToByte();             // Сбрасываем кэш побитового чтения.
        _data.resize(newSize, 0);      // Изменяем размер вектора, новые байты заполняются нулями.
        if (_readCursor > newSize)     // Если курсор чтения оказался за новым концом, подтягиваем его.
            _readCursor = newSize;
        _writeCursor = newSize;        // Курсор записи устанавливаем в конец буфера.
    }

    // Запрашивает освобождение лишней зарезервированной памяти.
    void ByteBuffer::ShrinkToFit()
    {
        _data.shrink_to_fit(); // Просим вектор сократить capacity до size().
    }

    // Извлекает внутренний вектор байтов наружу и очищает текущий объект.
    std::vector<uint8_t> ByteBuffer::TakeData() noexcept
    {
        AlignWriteToByte();                    // Дописываем остаток битового аккумулятора, если он есть.
        std::vector<uint8_t> out = std::move(_data); // Забираем вектор без копирования.
        Clear();                               // Сбрасываем курсоры и состояние текущего объекта.
        return out;                            // Возвращаем владение вызывающему коду.
    }

    // ==========================================
    // ЗАПИСЬ ДАННЫХ
    // ==========================================

    // Записывает логическое значение как 1 байт (1 или 0).
    void ByteBuffer::WriteBool(bool value)
    {
        Write<uint8_t>(value ? 1U : 0U); // Приводим bool к строгому байтовому представлению 0/1.
    }

    // Записывает массив байтов в текущую позицию записи с автоматическим расширением буфера.
    void ByteBuffer::WriteBytes(const uint8_t* src, size_t count)
    {
        if (count == 0) // Запись нуля байт — допустимая пустая операция.
            return;
        if (!src)       // Запрещаем передавать nullptr при ненулевой длине.
            throw std::invalid_argument("ByteBuffer::WriteBytes: source pointer is null.");

        AlignWriteToByte(); // Перед байтовой записью завершаем любой незакрытый битовый пакет.

        // Проверяем, не приведет ли сложение _writeCursor + count к переполнению size_t.
        if (count > std::numeric_limits<size_t>::max() - _writeCursor)
            throw std::length_error("ByteBuffer::WriteBytes: size overflow.");

        const size_t requiredSize = _writeCursor + count; // Вычисляем итоговую позицию после записи.
        if (requiredSize > _data.size())                  // Если буфер короче требуемого размера — расширяем его.
            _data.resize(requiredSize);

        std::memcpy(&_data[_writeCursor], src, count);    // Копируем данные в целевую область памяти.
        _writeCursor = requiredSize;                      // Сдвигаем курсор записи вперед.
    }

    // Дописывает содержимое другого ByteBuffer в текущий буфер.
    void ByteBuffer::WriteBuffer(const ByteBuffer& other)
    {
        if (!other.Empty())                        // Копируем только если исходный буфер не пуст.
            WriteBytes(other.Data(), other.Size());
    }

    // Записывает сырые байты строки (без '\0').
    void ByteBuffer::WriteString(std::string_view text)
    {
        if (!text.empty()) // Пропускаем пустую строку.
            WriteBytes(reinterpret_cast<const uint8_t*>(text.data()), text.size());
    }

    // Записывает строку с завершающим нулевым байтом ('\0').
    void ByteBuffer::WriteCString(std::string_view text)
    {
        WriteString(text);       // Сначала записываем символы строки.
        Write<uint8_t>(0);       // Затем добавляем нуль-терминатор.
    }

    // Записывает длину строки (uint32_t) и сами байты строки.
    void ByteBuffer::WriteLengthPrefixedString(std::string_view text)
    {
        // Проверяем, что длина строки помещается в 32-битный префикс.
        if (text.size() > std::numeric_limits<uint32_t>::max())
            throw std::length_error("ByteBuffer::WriteLengthPrefixedString: string exceeds uint32_t max length.");

        Write<uint32_t>(static_cast<uint32_t>(text.size())); // Записываем длину с учетом ByteOrder.
        WriteString(text);                                   // Записываем содержимое строки.
    }

    // Перезаписывает блок байтов по заданному смещению без изменения _writeCursor.
    void ByteBuffer::WriteBytesAt(size_t offset, const uint8_t* src, size_t count)
    {
        if (count == 0) // Нулевая запись ничего не делает.
            return;
        if (!src)       // Проверяем валидность указателя источника.
            throw std::invalid_argument("ByteBuffer::WriteBytesAt: source pointer is null.");
        // Проверяем, что диапазон [offset, offset + count) полностью лежит внутри буфера.
        if (offset > _data.size() || count > _data.size() - offset)
            ThrowOutOfBounds(offset, count);

        std::memcpy(&_data[offset], src, count); // Копируем данные по указанному смещению.
    }

    // ==========================================
    // ЧТЕНИЕ ДАННЫХ
    // ==========================================

    // Читает 1 байт и преобразует его в bool.
    bool ByteBuffer::ReadBool()
    {
        return Read<uint8_t>() != 0; // Любое ненулевое значение безопасно трактуется как true.
    }

    // Читает count байтов из буфера в массив dst.
    void ByteBuffer::ReadBytes(uint8_t* dst, size_t count)
    {
        if (count == 0) // Чтение нуля байтов ничего не делает.
            return;
        if (!dst)       // Проверяем валидность целевого указателя.
            throw std::invalid_argument("ByteBuffer::ReadBytes: destination pointer is null.");

        AlignReadToByte(); // Сбрасываем остаток битового кэша перед байтовым чтением.

        // Проверяем, достаточно ли непрочитанных байтов в буфере.
        if (_readCursor > _data.size() || count > _data.size() - _readCursor)
            ThrowOutOfBounds(_readCursor, count);

        std::memcpy(dst, &_data[_readCursor], count); // Копируем байты в выходной буфер.
        _readCursor += count;                         // Сдвигаем курсор чтения вперед.
    }

    // Читает count байтов и возвращает их в новом векторе.
    std::vector<uint8_t> ByteBuffer::ReadBytes(size_t count)
    {
        std::vector<uint8_t> out(count); // Выделяем вектор нужного размера.
        if (count > 0)                   // Заполняем его через основной метод ReadBytes.
            ReadBytes(out.data(), count);
        return out;                      // Возвращаем результат.
    }

    // Читает строку фиксированной длины (в байтах).
    std::string ByteBuffer::ReadString(size_t length, bool validateUtf8)
    {
        AlignReadToByte(); // Выравниваем чтение на границу байта.

        // Проверяем наличие достаточного количества байтов в буфере.
        if (_readCursor > _data.size() || length > _data.size() - _readCursor)
            ThrowOutOfBounds(_readCursor, length);

        if (length == 0) // Для нулевой длины сразу возвращаем пустую строку.
            return {};

        // Создаем временное представление для проверки UTF-8 без лишних копирований.
        std::string_view view(reinterpret_cast<const char*>(&_data[_readCursor]), length);
        if (validateUtf8 && !utils::utf8::IsValidUtf8(view))
            throw std::invalid_argument("ByteBuffer::ReadString: invalid UTF-8 sequence.");

        _readCursor += length;   // Сдвигаем курсор чтения на длину строки.
        return std::string(view);// Возвращаем копию строки.
    }

    // Читает нуль-терминированную C-строку до первого байта '\0'.
    std::string ByteBuffer::ReadCString(bool validateUtf8)
    {
        AlignReadToByte(); // Сбрасываем битовый кэш чтения.

        // Если курсор уже в конце буфера, читать нечего.
        if (_readCursor >= _data.size())
            ThrowOutOfBounds(_readCursor, 1);

        const char* begin = reinterpret_cast<const char*>(_data.data()) + _readCursor; // Начало поиска.
        const char* end   = reinterpret_cast<const char*>(_data.data()) + _data.size();// Конец буфера.
        const char* nul   = std::find(begin, end, '\0');                               // Ищем нулевой терминатор.
        if (nul == end)                                                                // Если '\0' не найден — ошибка границ.
            ThrowOutOfBounds(_readCursor, _data.size() - _readCursor + 1);

        std::string_view view(begin, static_cast<size_t>(nul - begin)); // Формируем срез без '\0'.
        if (validateUtf8 && !utils::utf8::IsValidUtf8(view))                  // При необходимости проверяем UTF-8.
            throw std::invalid_argument("ByteBuffer::ReadCString: invalid UTF-8 sequence.");

        _readCursor += view.size() + 1; // Пропускаем саму строку и завершающий байт '\0'.
        return std::string(view);       // Возвращаем прочитанную строку.
    }

    // Читает строку с 32-битным префиксом длины.
    std::string ByteBuffer::ReadLengthPrefixedString(bool validateUtf8)
    {
        const uint32_t len = Read<uint32_t>(); // Сначала считываем длину строки.
        return ReadString(len, validateUtf8);  // Затем читаем ровно len байтов.
    }

    // Пропускает указанное число байтов при чтении.
    void ByteBuffer::Skip(size_t bytes)
    {
        AlignReadToByte(); // Выравниваем чтение на границу байта.
        // Проверяем, что пропуск не выведет курсор за пределы буфера.
        if (_readCursor > _data.size() || bytes > _data.size() - _readCursor)
            ThrowOutOfBounds(_readCursor, bytes);
        _readCursor += bytes; // Сдвигаем курсор чтения.
    }

    // ==========================================
    // ПОБИТОВАЯ УПАКОВКА / РАСПАКОВКА
    // ==========================================

    // Записывает один бит в аккумулятор; при заполнении 8 бит сбрасывает байт в буфер.
    void ByteBuffer::WriteBit(bool bit)
    {
        // Сдвигаем накопленные биты влево и записываем новый бит в младший разряд.
        _writeBitAcc = static_cast<uint8_t>((_writeBitAcc << 1) | (bit ? 1U : 0U));
        ++_writeBitCount; // Увеличиваем счетчик накопленных бит.

        // Как только собран полный байт (8 бит), записываем его в _data.
        if (_writeBitCount == 8)
        {
            const uint8_t fullByte = _writeBitAcc; // Сохраняем готовый байт.
            _writeBitAcc   = 0;                    // Обнуляем аккумулятор до вызова WriteBytes.
            _writeBitCount = 0;                    // Обнуляем счетчик, чтобы избежать рекурсивного AlignWriteToByte.
            WriteBytes(&fullByte, 1);              // Записываем собранный байт в буфер.
        }
    }

    // Записывает младшие bitCount бит из value (от старшего бита к младшему).
    void ByteBuffer::WriteBits(uint64_t value, uint8_t bitCount)
    {
        // Проверяем допустимый диапазон числа бит (1..64).
        if (bitCount == 0 || bitCount > 64)
            throw std::out_of_range("ByteBuffer::WriteBits: bitCount must be in range [1, 64].");

        // Извлекаем и записываем биты по одному, начиная со старшего (bitCount - 1) до 0.
        for (uint8_t i = bitCount; i > 0; --i)
        {
            WriteBit(((value >> (i - 1)) & 1ULL) != 0);
        }
    }

    // Сбрасывает неполный байт битового аккумулятора в буфер, дополняя нулями справа.
    void ByteBuffer::AlignWriteToByte()
    {
        if (_writeBitCount == 0) // Если незаписанных бит нет — выходить сразу.
            return;

        // Сдвигаем накопленные биты в старшие разряды байта (младшие заполняются нулями).
        const uint8_t padded = static_cast<uint8_t>(_writeBitAcc << (8 - _writeBitCount));
        _writeBitAcc   = 0;      // Сбрасываем аккумулятор.
        _writeBitCount = 0;      // Сбрасываем счетчик до вызова WriteBytes.
        WriteBytes(&padded, 1);  // Записываем выровненный байт в буфер.
    }

    // Читает один бит из кэша чтения (при опустошении кэша подгружает следующий байт).
    bool ByteBuffer::ReadBit()
    {
        // Если в кэше не осталось непрочитанных бит, берем следующий байт из _data.
        if (_readBitLeft == 0)
        {
            if (_readCursor >= _data.size()) // Проверяем наличие хотя бы 1 байта.
                ThrowOutOfBounds(_readCursor, 1);

            _readBitCache = _data[_readCursor++]; // Загружаем байт в кэш и сдвигаем курсор чтения.
            _readBitLeft  = 8;                    // Теперь в кэше 8 доступных бит.
        }

        --_readBitLeft;                                     // Уменьшаем число оставшихся бит.
        return ((_readBitCache >> _readBitLeft) & 1U) != 0; // Извлекаем очередной бит (от старшего к младшему).
    }

    // Читает bitCount бит и собирает их в 64-битное беззнаковое число.
    uint64_t ByteBuffer::ReadBits(uint8_t bitCount)
    {
        // Проверяем допустимый диапазон числа бит (1..64).
        if (bitCount == 0 || bitCount > 64)
            throw std::out_of_range("ByteBuffer::ReadBits: bitCount must be in range [1, 64].");

        uint64_t result = 0; // Аккумулятор итогового значения.
        // Последовательно читаем биты и сдвигаем их в результат.
        for (uint8_t i = 0; i < bitCount; ++i)
        {
            result = (result << 1) | (ReadBit() ? 1ULL : 0ULL);
        }
        return result; // Возвращаем собранное число.
    }

    // Сбрасывает остаток битового кэша чтения, выравнивая дальнейшее чтение по байту.
    void ByteBuffer::AlignReadToByte() noexcept
    {
        _readBitCache = 0; // Очищаем кэшированный байт.
        _readBitLeft  = 0; // Обнуляем счетчик оставшихся бит.
    }

    //////////////////////////////////////////////////////////////////////
    ///              ПОТОКОВЫЕ ОПЕРАТОРЫ ЗАПИСИ                        ///
    //////////////////////////////////////////////////////////////////////

    // Записывает bool (1 байт) и возвращает *this.
    ByteBuffer& ByteBuffer::operator<<(bool value)             { WriteBool(value); return *this; }
    // Записывает char (1 байт) и возвращает *this.
    ByteBuffer& ByteBuffer::operator<<(char value)             { Write<char>(value); return *this; }
    // Записывает uint8_t и возвращает *this.
    ByteBuffer& ByteBuffer::operator<<(uint8_t value)          { Write<uint8_t>(value); return *this; }
    // Записывает uint16_t с учетом ByteOrder и возвращает *this.
    ByteBuffer& ByteBuffer::operator<<(uint16_t value)         { Write<uint16_t>(value); return *this; }
    // Записывает uint32_t с учетом ByteOrder и возвращает *this.
    ByteBuffer& ByteBuffer::operator<<(uint32_t value)         { Write<uint32_t>(value); return *this; }
    // Записывает uint64_t с учетом ByteOrder и возвращает *this.
    ByteBuffer& ByteBuffer::operator<<(uint64_t value)         { Write<uint64_t>(value); return *this; }
    // Записывает int8_t и возвращает *this.
    ByteBuffer& ByteBuffer::operator<<(int8_t value)           { Write<int8_t>(value); return *this; }
    // Записывает int16_t с учетом ByteOrder и возвращает *this.
    ByteBuffer& ByteBuffer::operator<<(int16_t value)          { Write<int16_t>(value); return *this; }
    // Записывает int32_t с учетом ByteOrder и возвращает *this.
    ByteBuffer& ByteBuffer::operator<<(int32_t value)          { Write<int32_t>(value); return *this; }
    // Записывает int64_t с учетом ByteOrder и возвращает *this.
    ByteBuffer& ByteBuffer::operator<<(int64_t value)          { Write<int64_t>(value); return *this; }
    // Записывает float с учетом ByteOrder и возвращает *this.
    ByteBuffer& ByteBuffer::operator<<(float value)            { Write<float>(value); return *this; }
    // Записывает double с учетом ByteOrder и возвращает *this.
    ByteBuffer& ByteBuffer::operator<<(double value)           { Write<double>(value); return *this; }
    // Записывает std::string_view как C-строку с '\0' и возвращает *this.
    ByteBuffer& ByteBuffer::operator<<(std::string_view value) { WriteCString(value); return *this; }
    // Записывает std::string как C-строку с '\0' и возвращает *this.
    ByteBuffer& ByteBuffer::operator<<(std::string const& str) { WriteCString(str); return *this; }
    // Записывает C-строку (nullptr преобразуется в пустую строку) и возвращает *this.
    ByteBuffer& ByteBuffer::operator<<(char const* str)        { WriteCString(str ? std::string_view(str) : std::string_view{}); return *this; }

    //////////////////////////////////////////////////////////////////////
    ///              ПОТОКОВЫЕ ОПЕРАТОРЫ ЧТЕНИЯ                        ///
    //////////////////////////////////////////////////////////////////////

    // Читает bool (1 байт) и возвращает *this.
    ByteBuffer& ByteBuffer::operator>>(bool& value)        { value = ReadBool(); return *this; }
    // Читает char (1 байт) и возвращает *this.
    ByteBuffer& ByteBuffer::operator>>(char& value)        { value = Read<char>(); return *this; }
    // Читает uint8_t и возвращает *this.
    ByteBuffer& ByteBuffer::operator>>(uint8_t& value)     { value = Read<uint8_t>(); return *this; }
    // Читает uint16_t с учетом ByteOrder и возвращает *this.
    ByteBuffer& ByteBuffer::operator>>(uint16_t& value)    { value = Read<uint16_t>(); return *this; }
    // Читает uint32_t с учетом ByteOrder и возвращает *this.
    ByteBuffer& ByteBuffer::operator>>(uint32_t& value)    { value = Read<uint32_t>(); return *this; }
    // Читает uint64_t с учетом ByteOrder и возвращает *this.
    ByteBuffer& ByteBuffer::operator>>(uint64_t& value)    { value = Read<uint64_t>(); return *this; }
    // Читает int8_t и возвращает *this.
    ByteBuffer& ByteBuffer::operator>>(int8_t& value)      { value = Read<int8_t>(); return *this; }
    // Читает int16_t с учетом ByteOrder и возвращает *this.
    ByteBuffer& ByteBuffer::operator>>(int16_t& value)     { value = Read<int16_t>(); return *this; }
    // Читает int32_t с учетом ByteOrder и возвращает *this.
    ByteBuffer& ByteBuffer::operator>>(int32_t& value)     { value = Read<int32_t>(); return *this; }
    // Читает int64_t с учетом ByteOrder и возвращает *this.
    ByteBuffer& ByteBuffer::operator>>(int64_t& value)     { value = Read<int64_t>(); return *this; }
    // Читает float с учетом ByteOrder и возвращает *this.
    ByteBuffer& ByteBuffer::operator>>(float& value)       { value = Read<float>(); return *this; }
    // Читает double с учетом ByteOrder и возвращает *this.
    ByteBuffer& ByteBuffer::operator>>(double& value)      { value = Read<double>(); return *this; }
    // Читает нуль-терминированную C-строку в std::string и возвращает *this.
    ByteBuffer& ByteBuffer::operator>>(std::string& value) { value = ReadCString(false); return *this; }

    // ==========================================
    // ИНДЕКСАЦИЯ И ДИАГНОСТИКА
    // ==========================================

    // Возвращает изменяемую ссылку на байт по индексу с проверкой выхода за границы.
    uint8_t& ByteBuffer::operator[](size_t index)
    {
        if (index >= _data.size()) // Проверяем, что индекс находится в пределах буфера.
            ThrowOutOfBounds(index, 1);
        return _data[index];       // Возвращаем ссылку на байт.
    }

    // Возвращает константную ссылку на байт по индексу с проверкой выхода за границы.
    const uint8_t& ByteBuffer::operator[](size_t index) const
    {
        if (index >= _data.size()) // Проверяем, что индекс находится в пределах буфера.
            ThrowOutOfBounds(index, 1);
        return _data[index];       // Возвращаем константную ссылку на байт.
    }

    // Форматирует байты буфера в шестнадцатеричную строку (по 16 байт в строке, с разделителем по 8 байт).
    std::string ByteBuffer::ToHexString() const
    {
        if (_data.empty()) // Для пустого буфера возвращаем пустую строку.
            return {};

        std::ostringstream oss; // Поток для сборки HEX-представления.
        oss << std::hex << std::uppercase << std::setfill('0'); // Верхний регистр, ведущие нули.

        for (size_t i = 0; i < _data.size(); ++i)
        {
            if (i > 0) // Вставляем перенос каждые 16 байт, двойной пробел каждые 8, иначе одиночный пробел.
                oss << ((i % 16 == 0) ? "\n" : (i % 8 == 0 ? "  " : " "));
            oss << std::setw(2) << static_cast<unsigned int>(_data[i]); // Выводим байт ровно 2 символами.
        }
        return oss.str(); // Возвращаем итоговый дамп.
    }

    // Форматирует байты буфера в строку десятичных чисел, разделенных пробелом.
    std::string ByteBuffer::ToDecimalString() const
    {
        if (_data.empty()) // Для пустого буфера возвращаем пустую строку.
            return {};

        std::ostringstream oss; // Поток для сборки десятичного дампа.
        for (size_t i = 0; i < _data.size(); ++i)
        {
            if (i > 0) // Разделяем значения пробелом (без лишнего пробела в конце).
                oss << ' ';
            oss << static_cast<unsigned int>(_data[i]); // Выводим числовое значение байта (0..255).
        }
        return oss.str(); // Возвращаем сформированную строку.
    }

    // Форматирует байты буфера как ASCII-строку (управляющие и не-ASCII байты заменяются на '.').
    std::string ByteBuffer::ToAsciiString() const
    {
        std::string out;        // Итоговая строка.
        out.reserve(_data.size()); // Заранее выделяем память под все байты.
        for (uint8_t byte : _data)
        {
            // Печатаемый диапазон ASCII: 32 (' ') .. 126 ('~'); остальные байты заменяем точкой.
            out.push_back((byte >= 32 && byte <= 126) ? static_cast<char>(byte) : '.');
        }
        return out; // Возвращаем ASCII-представление.
    }
}
