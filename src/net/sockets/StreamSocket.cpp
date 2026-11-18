#include "StreamSocket.h"




namespace other::net::sockets
{
    StreamSocket::StreamSocket(Socket& socket) : _socket(std::move(socket))
    {
    }

    StreamSocket::~StreamSocket()
    {
        Dispose();
    }

    // Таймауты записи привязаны к Send-тайм-аутам базового сокета
    int StreamSocket::GetWriteTimeout() const
    {
        if (!CanWrite())
            return -1;

        return _socket.GetSendTimeout();
    }

    int StreamSocket::SetWriteTimeout(int timeout)
    {
        if (!CanWrite())
            return -1;

        return _socket.SetSendTimeout(timeout);
    }

    // Таймауты чтения привязаны к Recv-тайм-аутам базового сокета
    int StreamSocket::GetReadTimeout() const
    {
        if (!CanRead())
            return -1;

        return _socket.GetRecvTimeout();
    }

    int StreamSocket::SetReadTimeout(int timeout)
    {
        if (!CanRead())
            return -1;

        return _socket.SetRecvTimeout(timeout);
    }

    bool StreamSocket::CanTimeout() const { return true; }
    bool StreamSocket::CanWrite()   const { return CanRead(); }
    bool StreamSocket::CanSeek()    const { return false; }
    bool StreamSocket::CanRead()    const { return _socket.IsCreated() && _socket.IsConnected(); }

    Socket& StreamSocket::SockInternal() { return _socket; }
    const Socket& StreamSocket::SockInternal() const { return _socket; }

    int StreamSocket::Write(const char* buffer, int size)
    {
        if (!CanWrite())
        {
            SET_NET_ERROR(SOCK_ENOTCONN);
            return -1;
        }

        if (buffer == nullptr || size <= 0)
        {
            SET_NET_ERROR(SOCK_EINVAL);
            return -1;
        }

        return _socket.Send(buffer, size);
    }

    int StreamSocket::Write(const std::vector<char>& data)
    {
        if (data.empty())
        {
            SET_NET_ERROR(SOCK_EINVAL);
            return -1;
        }
        return Write(data.data(), static_cast<int>(data.size()));
    }

    int StreamSocket::WriteString(const std::string& text)
    {
        if (text.empty())
        {
            SET_NET_ERROR(SOCK_EINVAL);
            return -1;
        }
        return Write(text.data(), static_cast<int>(text.size()));
    }

    bool StreamSocket::WriteAll(const char* buffer, int size)
    {
        if (!CanWrite())
        {
            SET_NET_ERROR(SOCK_ENOTCONN);
            return false;
        }

        if (buffer == nullptr || size <= 0)
        {
            SET_NET_ERROR(SOCK_EINVAL);
            return false;
        }

        int totalSent = 0;
        while (totalSent < size)
        {
            int sent = _socket.Send(buffer + totalSent, size - totalSent);
            if (sent <= 0)
            {
                return false;
            }
            totalSent += sent;
        }

        return true;
    }

    int StreamSocket::Read(char* buffer, int size)
    {
        if (!CanRead())
        {
            SET_NET_ERROR(SOCK_ENOTCONN);
            return -1;
        }

        if (buffer == nullptr || size <= 0)
        {
            SET_NET_ERROR(SOCK_EINVAL);
            return -1;
        }

        return _socket.Recv(buffer, size);
    }

    bool StreamSocket::ReadExactly(char* buffer, int size)
    {
        if (!CanRead())
        {
            SET_NET_ERROR(SOCK_ENOTCONN);
            return false;
        }

        if (buffer == nullptr || size <= 0)
        {
            SET_NET_ERROR(SOCK_EINVAL);
            return false;
        }

        int totalReceived = 0;
        while (totalReceived < size)
        {
            int bytesReceived = _socket.Recv(buffer + totalReceived, size - totalReceived);
            if (bytesReceived <= 0)
            {
                return false;
            }
            totalReceived += bytesReceived;
        }

        return true;
    }

    std::vector<char> StreamSocket::ReadBytes(int size)
    {
        std::vector<char> buffer(size);
        if (size <= 0 || !ReadExactly(buffer.data(), size))
        {
            buffer.clear();
        }
        return buffer;
    }

    std::string StreamSocket::ReadString(int size)
    {
        if (size <= 0 || !CanRead())
        {
            return "";
        }

        std::vector<char> buffer(size);
        if (ReadExactly(buffer.data(), size))
        {
            return std::string(buffer.begin(), buffer.end());
        }

        return "";
    }

    std::string StreamSocket::ReadLine(char delimiter)
    {
        if (!CanRead())
        {
            SET_NET_ERROR(SOCK_ENOTCONN);
            return "";
        }

        std::string line;
        char ch = 0;
        bool connectionClosed = false;

        while (true)
        {
            int bytesRead = _socket.Recv(&ch, 1);
            if (bytesRead < 0)
            {
                return "";
            }
            if (bytesRead == 0)
            {
                connectionClosed = true;
                break;
            }

            if (ch == delimiter)
            {
                break;
            }

            if (ch != '\r')
            {
                line.push_back(ch);
            }
        }

        if (connectionClosed && line.empty())
        {
            SET_NET_ERROR(SOCK_ENOTCONN);
            return "";
        }

        return line;
    }

    std::string StreamSocket::ReadUntil(const std::string& delimiter)
    {
        if (!CanRead() || delimiter.empty())
        {
            SET_NET_ERROR(SOCK_EINVAL);
            return "";
        }

        std::string buffer;
        char ch = 0;
        size_t delimLen = delimiter.length();

        while (true)
        {
            int bytesRead = _socket.Recv(&ch, 1);
            if (bytesRead <= 0)
            {
                if (bytesRead == 0)
                {
                    SET_NET_ERROR(SOCK_ENOTCONN);
                }
                break;
            }

            buffer.push_back(ch);

            if (buffer.length() >= delimLen)
            {
                if (buffer.compare(buffer.length() - delimLen, delimLen, delimiter) == 0)
                {
                    buffer.erase(buffer.length() - delimLen);
                    break;
                }
            }
        }

        return buffer;
    }

    int StreamSocket::Available() const
    {
        return _socket.Available();
    }

    void StreamSocket::Flush()
    {
        // Зарезервировано.
    }

    bool StreamSocket::Close(int timeout)
    {
        return _socket.Close(timeout);
    }

    bool StreamSocket::Dispose()
    {
        return _socket.Dispose();
    }

    bool StreamSocket::WaitForData(int timeoutMs) const
    {
        if (!CanRead())
        {
            return false;
        }

        if (Available() > 0)
        {
            return true;
        }

        return _socket.PollRead(timeoutMs);
    }

    int StreamSocket::WriteFormat(const char* format, ...)
    {
        if (!CanWrite() || format == nullptr)
        {
            SET_NET_ERROR(SOCK_EINVAL);
            return -1;
        }

        va_list args;
        va_start(args, format);

        va_list argsCopy;
        va_copy(argsCopy, args);
        int size = std::vsnprintf(nullptr, 0, format, argsCopy);
        va_end(argsCopy);

        if (size < 0)
        {
            va_end(args);
            SET_NET_ERROR(SOCK_EINVAL);
            return -1;
        }

        std::vector<char> buffer(static_cast<size_t>(size) + 1);
        std::vsnprintf(buffer.data(), buffer.size(), format, args);
        va_end(args);

        return Write(buffer.data(), size);
    }

    //////////////////////////////////////////////
    ///     ПОТОКОВЫЕ ОПЕРАТОРЫ ЗАПИСИ (<<)    ///
    //////////////////////////////////////////////
    StreamSocket& StreamSocket::operator<<(const std::string& text)
    {
        WriteString(text);
        return *this;
    }

    StreamSocket& StreamSocket::operator<<(const char* text)
    {
        if (text != nullptr)
        {
            WriteString(text);
        }
        else
        {
            SET_NET_ERROR(SOCK_EINVAL);
        }
        return *this;
    }

    StreamSocket& StreamSocket::operator<<(bool value)
    {
        WriteString(value ? "true\n" : "false\n");
        return *this;
    }

    StreamSocket& StreamSocket::operator<<(char value)
    {
        Write(&value, 1);
        return *this;
    }

    StreamSocket& StreamSocket::operator<<(int8_t value)
    {
        WriteString(std::to_string(static_cast<int>(value)) + "\n");
        return *this;
    }

    StreamSocket& StreamSocket::operator<<(int16_t value)
    {
        WriteString(std::to_string(value) + "\n");
        return *this;
    }

    StreamSocket& StreamSocket::operator<<(int32_t value)
    {
        WriteString(std::to_string(value) + "\n");
        return *this;
    }

    StreamSocket& StreamSocket::operator<<(int64_t value)
    {
        WriteString(std::to_string(value) + "\n");
        return *this;
    }

    StreamSocket& StreamSocket::operator<<(uint8_t value)
    {
        WriteString(std::to_string(static_cast<unsigned int>(value)) + "\n");
        return *this;
    }

    StreamSocket& StreamSocket::operator<<(uint16_t value)
    {
        WriteString(std::to_string(value) + "\n");
        return *this;
    }

    StreamSocket& StreamSocket::operator<<(uint32_t value)
    {
        WriteString(std::to_string(value) + "\n");
        return *this;
    }

    StreamSocket& StreamSocket::operator<<(uint64_t value)
    {
        WriteString(std::to_string(value) + "\n");
        return *this;
    }

    StreamSocket& StreamSocket::operator<<(float value)
    {
        WriteString(std::to_string(value) + "\n");
        return *this;
    }

    StreamSocket& StreamSocket::operator<<(double value)
    {
        WriteString(std::to_string(value) + "\n");
        return *this;
    }

    //////////////////////////////////////////////
    ///     ПОТОКОВЫЕ ОПЕРАТОРЫ ЧТЕНИЯ (>>)    ///
    //////////////////////////////////////////////
    StreamSocket& StreamSocket::operator>>(std::string& text)
    {
        text = ReadLine('\n');
        if (text.empty())
        {
            // Ошибка уже устанавливается внутри ReadLine если соединение разорвано
        }
        return *this;
    }

    StreamSocket& StreamSocket::operator>>(bool& value)
    {
        std::string line = ReadLine('\n');
        if (line.empty())
            return *this;

        if (line == "true" || line == "1")
        {
            value = true;
        }
        else if (line == "false" || line == "0")
        {
            value = false;
        }
        else
        {
            SET_NET_ERROR(SOCK_EINVAL);
        }

        return *this;
    }

    StreamSocket& StreamSocket::operator>>(char& value)
    {
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
                SET_NET_ERROR(SOCK_ENOTCONN);
            else
                SET_NET_ERROR(SOCK_EINVAL);
        }
        return *this;
    }

    StreamSocket& StreamSocket::operator>>(int8_t& value)
    {
        std::string line = ReadLine('\n');
        if (line.empty()) return *this;

        try
        {
            size_t idx = 0;
            int parsed = std::stoi(line, &idx);

            if (idx == line.length() && parsed >= std::numeric_limits<int8_t>::min() && parsed <= std::numeric_limits<int8_t>::max())
            {
                value = static_cast<int8_t>(parsed);
            }
            else
            {
                SET_NET_ERROR(SOCK_EINVAL);
            }
        }
        catch (...)
        {
            SET_NET_ERROR(SOCK_EINVAL);
        }

        return *this;
    }

    StreamSocket& StreamSocket::operator>>(int16_t& value)
    {
        std::string line = ReadLine('\n');
        if (line.empty()) return *this;

        try
        {
            size_t idx = 0;
            int parsed = std::stoi(line, &idx);

            if (idx == line.length() && parsed >= std::numeric_limits<int16_t>::min() && parsed <= std::numeric_limits<int16_t>::max())
            {
                value = static_cast<int16_t>(parsed);
            }
            else
            {
                SET_NET_ERROR(SOCK_EINVAL);
            }
        }
        catch (...)
        {
            SET_NET_ERROR(SOCK_EINVAL);
        }

        return *this;
    }

    StreamSocket& StreamSocket::operator>>(int32_t& value)
    {
        std::string line = ReadLine('\n');
        if (line.empty()) return *this;

        try
        {
            size_t idx = 0;
            long parsed = std::stol(line, &idx);

            if (idx == line.length() && parsed >= std::numeric_limits<int32_t>::min() && parsed <= std::numeric_limits<int32_t>::max())
            {
                value = static_cast<int32_t>(parsed);
            }
            else
            {
                SET_NET_ERROR(SOCK_EINVAL);
            }
        }
        catch (...)
        {
            SET_NET_ERROR(SOCK_EINVAL);
        }

        return *this;
    }

    StreamSocket& StreamSocket::operator>>(int64_t& value)
    {
        std::string line = ReadLine('\n');
        if (line.empty()) return *this;

        try
        {
            size_t idx = 0;
            int64_t parsed = std::stoll(line, &idx);

            if (idx == line.length() && parsed >= std::numeric_limits<int64_t>::min() && parsed <= std::numeric_limits<int64_t>::max())
            {
                value = parsed;
            }
            else
            {
                SET_NET_ERROR(SOCK_EINVAL);
            }
        }
        catch (...)
        {
            SET_NET_ERROR(SOCK_EINVAL);
        }

        return *this;
    }

    StreamSocket& StreamSocket::operator>>(uint8_t& value)
    {
        std::string line = ReadLine('\n');
        if (line.empty()) return *this;

        try
        {
            size_t idx = 0;
            unsigned long parsed = std::stoul(line, &idx);

            if (idx == line.length() && parsed <= static_cast<unsigned long>(std::numeric_limits<uint8_t>::max()))
            {
                value = static_cast<uint8_t>(parsed);
            }
            else
            {
                SET_NET_ERROR(SOCK_EINVAL);
            }
        }
        catch (...)
        {
            SET_NET_ERROR(SOCK_EINVAL);
        }

        return *this;
    }

    StreamSocket& StreamSocket::operator>>(uint16_t& value)
    {
        std::string line = ReadLine('\n');
        if (line.empty()) return *this;

        try
        {
            size_t idx = 0;
            unsigned long parsed = std::stoul(line, &idx);

            if (idx == line.length() && parsed <= static_cast<unsigned long>(std::numeric_limits<uint16_t>::max()))
            {
                value = static_cast<uint16_t>(parsed);
            }
            else
            {
                SET_NET_ERROR(SOCK_EINVAL);
            }
        }
        catch (...)
        {
            SET_NET_ERROR(SOCK_EINVAL);
        }

        return *this;
    }

    StreamSocket& StreamSocket::operator>>(uint32_t& value)
    {
        std::string line = ReadLine('\n');
        if (line.empty()) return *this;

        try
        {
            size_t idx = 0;
            unsigned long parsed = std::stoul(line, &idx);

            if (idx == line.length() && parsed <= static_cast<unsigned long>(std::numeric_limits<uint32_t>::max()))
            {
                value = static_cast<uint32_t>(parsed);
            }
            else
            {
                SET_NET_ERROR(SOCK_EINVAL);
            }
        }
        catch (...)
        {
            SET_NET_ERROR(SOCK_EINVAL);
        }

        return *this;
    }

    StreamSocket& StreamSocket::operator>>(uint64_t& value)
    {
        std::string line = ReadLine('\n');
        if (line.empty()) return *this;

        try
        {
            size_t idx = 0;
            uint64_t parsed = std::stoull(line, &idx);

            if (idx == line.length())
            {
                value = parsed;
            }
            else
            {
                SET_NET_ERROR(SOCK_EINVAL);
            }
        }
        catch (...)
        {
            SET_NET_ERROR(SOCK_EINVAL);
        }

        return *this;
    }

    StreamSocket& StreamSocket::operator>>(float& value)
    {
        std::string line = ReadLine('\n');
        if (line.empty()) return *this;

        try
        {
            size_t idx = 0;
            float parsed = std::stof(line, &idx);

            if (idx == line.length() && parsed >= std::numeric_limits<float>::lowest() && parsed <= std::numeric_limits<float>::max())
            {
                value = parsed;
            }
            else
            {
                SET_NET_ERROR(SOCK_EINVAL);
            }
        }
        catch (...)
        {
            SET_NET_ERROR(SOCK_EINVAL);
        }

        return *this;
    }

    StreamSocket& StreamSocket::operator>>(double& value)
    {
        std::string line = ReadLine('\n');
        if (line.empty()) return *this;

        try
        {
            size_t idx = 0;
            double parsed = std::stod(line, &idx);

            if (idx == line.length() &&
                parsed >= std::numeric_limits<double>::lowest() && parsed <= std::numeric_limits<double>::max())
            {
                value = parsed;
            }
            else
            {
                SET_NET_ERROR(SOCK_EINVAL);
            }
        }
        catch (...)
        {
            SET_NET_ERROR(SOCK_EINVAL);
        }

        return *this;
    }
}
