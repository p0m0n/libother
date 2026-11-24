#include "StreamSocket.h"
#include "Utils.h"
#include "private\PrivateGlobal.hpp"

namespace other::net::sockets
{
    using namespace other::utils;
	
	// Внутренний помощник: возвращает число непрочитанных байтов в локальном буфере предчтения `_readBuf`.
	size_t StreamSocket::BufferedBytes() const noexcept 
	{
		return _readBuf.Remaining(); 
	}
	
    // Переводит поток в состояние ошибки (после этого `if (stream)` вернёт false до вызова ClearError()).
    void StreamSocket::SetStreamError(int errCode) noexcept
    {
        _good      = false;
        _lastError = errCode;
        SET_NET_ERROR(errCode);
    }

    // Отдаёт 1 байт из локального буфера предчтения _readBuf (ByteBuffer).
    // При опустошении делает один системный вызов recv() до 4 КБ (с автоматическим повтором при EINTR).
    int StreamSocket::ReadNextByte(char& outCh)
    {
        if (_readBuf.Remaining() == 0)
        {
            if (_socket.IsClosed())
            {
                SetStreamError(SOCK_EBADF);
                return -1;
            }

            // Готовим место под порцию до 4 КБ (память выделяется лениво один раз и далее переиспользуется).
            _readBuf.Clear();
            _readBuf.Resize(DefaultReadBufferSize);

            int n = 0;
            while (true)
            {
                n = _socket.Recv(reinterpret_cast<char*>(_readBuf.Data()), static_cast<int>(DefaultReadBufferSize));
                if (n < 0 && GET_NET_ERROR() == SOCK_EINTR)
                    continue;
                break;
            }

            // 0 = штатный EOF (ошибку здесь не ставим, чтобы ReadLine вернул финальную строку без '\n');
            // -1 = сетевая ошибка.
            if (n <= 0)
            {
                _readBuf.Clear();
                if (n < 0)
                    SetStreamError(GET_NET_ERROR());
                return n;
            }

            // Усекаем размер буфера до реально принятых n байт и ставим курсор чтения в начало.
            _readBuf.Resize(static_cast<size_t>(n));
            _readBuf.Rewind();
        }

        // Читаем очередной байт из _readBuf (внутренний курсор сдвигается автоматически).
        outCh = static_cast<char>(_readBuf.Read<uint8_t>());
        return 1;
    }

    // Конструктор для lvalue-сокета: забирает владение через std::move без выделения памяти.
    StreamSocket::StreamSocket(Socket& socket) noexcept
        : _socket(std::move(socket)) // Перемещаем сокет внутрь потока (исходный объект становится пустым).
        , _readBuf(0)                // Буфер предчтения выделяется лениво при первом вызове ReadNextByte().
        , _good(true)                // Поток изначально в исправном состоянии.
        , _lastError(0)              // Ошибок пока не было.
    {
    }

    // Конструктор для временного сокета (например: StreamSocket s(server.Accept());).
    StreamSocket::StreamSocket(Socket&& socket) noexcept
        : _socket(std::move(socket)) // Забираем владение временным сокетом.
        , _readBuf(0)                // Ленивый буфер предчтения гарантирует безопасность noexcept.
        , _good(true)
        , _lastError(0)
    {
    }

    // Конструктор перемещения: переносит ресурсы и переводит источник в закрытое состояние.
    StreamSocket::StreamSocket(StreamSocket&& other) noexcept
        : _socket(std::move(other._socket))
        , _readBuf(std::move(other._readBuf))
        , _good(other._good)
        , _lastError(other._lastError)
    {
        other._good      = false;
        other._lastError = 0;
    }

    // Оператор перемещающего присваивания: закрывает текущий поток и переносит состояние other.
    StreamSocket& StreamSocket::operator=(StreamSocket&& other) noexcept
    {
        if (this != &other)
        {
            Dispose();
            _socket          = std::move(other._socket);
            _readBuf         = std::move(other._readBuf);
            _good            = other._good;
            _lastError       = other._lastError;
            other._good      = false;
            other._lastError = 0;
        }
        return *this;
    }

    // Деструктор: закрывает внутренний сокет и очищает буфер предчтения (RAII).
    StreamSocket::~StreamSocket() noexcept
    {
        Dispose();
    }

    bool StreamSocket::IsGood() const noexcept   { return _good; }
    StreamSocket::operator bool() const noexcept { return _good; }
	int StreamSocket::LastError() const noexcept { return _lastError; }
	void StreamSocket::ClearError() noexcept     { _good = true; _lastError = 0; }
    int StreamSocket::GetWriteTimeout() const noexcept      { return _socket.IsCreated() ? _socket.GetSendTimeout() : -1; }
    int StreamSocket::SetWriteTimeout(int timeout) noexcept { return _socket.IsCreated() ? _socket.SetSendTimeout(timeout) : -1; }
    int StreamSocket::GetReadTimeout() const noexcept       { return _socket.IsCreated() ? _socket.GetRecvTimeout() : -1; }
    int StreamSocket::SetReadTimeout(int timeout) noexcept  { return _socket.IsCreated() ? _socket.SetRecvTimeout(timeout) : -1; }

    // Сетевой поток поддерживает таймауты, но не поддерживает произвольную перемотку (Seek).
    bool StreamSocket::CanTimeout() const noexcept { return true; }
    bool StreamSocket::CanSeek()    const noexcept { return false; }

    // Запись возможна, только пока сам сокет открыт и TCP-соединение активно (без учёта _readBuf).
    bool StreamSocket::CanWrite() const noexcept
    {
        return _socket.IsCreated() && _socket.IsConnected();
    }

    // Чтение возможно, если в локальном _readBuf остались байты ИЛИ сокет открыт и подключён.
    bool StreamSocket::CanRead() const noexcept
    {
        if (BufferedBytes() > 0)
            return true;
		
        return _socket.IsCreated() && _socket.IsConnected();
    }

    // Прямой доступ к внутреннему объекту Socket.
    Socket& StreamSocket::SockInternal() noexcept             { return _socket; }
    const Socket& StreamSocket::SockInternal() const noexcept { return _socket; }

    // Отправляет до size байт за один системный вызов send() (с повтором при EINTR).
    int StreamSocket::Write(const char* buffer, int size)
    {
        if (_socket.IsClosed())
        {
            SetStreamError(SOCK_EBADF);
            return -1;
        }
		
        if (buffer == nullptr || size <= 0)
        {
            SetStreamError(SOCK_EINVAL);
            return -1;
        }

        int sent = 0;
        while (true)
        {
            sent = _socket.Send(buffer, size);
            if (sent < 0 && GET_NET_ERROR() == SOCK_EINTR)
                continue;
            break;
        }

        if (sent < 0)
            SetStreamError(GET_NET_ERROR());
		
        return sent;
    }

    // Отправляет вектор байтов (с проверкой непустоты и предела INT_MAX).
    int StreamSocket::Write(const std::vector<char>& data)
    {
        if (data.empty() || data.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
        {
            SetStreamError(SOCK_EINVAL);
            return -1;
        }
		
        return Write(data.data(), static_cast<int>(data.size()));
    }

    // Отправляет строку std::string без завершающего нулевого символа '\0'.
    int StreamSocket::WriteString(const std::string& text)
    {
        if (text.empty() || text.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
        {
            SetStreamError(SOCK_EINVAL);
            return -1;
        }
		
        return Write(text.data(), static_cast<int>(text.size()));
    }

    // Гарантированно отправляет все size байт в цикле (защита от частичной отправки в TCP).
    bool StreamSocket::WriteAll(const char* buffer, int size)
    {
        if (_socket.IsClosed())
        {
            SetStreamError(SOCK_EBADF);
            return false;
        }
		
        if (buffer == nullptr || size <= 0)
        {
            SetStreamError(SOCK_EINVAL);
            return false;
        }

        if (!_socket.SendAll(buffer, size))
        {
            SetStreamError(GET_NET_ERROR());
            return false;
        }
        return true;
    }

    // Отправляет ByteBuffer как бинарный пакет: 4 байта длины (Big-Endian) + полезная нагрузка.
    bool StreamSocket::WritePacket(const ByteBuffer& buffer)
    {
        if (_socket.IsClosed())
        {
            SetStreamError(SOCK_EBADF);
            return false;
        }
		
        if (buffer.Size() > static_cast<size_t>(std::numeric_limits<int>::max()))
        {
            SetStreamError(SOCK_EMSGSIZE);
            return false;
        }

        // отправляем 4-байтный заголовок длины в сетевом порядке байтов (Big-Endian).
        const uint32_t netSize = htonl(static_cast<uint32_t>(buffer.Size()));
        if (!WriteAll(reinterpret_cast<const char*>(&netSize), sizeof(netSize)))
            return false;

        // если пакет не пустой — гарантированно отправляем всё тело пакета.
        if (!buffer.Empty())
        {
            if (!WriteAll(reinterpret_cast<const char*>(buffer.Data()), static_cast<int>(buffer.Size())))
                return false;
        }
		
        return true;
    }

    // Принимает бинарный пакет в ByteBuffer с ограничением maxPacketSize (защита от DoS).
    bool StreamSocket::ReadPacket(ByteBuffer& buffer, uint32_t maxPacketSize)
    {
        buffer.Clear();

        if (_socket.IsClosed() && BufferedBytes() == 0)
        {
            SetStreamError(SOCK_EBADF);
            return false;
        }

        // вычитываем ровно 4 байта заголовка длины.
        uint32_t netSize = 0;
        if (!ReadExactly(reinterpret_cast<char*>(&netSize), sizeof(netSize)))
            return false;

        // переводим длину из Big-Endian и проверяем лимит.
        const uint32_t payloadSize = ntohl(netSize);
        if (payloadSize > maxPacketSize || payloadSize > static_cast<uint32_t>(std::numeric_limits<int>::max()))
        {
            SetStreamError(SOCK_EMSGSIZE);
            return false;
        }

        // Пустой пакет (0 байт полезной нагрузки) допустим.
        if (payloadSize == 0)
            return true;

        // расширяем буфер и гарантированно дочитываем всё тело пакета.
        buffer.Resize(payloadSize);
        if (!ReadExactly(reinterpret_cast<char*>(buffer.Data()), static_cast<int>(payloadSize)))
        {
            buffer.Clear();
            return false;
        }

        // перематываем курсор чтения в начало, чтобы сразу можно было делать `buffer >> ...`.
        buffer.Rewind();
        return true;
    }

    // Читает до size байт: сначала отдаёт данные из _readBuf (если есть), иначе читает напрямую из сокета.
    int StreamSocket::Read(char* buffer, int size)
    {
        if (buffer == nullptr || size <= 0)
        {
            SetStreamError(SOCK_EINVAL);
            return -1;
        }

        // Если в _readBuf уже лежат предчитанные байты — отдаём их без системного вызова.
        if (_readBuf.Remaining() > 0)
        {
            const size_t toCopy = std::min(static_cast<size_t>(size), _readBuf.Remaining());
            _readBuf.ReadBytes(reinterpret_cast<uint8_t*>(buffer), toCopy);
            return static_cast<int>(toCopy);
        }

        if (_socket.IsClosed())
        {
            SetStreamError(SOCK_EBADF);
            return -1;
        }

        int n = 0;
        while (true)
        {
            n = _socket.Recv(buffer, size);
            if (n < 0 && GET_NET_ERROR() == SOCK_EINTR)
                continue;
            break;
        }

        // 0 = штатное закрытие соединения собеседником (EOF); -1 = сетевая ошибка.
        if (n < 0)
            SetStreamError(GET_NET_ERROR());
        return n;
    }

    // Гарантированно вычитывает ровно size байт: сначала из _readBuf, затем остаток из сокета в цикле.
    bool StreamSocket::ReadExactly(char* buffer, int size)
    {
        if (buffer == nullptr || size <= 0)
        {
            SetStreamError(SOCK_EINVAL);
            return false;
        }

        int totalReceived = 0;

        // забираем все доступные байты из локального ByteBuffer предчтения.
        if (_readBuf.Remaining() > 0)
        {
            const size_t toCopy = std::min(static_cast<size_t>(size), _readBuf.Remaining());
            _readBuf.ReadBytes(reinterpret_cast<uint8_t*>(buffer), toCopy);
            totalReceived = static_cast<int>(toCopy);
        }

        if (totalReceived == size)
            return true;

        if (_socket.IsClosed())
        {
            SetStreamError(SOCK_EBADF);
            return false;
        }

        // добираем оставшийся объём из сокета в цикле с защитой от фрагментации и EINTR.
        while (totalReceived < size)
        {
            int bytesReceived = _socket.Recv(buffer + totalReceived, size - totalReceived);
            if (bytesReceived < 0)
            {
                if (GET_NET_ERROR() == SOCK_EINTR)
                    continue;
                SetStreamError(GET_NET_ERROR());
                return false;
            }
            if (bytesReceived == 0)
            {
                SetStreamError(SOCK_ENOTCONN);
                return false;
            }
            totalReceived += bytesReceived;
        }

        return true;
    }

    // Гарантированно вычитывает ровно size байт и возвращает их в векторе.
    std::vector<char> StreamSocket::ReadBytes(int size)
    {
        if (size <= 0)
        {
            SetStreamError(SOCK_EINVAL);
            return {};
        }

        std::vector<char> buffer(static_cast<size_t>(size));
        if (!ReadExactly(buffer.data(), size))
            buffer.clear();
		
        return buffer;
    }

    // Гарантированно вычитывает ровно size байт и возвращает их как std::string (без лишнего копирования).
    std::string StreamSocket::ReadString(int size)
    {
        if (size <= 0)
        {
            SetStreamError(SOCK_EINVAL);
            return "";
        }

        std::string result(static_cast<size_t>(size), '\0');
        if (!ReadExactly(result.data(), size))
            return "";
		
        return result;
    }

    // Буферизованно читает строку до символа delimiter (обычно '\n'), отбрасывая '\r' и проверяя maxLength.
    std::string StreamSocket::ReadLine(char delimiter, size_t maxLength)
    {
        if (_socket.IsClosed() && BufferedBytes() == 0)
        {
            SetStreamError(SOCK_EBADF);
            return "";
        }

        std::string line;
        char ch = 0;
        bool connectionClosed = false;

        while (true)
        {
            int bytesRead = ReadNextByte(ch);
            if (bytesRead < 0)
                return "";
			
            if (bytesRead == 0)
            {
                connectionClosed = true;
                break;
            }

            if (ch == delimiter)
                break;

            // Отбрасываем '\r', чтобы строки с CRLF ("\r\n") читались одинаково на Windows и Linux.
            if (ch != '\r')
            {
                if (maxLength > 0 && line.size() >= maxLength)
                {
                    SetStreamError(SOCK_EMSGSIZE);
                    return "";
                }
                line.push_back(ch);
            }
        }

        // Если соединение закрылось и до EOF не было прочитано ни одного символа — фиксируем ошибку.
        if (connectionClosed && line.empty())
        {
            SetStreamError(SOCK_ENOTCONN);
            return "";
        }

        return line;
    }

    // Буферизованно читает поток до появления многосимвольного разделителя delimiter (например "\r\n\r\n").
    std::string StreamSocket::ReadUntil(const std::string& delimiter, size_t maxLength)
    {
        if (delimiter.empty())
        {
            SetStreamError(SOCK_EINVAL);
            return "";
        }
		
        if (_socket.IsClosed() && BufferedBytes() == 0)
        {
            SetStreamError(SOCK_EBADF);
            return "";
        }

        std::string buffer;
        char ch = 0;
        const size_t delimLen = delimiter.length();

        while (true)
        {
            int bytesRead = ReadNextByte(ch);
            if (bytesRead < 0)
                return "";
			
            if (bytesRead == 0)
            {
                // Разделитель так и не встретился до закрытия соединения — фиксируем обрыв.
                SetStreamError(SOCK_ENOTCONN);
                return buffer;
            }

            buffer.push_back(ch);

            // Проверяем совпадение хвоста накопителя с искомым разделителем.
            if (buffer.length() >= delimLen && buffer.compare(buffer.length() - delimLen, delimLen, delimiter) == 0)
            {
                buffer.erase(buffer.length() - delimLen);
                return buffer;
            }

            // Проверяем ограничение maxLength без риска целочисленного переполнения (maxLength + delimLen).
            if (maxLength > 0 && buffer.length() >= delimLen && (buffer.length() - delimLen) >= maxLength)
            {
                SetStreamError(SOCK_EMSGSIZE);
                return "";
            }
        }
    }

    // Возвращает общее число байтов, готовых к немедленному чтению (в _readBuf + в буфере ядра ОС).
    int StreamSocket::Available() const
    {
        constexpr size_t maxInt = static_cast<size_t>(std::numeric_limits<int>::max());
        const size_t buffered   = BufferedBytes();

        if (_socket.IsClosed())
            return (buffered > 0) ? static_cast<int>(std::min(buffered, maxInt)) : -1;

        int osAvail = _socket.Available();
        if (osAvail < 0)
            return (buffered > 0) ? static_cast<int>(std::min(buffered, maxInt)) : -1;

        // Защита от переполнения int при сложении локального и системного буферов.
        const size_t total = (buffered > maxInt - static_cast<size_t>(osAvail)) ? maxInt : (buffered + static_cast<size_t>(osAvail));
        return static_cast<int>(total);
    }

    // Заглушка совместимости с потоками (локального буфера записи нет).
    void StreamSocket::Flush() noexcept { }

    // Закрывает внутреннее соединение и очищает локальный буфер предчтения _readBuf.
    bool StreamSocket::Close(int timeout) noexcept
    {
        _readBuf.Clear();
        return _socket.Close(timeout);
    }

    // Полностью освобождает внутренний сокет и очищает локальный буфер предчтения _readBuf.
    bool StreamSocket::Dispose() noexcept
    {
        _readBuf.Clear();
        return _socket.Dispose();
    }

    // Ожидает появления данных для чтения не дольше timeoutMs миллисекунд.
    bool StreamSocket::WaitForData(int timeoutMs) const noexcept
    {
        if (BufferedBytes() > 0)
            return true;
		
        if (_socket.IsClosed())
            return false;
		
        return _socket.PollRead(timeoutMs);
    }

    // Форматирует строку в стиле printf в классической C-локали ('.' для чисел) и гарантированно отправляет её через WriteAll().
    int StreamSocket::WriteFormat(const char* format, ...)
    {
        if (_socket.IsClosed())
        {
            SetStreamError(SOCK_EBADF);
            return -1;
        }

        if (format == nullptr)
        {
            SetStreamError(SOCK_EINVAL);
            return -1;
        }

        va_list args;
        va_start(args, format);

        va_list argsCopy;
        va_copy(argsCopy, args);

#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
        const _locale_t cLoc = NumericCLocale::Instance().Handle();

        // Первый проход: вычисляем точную длину строки в числовой C-локали (точка '.' независимо от setlocale).
        const int size = (cLoc != nullptr) ? ::_vscprintf_l(format, cLoc, argsCopy) : std::vsnprintf(nullptr, 0, format, argsCopy);
        va_end(argsCopy);

        if (size < 0)
        {
            va_end(args);
            SetStreamError(SOCK_EINVAL);
            return -1;
        }

        if (size == 0)
        {
            va_end(args);
            return 0;
        }

        // Выделяем буфер ровно под результат + '\0' и форматируем текст в той же C-локали.
        std::vector<char> buffer(static_cast<size_t>(size) + 1U, '\0');
        const int written = (cLoc != nullptr) ? ::_vsnprintf_l(buffer.data(), buffer.size(), format, cLoc, args) : std::vsnprintf(buffer.data(), buffer.size(), format, args);
        va_end(args);

        if (written < 0)
        {
            SetStreamError(SOCK_EINVAL);
            return -1;
        }
#else
        // На Linux временно переключаем локаль только текущего потока (безопасно при исключении std::bad_alloc).
        ScopedThreadCLocale localeGuard(NumericCLocale::Instance().Handle());

        const int size = std::vsnprintf(nullptr, 0, format, argsCopy);
        va_end(argsCopy);

        if (size < 0)
        {
            va_end(args);
            SetStreamError(SOCK_EINVAL);
            return -1;
        }
        if (size == 0)
        {
            va_end(args);
            return 0;
        }

        std::vector<char> buffer(static_cast<size_t>(size) + 1U, '\0');
        const int written = std::vsnprintf(buffer.data(), buffer.size(), format, args);
        va_end(args);

        if (written < 0)
        {
            SetStreamError(SOCK_EINVAL);
            return -1;
        }
#endif

        // Гарантированно отправляем всю отформатированную строку (без завершающего '\0').
        return WriteAll(buffer.data(), size) ? size : -1;
    }


    ///////////////////////////////////////////////////////////////////////
    ///            ПОТОКОВЫЕ ОПЕРАТОРЫ ЗАПИСИ (<<)                      ///
    ///////////////////////////////////////////////////////////////////////
    StreamSocket& StreamSocket::operator<<(const std::string& text)
    {
        if (!_good || text.empty())
            return *this;
		
        if (text.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
        {
            SetStreamError(SOCK_EMSGSIZE);
            return *this;
        }
		
        WriteAll(text.data(), static_cast<int>(text.size()));
        return *this;
    }

    StreamSocket& StreamSocket::operator<<(const char* text)
    {
        if (!_good)
            return *this;
		
        if (text == nullptr)
        {
            SetStreamError(SOCK_EINVAL);
            return *this;
        }
		
        const size_t len = std::strlen(text);
        if (len == 0)
            return *this;
		
        if (len > static_cast<size_t>(std::numeric_limits<int>::max()))
        {
            SetStreamError(SOCK_EMSGSIZE);
            return *this;
        }
		
        WriteAll(text, static_cast<int>(len));
        return *this;
    }

    StreamSocket& StreamSocket::operator<<(bool value)
    {
        if (!_good)
            return *this;
		
        const char* s = value ? "true\n" : "false\n";
        WriteAll(s, value ? 5 : 6);
        return *this;
    }

    StreamSocket& StreamSocket::operator<<(char value)
    {
        if (!_good)
            return *this;
		
        WriteAll(&value, 1);
        return *this;
    }

    StreamSocket& StreamSocket::operator<<(int8_t value)   { if (!_good) return *this; return (*this << (std::to_string(static_cast<int>(value)) + "\n")); }
    StreamSocket& StreamSocket::operator<<(int16_t value)  { if (!_good) return *this; return (*this << (std::to_string(value) + "\n")); }
    StreamSocket& StreamSocket::operator<<(int32_t value)  { if (!_good) return *this; return (*this << (std::to_string(value) + "\n")); }
    StreamSocket& StreamSocket::operator<<(int64_t value)  { if (!_good) return *this; return (*this << (std::to_string(value) + "\n")); }
    StreamSocket& StreamSocket::operator<<(uint8_t value)  { if (!_good) return *this; return (*this << (std::to_string(static_cast<unsigned int>(value)) + "\n")); }
    StreamSocket& StreamSocket::operator<<(uint16_t value) { if (!_good) return *this; return (*this << (std::to_string(value) + "\n")); }
    StreamSocket& StreamSocket::operator<<(uint32_t value) { if (!_good) return *this; return (*this << (std::to_string(value) + "\n")); }
    StreamSocket& StreamSocket::operator<<(uint64_t value) { if (!_good) return *this; return (*this << (std::to_string(value) + "\n")); }
    StreamSocket& StreamSocket::operator<<(float value)    { if (!_good) return *this; return (*this << hpp::FormatFloatLine(value, 9)); }
    StreamSocket& StreamSocket::operator<<(double value)   { if (!_good) return *this; return (*this << hpp::FormatFloatLine(value, 17)); }

    StreamSocket& StreamSocket::operator<<(const ByteBuffer& buffer)
    {
        if (!_good)
            return *this;
		
        WritePacket(buffer);
        return *this;
    }


    ///////////////////////////////////////////////////////////////////////
    ///            ПОТОКОВЫЕ ОПЕРАТОРЫ ЧТЕНИЯ (>>)                      ///
    ///////////////////////////////////////////////////////////////////////
    StreamSocket& StreamSocket::operator>>(std::string& text)
    {
        if (!_good)
            return *this;
		
        text = ReadLine('\n');
        return *this;
    }

    StreamSocket& StreamSocket::operator>>(bool& value)
    {
        if (!_good)
            return *this;

        std::string line = ReadLine('\n');
        if (line.empty())
        {
            if (_good)
                SetStreamError(SOCK_EINVAL);
            return *this;
        }

        if (line == "true" || line == "1")
            value = true;
        else if (line == "false" || line == "0")
            value = false;
        else
            SetStreamError(SOCK_EINVAL);

        return *this;
    }

    StreamSocket& StreamSocket::operator>>(char& value)
    {
        if (!_good)
            return *this;

        char ch = 0;
        int res = Read(&ch, 1);
        if (res == 1)
        {
            value = ch;
        }
        else
        {
            value = 0;
            if (res == 0)
                SetStreamError(SOCK_ENOTCONN);
        }
        return *this;
    }

    StreamSocket& StreamSocket::operator>>(int8_t& value)
    {
        if (!_good) 
            return *this;

        std::string line = ReadLine('\n');
        if (line.empty()) 
        { 
            if (_good) 
                SetStreamError(SOCK_EINVAL); 
            return *this; 
        }

        if (!hpp::ParseSignedInt(line, value)) 
            SetStreamError(SOCK_EINVAL);

        return *this;
    }

    StreamSocket& StreamSocket::operator>>(int16_t& value)
    {
        if (!_good) 
            return *this;

        std::string line = ReadLine('\n');
        if (line.empty()) 
        { 
            if (_good) 
                SetStreamError(SOCK_EINVAL); 
            return *this; 
        }

        if (!hpp::ParseSignedInt(line, value)) 
            SetStreamError(SOCK_EINVAL);

        return *this;
    }

    StreamSocket& StreamSocket::operator>>(int32_t& value)
    {
        if (!_good) 
            return *this;

        std::string line = ReadLine('\n');
        if (line.empty()) 
        { 
            if (_good) 
                SetStreamError(SOCK_EINVAL); 
            return *this; 
        }

        if (!hpp::ParseSignedInt(line, value)) 
            SetStreamError(SOCK_EINVAL);

        return *this;
    }

    StreamSocket& StreamSocket::operator>>(int64_t& value)
    {
        if (!_good) 
            return *this;

        std::string line = ReadLine('\n');
        if (line.empty()) 
        { 
            if (_good) 
                SetStreamError(SOCK_EINVAL); 
            return *this; 
        }

        if (!hpp::ParseSignedInt(line, value)) 
            SetStreamError(SOCK_EINVAL);

        return *this;
    }

    StreamSocket& StreamSocket::operator>>(uint8_t& value)
    {
        if (!_good) return *this;
        std::string line = ReadLine('\n');
        if (line.empty()) { if (_good) SetStreamError(SOCK_EINVAL); return *this; }
        if (!hpp::ParseUnsignedInt(line, value)) SetStreamError(SOCK_EINVAL);
        return *this;
    }

    StreamSocket& StreamSocket::operator>>(uint16_t& value)
    {
        if (!_good) 
            return *this;

        std::string line = ReadLine('\n');
        if (line.empty()) 
        { 
            if (_good) 
                SetStreamError(SOCK_EINVAL); 
            return *this; 
        }

        if (!hpp::ParseUnsignedInt(line, value)) 
            SetStreamError(SOCK_EINVAL);

        return *this;
    }

    StreamSocket& StreamSocket::operator>>(uint32_t& value)
    {
        if (!_good) 
            return *this;

        std::string line = ReadLine('\n');
        if (line.empty()) 
        { 
            if (_good) 
                SetStreamError(SOCK_EINVAL); 
            return *this; 
        }

        if (!hpp::ParseUnsignedInt(line, value)) 
            SetStreamError(SOCK_EINVAL);

        return *this;
    }

    StreamSocket& StreamSocket::operator>>(uint64_t& value)
    {
        if (!_good) 
            return *this;

        std::string line = ReadLine('\n');
        if (line.empty()) 
        { 
            if (_good) 
                SetStreamError(SOCK_EINVAL); 
            return *this; 
        }

        if (!hpp::ParseUnsignedInt(line, value)) 
            SetStreamError(SOCK_EINVAL);

        return *this;
    }

    StreamSocket& StreamSocket::operator>>(float& value)
    {
        if (!_good) 
            return *this;

        std::string line = ReadLine('\n');
        if (line.empty()) 
        { 
            if (_good) 
                SetStreamError(SOCK_EINVAL); 
            return *this; 
        }

        if (!hpp::ParseFiniteFloat(line, value)) 
            SetStreamError(SOCK_EINVAL);

        return *this;
    }

    StreamSocket& StreamSocket::operator>>(double& value)
    {
        if (!_good) 
			return *this;
		
        std::string line = ReadLine('\n');
        if (line.empty()) 
		{ 
            if (_good) 
                SetStreamError(SOCK_EINVAL); 
            return *this; 
        }

        if (!hpp::ParseFiniteFloat(line, value)) 
            SetStreamError(SOCK_EINVAL);

        return *this;
    }

    StreamSocket& StreamSocket::operator>>(ByteBuffer& buffer)
    {
        if (!_good)
            return *this;
		
        ReadPacket(buffer);
        return *this;
    }
}
