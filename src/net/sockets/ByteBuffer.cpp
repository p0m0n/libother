#include "ByteBuffer.h"
#include "Utils.h"


namespace other::net::sockets
{
    // Конструктор по умолчанию / с заданной начальной ёмкостью и порядком байтов.
    ByteBuffer::ByteBuffer(size_t initialCapacity, ByteOrder order)
        : _readCursor(0)     // Устанавливаем курсор чтения на начало буфера.
        , _writeCursor(0)    // Устанавливаем курсор записи на начало буфера.
        , _byteOrder(order)  // Сохраняем выбранный порядок байтов.
        , _writeBitAcc(0)    // Обнуляем аккумулятор битов записи.
        , _writeBitCount(0)  // Недописанных бит пока нет.
        , _readBitCache(0)   // Обнуляем кэш битов чтения.
        , _readBitLeft(0)    // Непрочитанных бит в кэше нет.
    {
        // Если запрошена ненулевая ёмкость, резервируем память заранее для снижения числа аллокаций.
        if (initialCapacity > 0)
            _data.reserve(initialCapacity);
    }

    // Конструктор, принимающий готовый вектор байтов во владение через move-семантику.
    ByteBuffer::ByteBuffer(std::vector<uint8_t> initialData, ByteOrder order) noexcept
        : _data(std::move(initialData)) // Перемещаем владение буфером без копирования байтов.
        , _readCursor(0)                // Чтение начинается с первого байта.
        , _writeCursor(_data.size())    // Запись продолжится с конца переданных данных.
        , _byteOrder(order)             // Сохраняем выбранный порядок байтов.
        , _writeBitAcc(0)               // Аккумулятор битов записи пуст.
        , _writeBitCount(0)             // Счётчик битов записи равен 0.
        , _readBitCache(0)              // Кэш битов чтения пуст.
        , _readBitLeft(0)               // Счётчик оставшихся бит чтения равен 0.
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

    // Конструктор перемещения с присвоением.
    ByteBuffer::ByteBuffer(ByteBuffer&& other) noexcept
        : _data(std::move(other._data))        // Переносим вектор байтов за O(1).
        , _readCursor(other._readCursor)       // Переносим позицию курсора чтения.
        , _writeCursor(other._writeCursor)     // Переносим позицию курсора записи.
        , _byteOrder(other._byteOrder)         // Переносим настройку порядка байтов.
        , _writeBitAcc(other._writeBitAcc)     // Переносим аккумулятор битовой записи.
        , _writeBitCount(other._writeBitCount) // Переносим число накопленных бит записи.
        , _readBitCache(other._readBitCache)   // Переносим кэш битового чтения.
        , _readBitLeft(other._readBitLeft)     // Переносим остаток бит в кэше чтения.
    {
        // Оставляем исходный объект в чистом, валидном пустом состоянии.
        other.Clear();
    }

    // Оператор перемещения с присвоением.
    ByteBuffer& ByteBuffer::operator=(ByteBuffer&& other) noexcept
    {
        // Защита от самоприсваивания (buf = std::move(buf)).
        if (this != &other)
        {
            _data          = std::move(other._data); // Переносим хранилище байтов.
            _readCursor    = other._readCursor;      // Переносим курсор чтения.
            _writeCursor   = other._writeCursor;     // Переносим курсор записи.
            _byteOrder     = other._byteOrder;       // Переносим порядок байтов.
            _writeBitAcc   = other._writeBitAcc;     // Переносим аккумулятор записи.
            _writeBitCount = other._writeBitCount;   // Переносим счётчик бит записи.
            _readBitCache  = other._readBitCache;    // Переносим кэш чтения.
            _readBitLeft   = other._readBitLeft;     // Переносим остаток бит чтения.
            other.Clear();                           // Сбрасываем исходный объект.
        }
        return *this; // Возвращаем ссылку на текущий объект.
    }

    // Формирует подробное диагностическое сообщение и выбрасывает исключение при выходе за границы буфера.
    void ByteBuffer::ThrowOutOfBounds(size_t offset, size_t requested) const
    {
        std::string msg = "ByteBuffer: out of bounds (offset=";
        msg += std::to_string(offset);
        msg += ", requested=";
        msg += std::to_string(requested);
        msg += ", size=";
        msg += std::to_string(_data.size());
        msg += ").";
        throw std::out_of_range(msg);
    }

    // Устанавливает позицию курсора чтения и сбрасывает битовый кэш чтения.
    void ByteBuffer::SetReadOffset(size_t offset)
    {
        // Проверяем границы до изменения состояния объекта.
        if (offset > _data.size())
            ThrowOutOfBounds(offset, 0);
		
		// Сбрасываем кэш побитового чтения.
        AlignReadToByte();
		
		// Обновляем позицию чтения.
        _readCursor = offset;
    }

    // Выравнивает незавершённые биты и устанавливает позицию курсора записи.
    void ByteBuffer::SetWriteOffset(size_t offset)
    {
        // Запрещаем установку курсора записи за пределы существующих данных.
        if (offset > _data.size())
            ThrowOutOfBounds(offset, 0);
		
		// Сбрасываем недописанные биты перед перемещением курсора.
        AlignWriteToByte();
		
		// Обновляем позицию записи.
        _writeCursor = offset;
    }

    // Полностью очищает буфер и сбрасывает все курсоры и битовые буферы.
    void ByteBuffer::Clear() noexcept
    {
        _data.clear();       // Очищаем содержимое вектора (без освобождения зарезервированной capacity).
        _readCursor    = 0;  // Сбрасываем курсор чтения.
        _writeCursor   = 0;  // Сбрасываем курсор записи.
        _writeBitAcc   = 0;  // Очищаем битовый аккумулятор записи.
        _writeBitCount = 0;  // Обнуляем счётчик бит записи.
        _readBitCache  = 0;  // Очищаем битовый кэш чтения.
        _readBitLeft   = 0;  // Обнуляем остаток бит чтения.
    }

    // Перематывает чтение в начало буфера, сохраняя записанные данные.
    void ByteBuffer::Rewind() noexcept
    {
        _readCursor   = 0;  // Возвращаем курсор чтения на 0.
        _readBitCache = 0;  // Сбрасываем битовый кэш чтения.
        _readBitLeft  = 0;  // Обнуляем счётчик оставшихся бит.
    }

    // Резервирует память под указанное количество байтов.
    void ByteBuffer::Reserve(size_t newCapacity)
    {
		// Резервируем только если требуемая ёмкость больше текущей.
        if (newCapacity > _data.capacity())
            _data.reserve(newCapacity);
    }

    // Изменяет логический размер буфера.
    void ByteBuffer::Resize(size_t newSize)
    {
		// Выравниваем незавершённые биты записи.
        AlignWriteToByte();
		
		// Изменяем размер вектора (новые байты заполняются нулями).
        _data.resize(newSize, 0);
        
		// Сбрасываем кэш побитового чтения после успешного resize().
		AlignReadToByte();
        
		// Если курсор чтения оказался за новым концом, подтягиваем его.
		if (_readCursor > newSize)
            _readCursor = newSize;
			
		// Курсор записи устанавливаем в конец буфера.
        _writeCursor = newSize;
    }

    // Запрашивает освобождение лишней зарезервированной памяти.
    void ByteBuffer::ShrinkToFit()
    {
		// Просим вектор сократить capacity до size().
        _data.shrink_to_fit();
    }

    // Извлекает внутренний вектор байтов наружу и очищает текущий объект.
    std::vector<uint8_t> ByteBuffer::TakeData()
    {
		// Дописываем остаток битового аккумулятора, если он есть.
        AlignWriteToByte();
		
		// Забираем вектор без копирования.
        std::vector<uint8_t> result = std::move(_data);
		
		// Сбрасываем курсоры и состояние текущего объекта.
        Clear();
        return result;
    }

    // Записывает логическое значение как 1 байт (1 или 0).
    void ByteBuffer::WriteBool(bool value)
    {
		// Приводим bool к строгому байтовому представлению 0/1.
        Write<uint8_t>(value ? 1U : 0U);
    }

    // Записывает массив байтов в текущую позицию записи с автоматическим расширением буфера.
    void ByteBuffer::WriteBytes(const uint8_t* src, size_t count)
    {
        if (count == 0)
            return;
			
        if (src == nullptr)
            throw std::invalid_argument("ByteBuffer::WriteBytes: source pointer is null.");

        // Проверяем переполнение ДО сброса битового аккумулятора (строгая гарантия исключений).
        const size_t maxSize     = std::numeric_limits<size_t>::max();
        const size_t pendingByte = (_writeBitCount > 0) ? 1U : 0U;
        if (pendingByte > maxSize - _writeCursor || count > maxSize - (_writeCursor + pendingByte))
            throw std::length_error("ByteBuffer::WriteBytes: size overflow.");

        // Заранее резервируем итоговый объём, чтобы AlignWriteToByte() не оставил частичную запись при bad_alloc.
        const size_t requiredSize = _writeCursor + pendingByte + count;
        if (requiredSize > _data.capacity())
            _data.reserve(requiredSize);

        // Перед байтовой записью завершаем любой незакрытый битовый пакет.
        AlignWriteToByte();

        // Если буфер короче требуемого размера — расширяем его (память уже зарезервирована выше).
        if (requiredSize > _data.size())
            _data.resize(requiredSize);
		
		// Копируем данные в целевую область памяти.
        std::memcpy(&_data[_writeCursor], src, count);
		
		// Сдвигаем курсор записи вперёд.
        _writeCursor = requiredSize;
    }

    // Дописывает содержимое другого ByteBuffer в текущий буфер (защищено от самозаписи buf.WriteBuffer(buf)).
    void ByteBuffer::WriteBuffer(const ByteBuffer& other)
    {
        // Отдельно обрабатываем самозапись, чтобы resize() не инвалидировал указатель other.Data() до копирования.
        if (this == &other)
        {
			// Фиксируем незавершённые биты, чтобы получить актуальный размер данных.
            AlignWriteToByte();
            if (_data.empty())
                return;

            const size_t count = _data.size();
            if (count > std::numeric_limits<size_t>::max() - _writeCursor)
                throw std::length_error("ByteBuffer::WriteBuffer: size overflow.");

            const size_t requiredSize = _writeCursor + count;
            if (requiredSize > _data.size())
                _data.resize(requiredSize);

            // Используем std::memmove, так как исходная и целевая области внутри _data могут перекрываться.
            std::memmove(&_data[_writeCursor], _data.data(), count);
			
			// Сдвигаем курсор записи вперёд.
            _writeCursor = requiredSize;
            return;
        }

        // Для стороннего буфера делегируем стандартной записи байтов.
        if (!other.Empty())
            WriteBytes(other.Data(), other.Size());
    }

    // Записывает сырые байты строки (без '\0').
    void ByteBuffer::WriteString(std::string_view text)
    {
		// Пропускаем пустую строку.
        if (!text.empty())
            WriteBytes(reinterpret_cast<const uint8_t*>(text.data()), text.size());
    }

    // Записывает строку с завершающим нулевым байтом ('\0').
    void ByteBuffer::WriteCString(std::string_view text)
    {
        // Проверяем возможное переполнение (строка + '\0' + возможный байт выравнивания бит) без underflow.
        const size_t maxSize     = std::numeric_limits<size_t>::max();
        const size_t pendingByte = (_writeBitCount > 0) ? 1U : 0U;
        if (pendingByte > maxSize - _writeCursor)
            throw std::length_error("ByteBuffer::WriteCString: size overflow.");

        const size_t baseOffset = _writeCursor + pendingByte;
        if (1U > maxSize - baseOffset || text.size() > maxSize - baseOffset - 1U)
            throw std::length_error("ByteBuffer::WriteCString: size overflow.");

        // Резервируем место сразу под строку и '\0', чтобы избежать двойной аллокации и частичной записи.
        const size_t requiredSize = baseOffset + text.size() + 1U;
        if (requiredSize > _data.capacity())
            _data.reserve(requiredSize);
		
		// Сначала записываем символы строки.
        WriteString(text);
		
		// Затем добавляем нуль-терминатор.
        Write<uint8_t>(0);
    }

    // Записывает 32-битную длину строки (uint32_t) и сами байты строки.
    void ByteBuffer::WriteLengthPrefixedString(std::string_view text)
    {
        // Проверяем, что длина строки помещается в 32-битный префикс.
        if (text.size() > std::numeric_limits<uint32_t>::max())
            throw std::length_error("ByteBuffer::WriteLengthPrefixedString: string exceeds uint32_t max length.");

        // Проверяем возможное переполнение (префикс + тело строки + возможный байт выравнивания бит).
        const size_t maxSize     = std::numeric_limits<size_t>::max();
        const size_t pendingByte = (_writeBitCount > 0) ? 1U : 0U;
        if (pendingByte > maxSize - _writeCursor)
            throw std::length_error("ByteBuffer::WriteLengthPrefixedString: size overflow.");

        const size_t baseOffset = _writeCursor + pendingByte;
        if (sizeof(uint32_t) > maxSize - baseOffset || text.size() > maxSize - baseOffset - sizeof(uint32_t))
            throw std::length_error("ByteBuffer::WriteLengthPrefixedString: size overflow.");

        // Резервируем итоговый объём заранее, чтобы гарантировать атомарность записи заголовка и тела.
        const size_t requiredSize = baseOffset + sizeof(uint32_t) + text.size();
        if (requiredSize > _data.capacity())
            _data.reserve(requiredSize);
		
		// Записываем длину с учётом ByteOrder.
        Write<uint32_t>(static_cast<uint32_t>(text.size()));
		
		// Записываем содержимое строки.
        WriteString(text);
    }

    // Перезаписывает блок байтов по заданному смещению без изменения _writeCursor.
    void ByteBuffer::WriteBytesAt(size_t offset, const uint8_t* src, size_t count)
    {
        if (count == 0)
            return;
		
        if (src == nullptr)
            throw std::invalid_argument("ByteBuffer::WriteBytesAt: source pointer is null.");
		
        // Проверяем, что диапазон [offset, offset + count) полностью лежит внутри буфера.
        if (offset > _data.size() || count > _data.size() - offset)
            ThrowOutOfBounds(offset, count);
		
		// Копируем данные по указанному смещению.
        std::memcpy(&_data[offset], src, count);
    }

    // Читает 1 байт и преобразует его в bool.
    bool ByteBuffer::ReadBool()
    {
		// Любое ненулевое значение безопасно трактуется как true.
        return Read<uint8_t>() != 0;
    }

    // Читает count байтов из буфера в массив dst.
    void ByteBuffer::ReadBytes(uint8_t* dst, size_t count)
    {
        if (count == 0)
            return;
		
        if (dst == nullptr)
            throw std::invalid_argument("ByteBuffer::ReadBytes: destination pointer is null.");

        // Проверяем наличие достаточного числа байтов ДО сброса битового кэша.
        if (_readCursor > _data.size() || count > _data.size() - _readCursor)
            ThrowOutOfBounds(_readCursor, count);
		
		// Сбрасываем остаток битового кэша перед байтовым чтением.
        AlignReadToByte();
		
		// Копируем байты в выходной буфер.
        std::memcpy(dst, &_data[_readCursor], count);
		
		// Сдвигаем курсор чтения вперёд.
        _readCursor += count;
    }

    // Читает count байтов и возвращает их в новом векторе.
    std::vector<uint8_t> ByteBuffer::ReadBytes(size_t count)
    {
        if (count == 0)
            return {};

        // Проверяем границы ДО выделения вектора (защита от OOM/DoS при некорректном count).
        if (_readCursor > _data.size() || count > _data.size() - _readCursor)
            ThrowOutOfBounds(_readCursor, count);
		
		// Выделяем вектор нужного размера (при bad_alloc курсор не меняется).
        std::vector<uint8_t> result(count);
		
		// Копируем байты и сдвигаем курсор чтения.
        ReadBytes(result.data(), count);
        return result;
    }

    // Читает строку фиксированной длины (в байтах).
    std::string ByteBuffer::ReadString(size_t length, bool validateUtf8)
    {
        // Проверяем наличие достаточного количества байтов ДО изменения состояния объекта.
        if (_readCursor > _data.size() || length > _data.size() - _readCursor)
            ThrowOutOfBounds(_readCursor, length);
		
		// Для нулевой длины выравниваем чтение и возвращаем пустую строку.
        if (length == 0)
        {
            AlignReadToByte();
            return {};
        }

        // Создаём временное представление для проверки UTF-8 без лишних копирований.
        std::string_view view(reinterpret_cast<const char*>(&_data[_readCursor]), length);
        if (validateUtf8 && !utils::utf8::IsValidUtf8(view))
            throw std::invalid_argument("ByteBuffer::ReadString: invalid UTF-8 sequence.");

        // Сначала выделяем строку, и только при успехе обновляем битовый кэш и курсор.
        std::string result(view);
		
		// Сбрасываем остаток битового кэша перед байтовым чтением.
        AlignReadToByte();
		
		// Сдвигаем курсор чтения вперёд.
        _readCursor += length;
        return result;
    }

    // Читает нуль-терминированную C-строку до первого байта '\0'.
    std::string ByteBuffer::ReadCString(bool validateUtf8)
    {
        // Если курсор уже в конце буфера, читать нечего.
        if (_readCursor >= _data.size())
            ThrowOutOfBounds(_readCursor, 1);

        const char* begin = reinterpret_cast<const char*>(_data.data()) + _readCursor;  // Начало поиска.
        const char* end   = reinterpret_cast<const char*>(_data.data()) + _data.size(); // Конец буфера.
        const char* nul   = std::find(begin, end, '\0');                                // Ищем нулевой терминатор.
        if (nul == end)                                                                 // Если '\0' не найден — ошибка границ.
            ThrowOutOfBounds(_readCursor, _data.size() - _readCursor + 1);

        std::string_view view(begin, static_cast<size_t>(nul - begin)); // Формируем срез без '\0'.
        if (validateUtf8 && !utils::utf8::IsValidUtf8(view))            // При необходимости проверяем UTF-8.
            throw std::invalid_argument("ByteBuffer::ReadCString: invalid UTF-8 sequence.");

        // Выделяем результирующую строку до изменения состояния курсора.
        std::string result(view);
        AlignReadToByte();
		
		// Пропускаем саму строку и завершающий байт '\0'.
        _readCursor += view.size() + 1;
        return result;
    }

    // Читает строку с 32-битным префиксом длины (атомарно относительно _readCursor).
    std::string ByteBuffer::ReadLengthPrefixedString(bool validateUtf8)
    {
        // Подглядываем 32-битную длину без изменения курсора и битового кэша.
        const uint32_t len = Peek<uint32_t>(_readCursor);

        // Проверяем, что тело строки целиком помещается в буфер после 4-байтного заголовка.
        const size_t bodyOffset = _readCursor + sizeof(uint32_t);
        if (static_cast<size_t>(len) > _data.size() - bodyOffset)
            ThrowOutOfBounds(bodyOffset, len);
		
		// Пустая строка: выравниваем кэш и сдвигаем курсор за префикс длины.
        if (len == 0)
        {
            AlignReadToByte();
            _readCursor = bodyOffset;
            return {};
        }

        // При необходимости валидируем UTF-8 до изменения курсора.
        std::string_view view(reinterpret_cast<const char*>(&_data[bodyOffset]), len);
        if (validateUtf8 && !utils::utf8::IsValidUtf8(view))
            throw std::invalid_argument("ByteBuffer::ReadLengthPrefixedString: invalid UTF-8 sequence.");

        // Выделяем строку, и только при успехе сдвигаем курсор за заголовок и тело строки.
        std::string result(view);
        AlignReadToByte();
        _readCursor = bodyOffset + len;
        return result;
    }

    // Пропускает указанное число байтов при чтении.
    void ByteBuffer::Skip(size_t bytes)
    {
        // Проверяем, что пропуск не выведет курсор за пределы буфера, до сброса битового кэша.
        if (_readCursor > _data.size() || bytes > _data.size() - _readCursor)
            ThrowOutOfBounds(_readCursor, bytes);
		
		// Выравниваем чтение на границу байта.
        AlignReadToByte();
		
		// Сдвигаем курсор чтения.
        _readCursor += bytes;
    }

    // Записывает один бит в аккумулятор; при заполнении 8 бит сбрасывает байт в буфер.
    void ByteBuffer::WriteBit(bool bit)
    {
        // Сохраняем текущее состояние аккумулятора для отката при возможном исключении в WriteBytes().
        const uint8_t prevAcc   = _writeBitAcc;
        const uint8_t prevCount = _writeBitCount;

        // Сдвигаем накопленные биты влево и записываем новый бит в младший разряд.
        _writeBitAcc = static_cast<uint8_t>((_writeBitAcc << 1) | (bit ? 1U : 0U));
        ++_writeBitCount; // Увеличиваем счётчик накопленных бит.

        // Как только собран полный байт (8 бит), записываем его в _data.
        if (_writeBitCount == 8)
        {
			// Сохраняем готовый байт.
            const uint8_t fullByte = _writeBitAcc;
			
			// Обнуляем аккумулятор до вызова WriteBytes.
            _writeBitAcc   = 0;
			
			// Обнуляем счётчик, чтобы избежать рекурсивного AlignWriteToByte.
            _writeBitCount = 0;
            try
            {
				// Записываем собранный байт в буфер.
                WriteBytes(&fullByte, 1);
            }
            catch (...)
            {
                // Восстанавливаем исходный аккумулятор, чтобы не потерять накопленные ранее биты.
                _writeBitAcc   = prevAcc;
                _writeBitCount = prevCount;
                throw;
            }
        }
    }

    // Записывает младшие bitCount бит из value (от старшего бита к младшему).
    void ByteBuffer::WriteBits(uint64_t value, uint8_t bitCount)
    {
        // Проверяем допустимый диапазон числа бит (1..64).
        if (bitCount == 0 || bitCount > 64)
            throw std::out_of_range("ByteBuffer::WriteBits: bitCount must be in range [1, 64].");

        // Вычисляем, сколько полных байтов будет сброшено в _data, и резервируем память заранее.
        const size_t bytesToFlush = (static_cast<size_t>(_writeBitCount) + static_cast<size_t>(bitCount)) / 8U;
        if (bytesToFlush > std::numeric_limits<size_t>::max() - _writeCursor)
            throw std::length_error("ByteBuffer::WriteBits: size overflow.");

        const size_t requiredSize = _writeCursor + bytesToFlush;
        if (requiredSize > _data.capacity())
            _data.reserve(requiredSize);

        // Извлекаем и записываем биты по одному, начиная со старшего (bitCount - 1) до 0.
        for (uint8_t i = bitCount; i > 0; --i)
        {
            WriteBit(((value >> (i - 1)) & 1ULL) != 0);
        }
    }

    // Сбрасывает неполный байт битового аккумулятора в буфер, дополняя младшие разряды нулями.
    void ByteBuffer::AlignWriteToByte()
    {
        if (_writeBitCount == 0)
            return;

        // Сохраняем состояние на случай исключения при расширении _data.
        const uint8_t prevAcc   = _writeBitAcc;
        const uint8_t prevCount = _writeBitCount;

        // Сдвигаем накопленные биты в старшие разряды байта (младшие заполняются нулями).
        const uint8_t padded = static_cast<uint8_t>(_writeBitAcc << (8 - _writeBitCount));
		
		// Сбрасываем аккумулятор.
        _writeBitAcc   = 0;
		
		// Сбрасываем счётчик до вызова WriteBytes.
        _writeBitCount = 0;
		
        try
        {
			// Записываем выровненный байт в буфер.
            WriteBytes(&padded, 1);
        }
        catch (...)
        {
            // При ошибке выделения памяти возвращаем незаписанные биты обратно в аккумулятор.
            _writeBitAcc   = prevAcc;
            _writeBitCount = prevCount;
            throw;
        }
    }

    // Читает один бит из кэша чтения (при опустошении кэша подгружает следующий байт).
    bool ByteBuffer::ReadBit()
    {
        // Если в кэше не осталось непрочитанных бит, берём следующий байт из _data.
        if (_readBitLeft == 0)
        {
			// Проверяем наличие хотя бы 1 байта.
            if (_readCursor >= _data.size())
                ThrowOutOfBounds(_readCursor, 1);
			
			// Загружаем байт в кэш и сдвигаем курсор чтения.
            _readBitCache = _data[_readCursor++];
			
			// Теперь в кэше 8 доступных бит.
            _readBitLeft  = 8;
        }
		
		// Уменьшаем число оставшихся бит.
        --_readBitLeft;
		
		// Извлекаем очередной бит (от старшего к младшему).
        return ((_readBitCache >> _readBitLeft) & 1U) != 0;
    }

    // Читает bitCount бит и собирает их в 64-битное беззнаковое число.
    uint64_t ByteBuffer::ReadBits(uint8_t bitCount)
    {
        // Проверяем допустимый диапазон числа бит (1..64).
        if (bitCount == 0 || bitCount > 64)
            throw std::out_of_range("ByteBuffer::ReadBits: bitCount must be in range [1, 64].");

        // Проверяем наличие всех bitCount бит заранее (без переполнения и без порчи курсора при ошибке).
        const size_t bytesLeft = (_readCursor < _data.size()) ? (_data.size() - _readCursor) : 0;
        if (bytesLeft < 8)
        {
            const size_t bitsLeft = static_cast<size_t>(_readBitLeft) + bytesLeft * 8U;
            if (static_cast<size_t>(bitCount) > bitsLeft)
            {
                const size_t neededBits  = static_cast<size_t>(bitCount) - static_cast<size_t>(_readBitLeft);
                const size_t neededBytes = (neededBits + 7U) / 8U;
                ThrowOutOfBounds(_readCursor, neededBytes);
            }
        }
		
		// Последовательно читаем биты и сдвигаем их в результат.
        uint64_t result = 0;
        for (uint8_t i = 0; i < bitCount; ++i)
        {
            result = (result << 1) | (ReadBit() ? 1ULL : 0ULL);
        }
		
        return result;
    }

    // Сбрасывает остаток битового кэша чтения, выравнивая дальнейшее чтение по байту.
    void ByteBuffer::AlignReadToByte() noexcept
    {
		// Очищаем кэшированный байт.
        _readBitCache = 0;
		
		// Обнуляем счётчик оставшихся бит.
        _readBitLeft  = 0;
    }

    ByteBuffer& ByteBuffer::operator<<(bool value)             { WriteBool(value);        return *this; }
    ByteBuffer& ByteBuffer::operator<<(char value)             { Write<char>(value);      return *this; }
    ByteBuffer& ByteBuffer::operator<<(uint8_t value)          { Write<uint8_t>(value);   return *this; }
    ByteBuffer& ByteBuffer::operator<<(uint16_t value)         { Write<uint16_t>(value);  return *this; }
    ByteBuffer& ByteBuffer::operator<<(uint32_t value)         { Write<uint32_t>(value);  return *this; }
    ByteBuffer& ByteBuffer::operator<<(uint64_t value)         { Write<uint64_t>(value);  return *this; }
    ByteBuffer& ByteBuffer::operator<<(int8_t value)           { Write<int8_t>(value);    return *this; }
    ByteBuffer& ByteBuffer::operator<<(int16_t value)          { Write<int16_t>(value);   return *this; }
    ByteBuffer& ByteBuffer::operator<<(int32_t value)          { Write<int32_t>(value);   return *this; }
    ByteBuffer& ByteBuffer::operator<<(int64_t value)          { Write<int64_t>(value);   return *this; }
    ByteBuffer& ByteBuffer::operator<<(float value)            { Write<float>(value);     return *this; }
    ByteBuffer& ByteBuffer::operator<<(double value)           { Write<double>(value);    return *this; }
    ByteBuffer& ByteBuffer::operator<<(std::string_view value) { WriteCString(value);     return *this; }
    ByteBuffer& ByteBuffer::operator<<(std::string const& str) { WriteCString(str);       return *this; }
    ByteBuffer& ByteBuffer::operator<<(char const* str)        { WriteCString(str ? std::string_view(str) : std::string_view{}); return *this; }

    ByteBuffer& ByteBuffer::operator>>(bool& value)        { value = ReadBool();          return *this; }
    ByteBuffer& ByteBuffer::operator>>(char& value)        { value = Read<char>();        return *this; }
    ByteBuffer& ByteBuffer::operator>>(uint8_t& value)     { value = Read<uint8_t>();     return *this; }
    ByteBuffer& ByteBuffer::operator>>(uint16_t& value)    { value = Read<uint16_t>();    return *this; }
    ByteBuffer& ByteBuffer::operator>>(uint32_t& value)    { value = Read<uint32_t>();    return *this; }
    ByteBuffer& ByteBuffer::operator>>(uint64_t& value)    { value = Read<uint64_t>();    return *this; }
    ByteBuffer& ByteBuffer::operator>>(int8_t& value)      { value = Read<int8_t>();      return *this; }
    ByteBuffer& ByteBuffer::operator>>(int16_t& value)     { value = Read<int16_t>();     return *this; }
    ByteBuffer& ByteBuffer::operator>>(int32_t& value)     { value = Read<int32_t>();     return *this; }
    ByteBuffer& ByteBuffer::operator>>(int64_t& value)     { value = Read<int64_t>();     return *this; }
    ByteBuffer& ByteBuffer::operator>>(float& value)       { value = Read<float>();       return *this; }
    ByteBuffer& ByteBuffer::operator>>(double& value)      { value = Read<double>();      return *this; }
    ByteBuffer& ByteBuffer::operator>>(std::string& value) { value = ReadCString(false);  return *this; }

    // Возвращает изменяемую ссылку на байт по индексу с проверкой выхода за границы.
    uint8_t& ByteBuffer::operator[](size_t index)
    {
		// Проверяем, что индекс находится в пределах буфера.
        if (index >= _data.size())
            ThrowOutOfBounds(index, 1);
        return _data[index];
    }

    // Возвращает константную ссылку на байт по индексу с проверкой выхода за границы.
    const uint8_t& ByteBuffer::operator[](size_t index) const
    {
		// Проверяем, что индекс находится в пределах буфера.
        if (index >= _data.size())
            ThrowOutOfBounds(index, 1);
        return _data[index];
    }

    // Форматирует байты буфера в шестнадцатеричную строку (по 16 байт в строке, с разделителем по 8 байт).
    std::string ByteBuffer::ToHexString() const
    {
        if (_data.empty())
            return {};

        static constexpr char HexDigits[] = "0123456789ABCDEF";
        std::string result;
		
        // Резервируем память заранее (2 HEX-символа + разделитель на байт + двойной пробел каждые 8 байт).
        result.reserve(_data.size() * 3U + (_data.size() / 8U));

        for (size_t i = 0; i < _data.size(); ++i)
        {
            if (i > 0) // Вставляем перенос каждые 16 байт, двойной пробел каждые 8, иначе одиночный пробел.
            {
                if (i % 16 == 0)
                    result.push_back('\n');
                else if (i % 8 == 0)
                    result.append("  ");
                else
                    result.push_back(' ');
            }

            // Выводим старший и младший полубайты в верхнем регистре (независимо от локали).
            const uint8_t byte = _data[i];
            result.push_back(HexDigits[(byte >> 4) & 0x0FU]);
            result.push_back(HexDigits[byte & 0x0FU]);
        }
		
        return result;
    }

    // Форматирует байты буфера в строку десятичных чисел (0..255), разделённых пробелом.
    std::string ByteBuffer::ToDecimalString() const
    {
        if (_data.empty())
            return {};

        std::string result;
		
        // В худшем случае каждый байт занимает 3 цифры и 1 пробел (4 символа).
        result.reserve(_data.size() * 4U);

        for (size_t i = 0; i < _data.size(); ++i)
        {
            if (i > 0) // Разделяем значения пробелом (без лишнего пробела в конце).
                result.push_back(' ');

            // Быстрое преобразование uint8_t (0..255) в десятичные цифры без локали и потоков.
            const uint8_t byte = _data[i];
            if (byte >= 100)
            {
                result.push_back(static_cast<char>('0' + (byte / 100)));
                result.push_back(static_cast<char>('0' + ((byte / 10) % 10)));
                result.push_back(static_cast<char>('0' + (byte % 10)));
            }
            else if (byte >= 10)
            {
                result.push_back(static_cast<char>('0' + (byte / 10)));
                result.push_back(static_cast<char>('0' + (byte % 10)));
            }
            else
            {
                result.push_back(static_cast<char>('0' + byte));
            }
        }
        return result;
    }

    // Форматирует байты буфера как ASCII-строку (управляющие и не-ASCII байты заменяются на '.').
    std::string ByteBuffer::ToAsciiString() const
    {
        std::string result;
		
		// Заранее выделяем память под все байты.
        result.reserve(_data.size());
        for (uint8_t byte : _data)
        {
            // Печатаемый диапазон ASCII: 32 (' ') .. 126 ('~'); остальные байты заменяем точкой.
            result.push_back((byte >= 32 && byte <= 126) ? static_cast<char>(byte) : '.');
        }
        return result;
    }
}
