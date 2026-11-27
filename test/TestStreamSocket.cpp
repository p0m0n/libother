#include "StreamSocket.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <vector>


namespace
{
    using namespace other::net::sockets;

    /**
     * @class TestRunner
     * @brief Вспомогательный регистратор проверок с подробным логированием успехов и ошибок в консоль.
     *
     * Подсчитывает общее число пройденных и проваленных утверждений и сценариев, при провале
     * печатает расшифровку системного кода ошибки через `SocketErrorToString()` и формирует итоговую сводку.
     */
    class TestRunner
    {
    private:
        size_t      _totalPassed = 0; ///< Общее число успешных проверок за весь запуск.
        size_t      _totalFailed = 0; ///< Общее число проваленных проверок за весь запуск.
        size_t      _suitesPassed = 0; ///< Количество полностью пройденных сценариев.
        size_t      _suitesFailed = 0; ///< Количество сценариев с хотя бы одной ошибкой.
        size_t      _curSuiteFailed = 0; ///< Число ошибок в текущем сценарии.
        std::string _curSuiteName;       ///< Название текущего сценария.

    public:
        /**
         * @brief Открывает новый тестовый сценарий и печатает его заголовок.
         * @param [in] name Название тестового сценария.
         * @throws std::bad_alloc При нехватке памяти для копирования строки имени.
         */
        void BeginSuite(const std::string& name)
        {
            _curSuiteName = name;
            _curSuiteFailed = 0;
            std::cout << "\n========================================================================\n"
                << "[TEST] " << name << '\n'
                << "========================================================================\n";
        }

        /**
         * @brief Завершает текущий сценарий и выводит итоговый статус (`OK` или `FAILED`).
         * @note Исключения не выбрасывает.
         */
        void EndSuite()
        {
            if (_curSuiteFailed == 0)
            {
                ++_suitesPassed;
                std::cout << "[SUITE OK]     " << _curSuiteName << '\n';
            }
            else
            {
                ++_suitesFailed;
                std::cerr << "[SUITE FAILED] " << _curSuiteName
                    << " (ошибок: " << _curSuiteFailed << ")\n";
            }
        }

        /**
         * @brief Выводит информационное сообщение о промежуточном состоянии теста.
         * @param [in] message Текст сообщения.
         */
        void Info(const std::string& message) const
        {
            std::cout << "  [INFO] " << message << '\n';
        }

        /**
         * @brief Проверяет условие и выводит в консоль `[PASS]` или `[FAIL]` с опциональным кодом ошибки.
         * @param [in] condition Проверяемое утверждение.
         * @param [in] message   Описание проверки.
         * @param [in] errCode   Необязательный код ошибки (если `!= 0`, выводится при провале).
         * @return Значение `condition` (`true` при успехе, `false` при провале).
         */
        bool Check(bool condition, const std::string& message, int errCode = 0)
        {
            if (condition)
            {
                ++_totalPassed;
                std::cout << "  [PASS] " << message << '\n';
                return true;
            }

            ++_totalFailed;
            ++_curSuiteFailed;
            std::cerr << "  [FAIL] " << message;
            const int code = (errCode != 0) ? errCode : GET_NET_ERROR();
            if (code != 0)
                std::cerr << " | error=" << code << " (" << SocketErrorToString(code) << ")";
            std::cerr << '\n';
            return false;
        }

        /**
         * @brief Печатает сводную статистику по всему прогону тестов.
         * @return `0`, если все проверки успешны; `1`, если зафиксирована хотя бы одна ошибка.
         */
        int PrintSummary() const
        {
            std::cout << "\n########################################################################\n"
                << "  ИТОГОВЫЙ ОТЧЁТ ТЕСТИРОВАНИЯ StreamSocket\n"
                << "  Сценариев OK:    " << _suitesPassed << '\n'
                << "  Сценариев FAIL:  " << _suitesFailed << '\n'
                << "  Проверок PASS:   " << _totalPassed << '\n'
                << "  Проверок FAIL:   " << _totalFailed << '\n'
                << "########################################################################\n";

            return (_totalFailed == 0 && _suitesFailed == 0) ? 0 : 1;
        }
    };

    /**
     * @struct StreamPair
     * @brief Вспомогательная связка из двух подключённых друг к другу локальных `StreamSocket` (`client` <-> `server`).
     *
     * Инкапсулирует слушающий сокет и оба конца TCP-соединения на `127.0.0.1` с разумными таймаутами,
     * чтобы ни один тест не мог зависнуть навечно при сбое.
     */
    struct StreamPair
    {
        Socket       listenSock; ///< Временный слушающий сокет на `127.0.0.1`.
        StreamSocket client;     ///< Клиентский потоковый сокет.
        StreamSocket server;     ///< Серверный потоковый сокет (результат `Accept()`).
        bool         ok = false; ///< Флаг успешного создания соединённой пары.

        /**
         * @brief Создаёт локальную TCP-пару `client` <-> `server` и настраивает защитные таймауты (3 сек).
         * @param [in] timeoutMs Таймаут чтения/записи в миллисекундах (по умолчанию `3000` мс).
         * @throws std::bad_alloc При нехватке памяти внутри `Socket::Bind()` / `Socket::Connect()`.
         */
        explicit StreamPair(int timeoutMs = 3000)
            : listenSock(AF_INET, SOCK_STREAM, IPPROTO_TCP)
            , client(Socket())
            , server(Socket())
        {
            if (!listenSock.IsCreated())
                return;

            listenSock.SetReuseAddress(true);
            if (!listenSock.Bind("127.0.0.1", 0) || !listenSock.Listen(4))
                return;

            const int port = listenSock.LocalPort();
            if (port <= 0)
                return;

            Socket rawClient(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (!rawClient.IsCreated() || !rawClient.Connect("127.0.0.1", port))
                return;

            Socket rawServer = listenSock.Accept();
            if (!rawServer.IsCreated())
                return;

            // Отключаем алгоритм Нейгла для мгновенного прохождения мелких тестовых пакетов.
            rawClient.SetNoDelay(true);
            rawServer.SetNoDelay(true);

            client = StreamSocket(std::move(rawClient));
            server = StreamSocket(std::move(rawServer));

            if (timeoutMs > 0)
            {
                client.SetReadTimeout(timeoutMs);
                client.SetWriteTimeout(timeoutMs);
                server.SetReadTimeout(timeoutMs);
                server.SetWriteTimeout(timeoutMs);
            }

            ok = true;
        }
    };

    // =========================================================================
    // ТЕСТ 1: КОНСТРУКТОРЫ, MOVE-СЕМАНТИКА, СВОЙСТВА И ТАЙМАУТЫ
    // =========================================================================

    /**
     * @brief Проверяет оба конструктора, перемещение, свойства потока и настройку таймаутов.
     * @param [in,out] tr Экземпляр `TestRunner` для логирования проверок.
     */
    void TestConstructorsMoveAndProperties(TestRunner& tr)
    {
        tr.BeginSuite("1. Конструкторы, move-семантика, свойства и таймауты");

        Socket listenSock(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        listenSock.SetReuseAddress(true);
        if (!tr.Check(listenSock.IsCreated() && listenSock.Bind("127.0.0.1", 0) && listenSock.Listen(4),
            "Запущен временный локальный TCP-сервер"))
        {
            tr.EndSuite();
            return;
        }
        const int port = listenSock.LocalPort();

        // 1. Конструктор из lvalue Socket& обязан переместить владение и обнулить исходный Socket.
        Socket rawClient(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        tr.Check(rawClient.Connect("127.0.0.1", port), "rawClient подключился к серверу");
        StreamSocket clientStream(rawClient);
        tr.Check(rawClient.IsClosed(), "После StreamSocket(Socket&) исходный rawClient стал закрытым");
        tr.Check(clientStream.IsGood() && static_cast<bool>(clientStream) && clientStream.LastError() == 0,
            "clientStream создан в исправном состоянии (IsGood() == true, LastError() == 0)");

        // 2. Конструктор из rvalue Socket&& (прямо из Accept()).
        StreamSocket serverStream(listenSock.Accept());
        tr.Check(serverStream.IsGood() && serverStream.SockInternal().IsCreated(),
            "StreamSocket(listenSock.Accept()) успешно принял владение сокетом");

        // 3. Базовые свойства потока.
        tr.Check(clientStream.CanTimeout(), "CanTimeout() == true");
        tr.Check(!clientStream.CanSeek(), "CanSeek() == false");
        tr.Check(clientStream.CanRead() && clientStream.CanWrite(), "CanRead() == true и CanWrite() == true");
        clientStream.Flush(); // Безопасная заглушка.
        tr.Check(clientStream.IsGood(), "Flush() не нарушает состояние потока");

        // 4. Таймауты (значения 1..499 округляются до 500 мс, -1 = бесконечно).
        tr.Check(clientStream.SetReadTimeout(200) == 0, "SetReadTimeout(200) успешен");
        tr.Check(clientStream.GetReadTimeout() >= 500,
            "GetReadTimeout() вернул " + std::to_string(clientStream.GetReadTimeout()) + " мс (>= 500)");
        tr.Check(clientStream.SetWriteTimeout(1500) == 0, "SetWriteTimeout(1500) успешен");
        tr.Check(clientStream.GetWriteTimeout() >= 1500,
            "GetWriteTimeout() вернул " + std::to_string(clientStream.GetWriteTimeout()) + " мс (>= 1500)");
        tr.Check(clientStream.SetReadTimeout(-1) == 0 && clientStream.GetReadTimeout() == -1,
            "Сброс таймаута чтения в -1 (бесконечно) работает корректно");

        // 5. Move-конструктор и move-присваивание.
        StreamSocket movedCtor(std::move(clientStream));
        tr.Check(!clientStream.IsGood() && clientStream.SockInternal().IsClosed(),
            "После move-конструктора исходный clientStream закрыт и !IsGood()");
        tr.Check(movedCtor.IsGood() && movedCtor.SockInternal().IsCreated(),
            "movedCtor валиден и владеет сокетом");

        StreamSocket movedAssign(Socket{});
        movedAssign = std::move(movedCtor);
        tr.Check(!movedCtor.IsGood() && movedCtor.SockInternal().IsClosed(),
            "После move-присваивания movedCtor обнулён");
        tr.Check(movedAssign.IsGood() && movedAssign.CanWrite(),
            "movedAssign полностью работоспособен");

        tr.EndSuite();
    }

    // =========================================================================
    // ТЕСТ 2: СЫРЫЕ И ГАРАНТИРОВАННЫЕ ОПЕРАЦИИ ЗАПИСИ И ЧТЕНИЯ
    // =========================================================================

    /**
     * @brief Проверяет `Write`, `WriteString`, `WriteAll`, `Read`, `ReadExactly`, `ReadBytes`, `ReadString` и валидацию аргументов.
     * @param [in,out] tr Экземпляр `TestRunner` для логирования проверок.
     */
    void TestRawAndExactIO(TestRunner& tr)
    {
        tr.BeginSuite("2. Сырые и гарантированные операции I/O (Write/Read/ReadExactly/ReadBytes/ReadString)");

        StreamPair pair;
        if (!tr.Check(pair.ok, "Установлена пара StreamSocket"))
        {
            tr.EndSuite();
            return;
        }

        // 1. Write(vector<char>) -> ReadBytes(size).
        const std::vector<char> vecPayload = { 'B', 'I', 'N', '\0', '1', '2', '3' };
        int sentVec = pair.client.Write(vecPayload);
        tr.Check(sentVec == static_cast<int>(vecPayload.size()),
            "Write(vector<char>) отправил " + std::to_string(sentVec) + " байт (включая '\\0')");

        std::vector<char> recvdVec = pair.server.ReadBytes(static_cast<int>(vecPayload.size()));
        tr.Check(recvdVec == vecPayload && pair.server.IsGood(),
            "ReadBytes() точно восстановил бинарный вектор с нулевым байтом внутри");

        // 2. WriteString -> ReadString.
        const std::string textPayload = "StreamSocket_Exact_String";
        int sentStr = pair.client.WriteString(textPayload);
        tr.Check(sentStr == static_cast<int>(textPayload.size()), "WriteString() отправил всю строку");

        std::string recvdStr = pair.server.ReadString(static_cast<int>(textPayload.size()));
        tr.Check(recvdStr == textPayload && pair.server.IsGood(),
            "ReadString() получил '" + recvdStr + "'");

        // 3. WriteAll -> ReadExactly.
        const char rawData[] = "RAW_ALL_BLOCK_0123456789";
        constexpr int rawLen = static_cast<int>(sizeof(rawData) - 1);
        tr.Check(pair.client.WriteAll(rawData, rawLen), "WriteAll() отправил блок целиком");

        char recvBuf[64] = {};
        tr.Check(pair.server.ReadExactly(recvBuf, rawLen) && std::memcmp(recvBuf, rawData, rawLen) == 0,
            "ReadExactly() вычитал ровно " + std::to_string(rawLen) + " байт без искажений");

        // 4. Проверка обработки некорректных аргументов (nullptr, size <= 0, пустые контейнеры).
        tr.Check(pair.client.Write(nullptr, 10) == -1 && !pair.client.IsGood() && pair.client.LastError() == SOCK_EINVAL,
            "Write(nullptr, 10) вернул -1 и установил SOCK_EINVAL");
        pair.client.ClearError();

        tr.Check(pair.client.Write(std::vector<char>{}) == -1 && pair.client.LastError() == SOCK_EINVAL,
            "Write(пустой vector) вернул -1 + SOCK_EINVAL");
        pair.client.ClearError();

        tr.Check(pair.client.WriteString("") == -1 && pair.client.LastError() == SOCK_EINVAL,
            "WriteString(\"\") вернул -1 + SOCK_EINVAL");
        pair.client.ClearError();

        tr.Check(!pair.client.WriteAll(nullptr, 5) && pair.client.LastError() == SOCK_EINVAL,
            "WriteAll(nullptr, 5) вернул false + SOCK_EINVAL");
        pair.client.ClearError();

        tr.Check(pair.server.Read(nullptr, 5) == -1 && pair.server.LastError() == SOCK_EINVAL,
            "Read(nullptr, 5) вернул -1 + SOCK_EINVAL");
        pair.server.ClearError();

        tr.Check(pair.server.ReadBytes(0).empty() && !pair.server.IsGood() && pair.server.LastError() == SOCK_EINVAL,
            "ReadBytes(0) вернул пустой вектор + SOCK_EINVAL");
        pair.server.ClearError();

        tr.Check(pair.server.ReadString(-4).empty() && !pair.server.IsGood() && pair.server.LastError() == SOCK_EINVAL,
            "ReadString(-4) вернул \"\" + SOCK_EINVAL");
        pair.server.ClearError();

        tr.EndSuite();
    }

    // =========================================================================
    // ТЕСТ 3: READLINE, СМЕШИВАНИЕ С БИНАРНЫМ ЧТЕНИЕМ, DOS-ЛИМИТ И EOF БЕЗ '\n'
    // =========================================================================

    /**
     * @brief Проверяет `ReadLine` (`\n`, `\r\n`, кастомный разделитель), смешивание с `ReadExactly`/`Read`,
     *        ограничение `maxLength` и чтение последней строки при EOF без завершающего переноса.
     * @param [in,out] tr Экземпляр `TestRunner` для логирования проверок.
     */
    void TestReadLineAndMixedReading(TestRunner& tr)
    {
        tr.BeginSuite("3. ReadLine (LF/CRLF/смешанное чтение/DoS-лимит/EOF без '\\n')");

        StreamPair pair;
        if (!tr.Check(pair.ok, "Установлена пара StreamSocket"))
        {
            tr.EndSuite();
            return;
        }

        // Отправляем одним пакетом: LF-строку, CRLF-строку, пустую строку, строку с ';', и 4 бинарных байта.
        const std::string mixed = "FirstLine\nSecondLine\r\n\nCustomDelim;BIN!";
        tr.Check(pair.client.WriteAll(mixed.data(), static_cast<int>(mixed.size())),
            "Отправлен смешанный блок (строки + бинарный хвост 'BIN!')");

        // 1. Первая строка (LF) заполняет локальный _readBuf.
        std::string l1 = pair.server.ReadLine('\n');
        tr.Check(l1 == "FirstLine" && pair.server.IsGood(), "ReadLine #1 (LF): '" + l1 + "'");

        // После первого ReadLine остаток уже лежит в _readBuf -> Available() > 0 и WaitForData(0) == true.
        tr.Check(pair.server.Available() > 0 && pair.server.WaitForData(0),
            "После ReadLine #1 остаток находится в _readBuf (Available=" + std::to_string(pair.server.Available()) + ")");

        // 2. Вторая строка (CRLF -> '\r' должен отброситься).
        std::string l2 = pair.server.ReadLine('\n');
        tr.Check(l2 == "SecondLine" && pair.server.IsGood(), "ReadLine #2 (CRLF без '\\r'): '" + l2 + "'");

        // 3. Пустая строка "\n" -> возвращает "" и оставляет IsGood() == true.
        std::string l3 = pair.server.ReadLine('\n');
        tr.Check(l3.empty() && pair.server.IsGood(), "ReadLine #3 (пустая строка): \"\" при IsGood() == true");

        // 4. Кастомный разделитель ';'.
        std::string l4 = pair.server.ReadLine(';');
        tr.Check(l4 == "CustomDelim" && pair.server.IsGood(), "ReadLine #4 (delimiter=';'): '" + l4 + "'");

        // 5. Бинарное чтение сразу после ReadLine — проверяем, что данные из _readBuf не потеряны!
        char binTail[4] = {};
        tr.Check(pair.server.ReadExactly(binTail, 4) && std::memcmp(binTail, "BIN!", 4) == 0,
            "ReadExactly(4) после ReadLine точно забрал 'BIN!' из локального _readBuf");

        // 6. Защита от DoS: строка длиннее maxLength (10 байт).
        const std::string tooLong = "0123456789ABCDEF\n";
        tr.Check(pair.client.WriteAll(tooLong.data(), static_cast<int>(tooLong.size())),
            "Отправлена слишком длинная строка (16 символов)");
        std::string over = pair.server.ReadLine('\n', /*maxLength=*/10);
        tr.Check(over.empty() && !pair.server.IsGood() && pair.server.LastError() == SOCK_EMSGSIZE,
            "ReadLine(maxLength=10) отклонил строку с кодом SOCK_EMSGSIZE", pair.server.LastError());
        pair.server.ClearError();
        // Дочитываем остаток отклонённой строки до '\n'.
        pair.server.ReadLine('\n');

        // 7. Последняя строка без завершающего '\n' перед закрытием соединения (EOF).
        const std::string lastNoNewline = "TAIL_WITHOUT_LF";
        tr.Check(pair.client.WriteAll(lastNoNewline.data(), static_cast<int>(lastNoNewline.size())),
            "Отправлена последняя строка без '\\n' и закрыт клиентский сокет");
        pair.client.Close(0);

        std::string lLast = pair.server.ReadLine('\n');
        tr.Check(lLast == lastNoNewline && pair.server.IsGood(),
            "ReadLine при EOF вернул финальную строку '" + lLast + "' с IsGood() == true");

        // Следующий ReadLine на пустом EOF обязан вернуть "" и перевести поток в SOCK_ENOTCONN.
        std::string lEof = pair.server.ReadLine('\n');
        tr.Check(lEof.empty() && !pair.server.IsGood() && pair.server.LastError() == SOCK_ENOTCONN,
            "Повторный ReadLine на EOF вернул \"\" и выставил SOCK_ENOTCONN", pair.server.LastError());

        tr.EndSuite();
    }

    // =========================================================================
    // ТЕСТ 4: READUNTIL (МНОГОСИМВОЛЬНЫЙ РАЗДЕЛИТЕЛЬ, HTTP-СЦЕНАРИЙ И DOS-ЛИМИТ)
    // =========================================================================

    /**
     * @brief Проверяет `ReadUntil` для HTTP-заголовков `"\r\n\r\n"`, защиту `maxLength`, пустой разделитель и EOF.
     * @param [in,out] tr Экземпляр `TestRunner` для логирования проверок.
     */
    void TestReadUntil(TestRunner& tr)
    {
        tr.BeginSuite("4. ReadUntil (HTTP \"\\r\\n\\r\\n\", защита maxLength, пустой разделитель, EOF)");

        StreamPair pair;
        if (!tr.Check(pair.ok, "Установлена пара StreamSocket"))
        {
            tr.EndSuite();
            return;
        }

        // 1. HTTP-запрос: заголовки до "\r\n\r\n" + тело "BODY123".
        const std::string httpHeaders = "GET /index.html HTTP/1.1\r\nHost: localhost\r\nContent-Length: 7";
        const std::string httpBody = "BODY123";
        const std::string fullReq = httpHeaders + "\r\n\r\n" + httpBody;

        tr.Check(pair.client.WriteAll(fullReq.data(), static_cast<int>(fullReq.size())),
            "Отправлен HTTP-запрос (заголовки + \\r\\n\\r\\n + тело)");

        std::string parsedHeaders = pair.server.ReadUntil("\r\n\r\n");
        tr.Check(parsedHeaders == httpHeaders && pair.server.IsGood(),
            "ReadUntil(\"\\r\\n\\r\\n\") точно выделил HTTP-заголовки без разделителя");

        std::string parsedBody = pair.server.ReadString(static_cast<int>(httpBody.size()));
        tr.Check(parsedBody == httpBody && pair.server.IsGood(),
            "ReadString(7) сразу после ReadUntil вычитал тело '" + parsedBody + "'");

        // 2. Пустой разделитель -> SOCK_EINVAL.
        std::string emptyDelim = pair.server.ReadUntil("");
        tr.Check(emptyDelim.empty() && !pair.server.IsGood() && pair.server.LastError() == SOCK_EINVAL,
            "ReadUntil(\"\") вернул \"\" и выставил SOCK_EINVAL");
        pair.server.ClearError();

        // 3. Превышение maxLength -> SOCK_EMSGSIZE.
        const std::string overflowMsg = "123456789012345--END--";
        pair.client.WriteAll(overflowMsg.data(), static_cast<int>(overflowMsg.size()));
        std::string tooBig = pair.server.ReadUntil("--END--", /*maxLength=*/8);
        tr.Check(tooBig.empty() && !pair.server.IsGood() && pair.server.LastError() == SOCK_EMSGSIZE,
            "ReadUntil(maxLength=8) прервал чтение с кодом SOCK_EMSGSIZE", pair.server.LastError());
        pair.server.ClearError();
        pair.server.ReadUntil("--END--"); // Добираем остаток до маркера.

        // 4. EOF до появления разделителя -> возвращает частичный буфер и ставит SOCK_ENOTCONN.
        const std::string partial = "PARTIAL_NO_MARKER";
        pair.client.WriteAll(partial.data(), static_cast<int>(partial.size()));
        pair.client.Close(0);

        std::string cut = pair.server.ReadUntil("::STOP::");
        tr.Check(cut == partial && !pair.server.IsGood() && pair.server.LastError() == SOCK_ENOTCONN,
            "ReadUntil при EOF без разделителя вернул '" + cut + "' и выставил SOCK_ENOTCONN");

        tr.EndSuite();
    }

    // =========================================================================
    // ТЕСТ 5: БИНАРНЫЙ ФРЕЙМИНГ ПАКЕТОВ BYTEBUFFER (ВКЛЮЧАЯ 256 КБ И DOS-ЛИМИТ)
    // =========================================================================

    /**
     * @brief Проверяет `WritePacket`/`ReadPacket` и операторы `<<`/`>>` для `ByteBuffer`:
     *        сложный пакет, 0-байтный пакет, крупный пакет 256 КБ и ограничение `maxPacketSize`.
     * @param [in,out] tr Экземпляр `TestRunner` для логирования проверок.
     */
    void TestBinaryPacketFraming(TestRunner& tr)
    {
        tr.BeginSuite("5. Бинарные пакеты ByteBuffer (WritePacket/ReadPacket, пустой, 256 КБ, DoS-лимит)");

        StreamPair pair;
        if (!tr.Check(pair.ok, "Установлена пара StreamSocket"))
        {
            tr.EndSuite();
            return;
        }

        // 1. Обычный пакет через потоковые операторы << и >>.
        ByteBuffer tx(256, ByteOrder::BigEndian);
        tx << uint32_t(0xDEADBEEFU) << int16_t(-12345) << true << double(3.1415926535) << std::string("PacketPayload");
        tx.WriteBits(0b101101U, 6);
        tx.AlignWriteToByte();

        pair.client << tx;
        tr.Check(pair.client.IsGood(), "operator<<(ByteBuffer) отправил пакет (" + std::to_string(tx.Size()) + " байт)");

        ByteBuffer rx(0, ByteOrder::BigEndian);
        pair.server >> rx;
        tr.Check(pair.server.IsGood() && rx.Size() == tx.Size() && rx.ReadOffset() == 0,
            "operator>>(ByteBuffer) принял пакет (" + std::to_string(rx.Size()) + " байт) и перемотал курсор в 0");

        uint32_t magic = 0;
        int16_t  s16 = 0;
        bool     flag = false;
        double   pi = 0.0;
        std::string text;
        uint64_t bits = 0;
        rx >> magic >> s16 >> flag >> pi >> text;
        bits = rx.ReadBits(6);

        tr.Check(magic == 0xDEADBEEFU && s16 == -12345 && flag && std::fabs(pi - 3.1415926535) < 1e-12
            && text == "PacketPayload" && bits == 0b101101U,
            "Все поля десериализованы без потерь (magic, int16, bool, double, string, 6 бит)");

        // 2. Пустой 0-байтный пакет (валидный случай: отправляется 4-байтный нулевой заголовок).
        ByteBuffer emptyTx(0);
        tr.Check(pair.client.WritePacket(emptyTx), "WritePacket(пустой ByteBuffer) успешен");
        ByteBuffer emptyRx(16);
        emptyRx << uint32_t(999); // Предварительно кладём мусор, чтобы проверить очистку.
        tr.Check(pair.server.ReadPacket(emptyRx) && emptyRx.Empty() && pair.server.IsGood(),
            "ReadPacket() корректно принял 0-байтный пакет и очистил целевой буфер");

        // 3. Крупный фрагментированный пакет (256 КБ) в параллельном потоке для защиты от дедлока буферов ОС.
        constexpr size_t kLargeSize = 256U * 1024U;
        ByteBuffer largeTx(kLargeSize);
        largeTx.Resize(kLargeSize);
        for (size_t i = 0; i < kLargeSize; ++i)
            largeTx[i] = static_cast<uint8_t>((i * 131U + 17U) & 0xFFU);

        bool sendLargeOk = false;
        std::thread senderThread([&]()
            {
                sendLargeOk = pair.client.WritePacket(largeTx);
            });

        ByteBuffer largeRx(0);
        bool recvLargeOk = pair.server.ReadPacket(largeRx);
        senderThread.join();

        bool contentMatch = (largeRx.Size() == kLargeSize)
            && (std::memcmp(largeRx.Data(), largeTx.Data(), kLargeSize) == 0);
        tr.Check(sendLargeOk && recvLargeOk && contentMatch,
            "Крупный пакет 256 КБ (" + std::to_string(kLargeSize) + " байт) доставлен и совпал побайтово");

        // 4. Защита от DoS: превышение maxPacketSize -> SOCK_EMSGSIZE.
        ByteBuffer oversizedTx(64);
        oversizedTx.Resize(64);
        tr.Check(pair.client.WritePacket(oversizedTx), "Отправлен пакет 64 байта для проверки лимита");

        ByteBuffer limitedRx(0);
        bool accepted = pair.server.ReadPacket(limitedRx, /*maxPacketSize=*/32);
        tr.Check(!accepted && limitedRx.Empty() && !pair.server.IsGood() && pair.server.LastError() == SOCK_EMSGSIZE,
            "ReadPacket(maxPacketSize=32) отклонил 64-байтный пакет с SOCK_EMSGSIZE", pair.server.LastError());

        tr.EndSuite();
    }

    // =========================================================================
    // ТЕСТ 6: ПОТОКОВЫЕ ОПЕРАТОРЫ << И >> (ВСЕ ТИПЫ И ГРАНИЧНЫЕ ЗНАЧЕНИЯ)
    // =========================================================================

    /**
     * @brief Проверяет round-trip сериализацию всех поддерживаемых типов через `operator<<` и `operator>>`.
     * @param [in,out] tr Экземпляр `TestRunner` для логирования проверок.
     */
    void TestStreamOperatorsValid(TestRunner& tr)
    {
        tr.BeginSuite("6. Потоковые операторы << и >> (все типы и граничные значения)");

        StreamPair pair;
        if (!tr.Check(pair.ok, "Установлена пара StreamSocket"))
        {
            tr.EndSuite();
            return;
        }

        const std::string expStr = "Hello StreamSocket!";
        const bool        expBoolT = true;
        const bool        expBoolF = false;
        const char        expChar = 'Z';
        const int8_t      expI8 = std::numeric_limits<int8_t>::min();
        const int16_t     expI16 = std::numeric_limits<int16_t>::min();
        const int32_t     expI32 = std::numeric_limits<int32_t>::min();
        const int64_t     expI64 = std::numeric_limits<int64_t>::min();
        const uint8_t     expU8 = std::numeric_limits<uint8_t>::max();
        const uint16_t    expU16 = std::numeric_limits<uint16_t>::max();
        const uint32_t    expU32 = std::numeric_limits<uint32_t>::max();
        const uint64_t    expU64 = std::numeric_limits<uint64_t>::max();
        const float       expFlt = -12345.6787f;
        const double      expDbl = 3.141592653589793;

        // Пустые строки "" и std::string{} не должны переводить поток в ошибку.
        pair.client << "" << std::string{}
            << expStr << "\n"
            << expBoolT << expBoolF << expChar
            << expI8 << expI16 << expI32 << expI64
            << expU8 << expU16 << expU32 << expU64
            << expFlt << expDbl;

        tr.Check(pair.client.IsGood(), "Цепочка operator<< для всех типов и пустых строк завершилась без ошибок");

        std::string gotStr;
        bool        gotBoolT = false;
        bool        gotBoolF = true;
        char        gotChar = 0;
        int8_t      gotI8 = 0;
        int16_t     gotI16 = 0;
        int32_t     gotI32 = 0;
        int64_t     gotI64 = 0;
        uint8_t     gotU8 = 0;
        uint16_t    gotU16 = 0;
        uint32_t    gotU32 = 0;
        uint64_t    gotU64 = 0;
        float       gotFlt = 0.0f;
        double      gotDbl = 0.0;

        pair.server >> gotStr >> gotBoolT >> gotBoolF >> gotChar
            >> gotI8 >> gotI16 >> gotI32 >> gotI64
            >> gotU8 >> gotU16 >> gotU32 >> gotU64
            >> gotFlt >> gotDbl;

        tr.Check(pair.server.IsGood(), "Цепочка operator>> завершилась с IsGood() == true");
        tr.Check(gotStr == expStr, "std::string: '" + gotStr + "'");
        tr.Check(gotBoolT == expBoolT && gotBoolF == expBoolF, "bool: true и false");
        tr.Check(gotChar == expChar, std::string("char: '") + gotChar + "'");
        tr.Check(gotI8 == expI8 && gotI16 == expI16 && gotI32 == expI32 && gotI64 == expI64,
            "int8/16/32/64 граничные минимумы совпали точно");
        tr.Check(gotU8 == expU8 && gotU16 == expU16 && gotU32 == expU32 && gotU64 == expU64,
            "uint8/16/32/64 граничные максимумы совпали точно");
        tr.Check(std::fabs(gotFlt - expFlt) < 1e-4f, "float round-trip точен (получено " + std::to_string(gotFlt) + ")");
        tr.Check(std::fabs(gotDbl - expDbl) < 1e-14,
            "double round-trip сохранил полную точность (отклонение < 1e-14)");

        tr.EndSuite();
    }

    // =========================================================================
    // ТЕСТ 7: СТРОГАЯ ВАЛИДАЦИЯ И НЕГАТИВНЫЕ СЦЕНАРИИ OPERATOR<< / OPERATOR>>
    // =========================================================================

    /**
     * @brief Проверяет отклонение некорректных данных в `operator<<`/`operator>>` без порчи выходных переменных.
     * @param [in,out] tr Экземпляр `TestRunner` для логирования проверок.
     */
    void TestStreamOperatorsInvalidInput(TestRunner& tr)
    {
        tr.BeginSuite("7. Негативные сценарии парсинга (пустая строка, хвост, переполнение, '-1' в uint, NaN/Inf)");

        StreamPair pair;
        if (!tr.Check(pair.ok, "Установлена пара StreamSocket"))
        {
            tr.EndSuite();
            return;
        }

        // 1. Передача nullptr в operator<<(const char*).
        const char* nullStr = nullptr;
        pair.client << nullStr;
        tr.Check(!pair.client.IsGood() && pair.client.LastError() == SOCK_EINVAL,
            "operator<<(nullptr) перевёл поток в ошибку SOCK_EINVAL");
        pair.client.ClearError();
        tr.Check(pair.client.IsGood() && pair.client.LastError() == 0, "ClearError() восстановил поток");

        // Отправляем набор заведомо некорректных строк для последовательного чтения на сервере.
        const std::string badInputs =
            "maybe\n"       // 1) невалидный bool
            "\n"            // 2) пустая строка -> int32_t (ранее был баг: не ставилась ошибка!)
            "42abc\n"       // 3) мусор в хвосте -> int32_t
            "300\n"         // 4) переполнение int8_t (max 127)
            "-1\n"          // 5) отрицательное число -> uint32_t
            "   -99\n"      // 6) отрицательное число с ведущими пробелами -> uint64_t
            "nan\n"         // 7) NaN -> float
            "inf\n"         // 8) Inf -> double
            "12.34xyz\n";   // 9) мусор в хвосте -> double

        tr.Check(pair.client.WriteAll(badInputs.data(), static_cast<int>(badInputs.size())),
            "Отправлен набор из 9 некорректных строк для проверки строгого парсинга");

        // 1) bool
        bool bVal = true;
        pair.server >> bVal;
        tr.Check(!pair.server.IsGood() && pair.server.LastError() == SOCK_EINVAL && bVal == true,
            "Невалидный bool ('maybe') -> SOCK_EINVAL, переменная не изменена");
        pair.server.ClearError();

        // 2) пустая строка -> int32_t
        int32_t i32Val = 777;
        pair.server >> i32Val;
        tr.Check(!pair.server.IsGood() && pair.server.LastError() == SOCK_EINVAL && i32Val == 777,
            "Пустая строка '\\n' для int32_t -> SOCK_EINVAL, переменная не изменена");
        pair.server.ClearError();

        // 3) хвост мусора -> int32_t
        pair.server >> i32Val;
        tr.Check(!pair.server.IsGood() && pair.server.LastError() == SOCK_EINVAL && i32Val == 777,
            "Мусор в хвосте ('42abc') для int32_t -> SOCK_EINVAL, переменная не изменена");
        pair.server.ClearError();

        // 4) переполнение int8_t
        int8_t i8Val = 55;
        pair.server >> i8Val;
        tr.Check(!pair.server.IsGood() && pair.server.LastError() == SOCK_EINVAL && i8Val == 55,
            "Переполнение int8_t ('300') -> SOCK_EINVAL, переменная не изменена");
        pair.server.ClearError();

        // 5) '-1' -> uint32_t
        uint32_t u32Val = 123;
        pair.server >> u32Val;
        tr.Check(!pair.server.IsGood() && pair.server.LastError() == SOCK_EINVAL && u32Val == 123,
            "Отрицательное '-1' для uint32_t отклонено (SOCK_EINVAL), переменная не изменена");
        pair.server.ClearError();

        // 6) '   -99' -> uint64_t
        uint64_t u64Val = 456;
        pair.server >> u64Val;
        tr.Check(!pair.server.IsGood() && pair.server.LastError() == SOCK_EINVAL && u64Val == 456,
            "Отрицательное с пробелами '   -99' для uint64_t отклонено (SOCK_EINVAL)");
        pair.server.ClearError();

        // 7) 'nan' -> float
        float fVal = 1.5f;
        pair.server >> fVal;
        tr.Check(!pair.server.IsGood() && pair.server.LastError() == SOCK_EINVAL && fVal == 1.5f,
            "'nan' для float отклонён (SOCK_EINVAL), переменная не изменена");
        pair.server.ClearError();

        // 8) 'inf' -> double
        double dVal = 2.5;
        pair.server >> dVal;
        tr.Check(!pair.server.IsGood() && pair.server.LastError() == SOCK_EINVAL && dVal == 2.5,
            "'inf' для double отклонён (SOCK_EINVAL), переменная не изменена");
        pair.server.ClearError();

        // 9) '12.34xyz' -> double
        pair.server >> dVal;
        tr.Check(!pair.server.IsGood() && pair.server.LastError() == SOCK_EINVAL && dVal == 2.5,
            "Хвост мусора '12.34xyz' для double отклонён (SOCK_EINVAL)");

        tr.EndSuite();
    }

    // =========================================================================
    // ТЕСТ 8: WRITEFORMAT И READFORMAT
    // =========================================================================

    /**
     * @brief Проверяет форматированную отправку `WriteFormat()` и разбор строки `ReadFormat()`.
     * @param [in,out] tr Экземпляр `TestRunner` для логирования проверок.
     */
    void TestWriteFormatAndReadFormat(TestRunner& tr)
    {
        tr.BeginSuite("8. Форматированный ввод-вывод (WriteFormat / ReadFormat)");

        StreamPair pair;
        if (!tr.Check(pair.ok, "Установлена пара StreamSocket"))
        {
            tr.EndSuite();
            return;
        }

        // 1. Успешный WriteFormat -> ReadFormat.
        int written = pair.client.WriteFormat("%s %d %.2f\n", "PLAYER", 42, 98.75);
        tr.Check(written > 0 && pair.client.IsGood(),
            "WriteFormat отправил " + std::to_string(written) + " байт");

        std::string tag;
        int         level = 0;
        double      score = 0.0;
        bool parsedOk = pair.server.ReadFormat(tag, level, score);
        tr.Check(parsedOk && pair.server.IsGood() && tag == "PLAYER" && level == 42 && std::fabs(score - 98.75) < 1e-6,
            "ReadFormat разобрал: tag='" + tag + "', level=" + std::to_string(level) + ", score=" + std::to_string(score));

        // 2. WriteFormat(nullptr) -> -1 + SOCK_EINVAL.
        tr.Check(pair.client.WriteFormat(nullptr) == -1 && !pair.client.IsGood() && pair.client.LastError() == SOCK_EINVAL,
            "WriteFormat(nullptr) вернул -1 и выставил SOCK_EINVAL");
        pair.client.ClearError();

        // 3. ReadFormat на пустой строке -> false + SOCK_EINVAL.
        pair.client << "\n";
        int dummy = 0;
        tr.Check(!pair.server.ReadFormat(dummy) && !pair.server.IsGood() && pair.server.LastError() == SOCK_EINVAL,
            "ReadFormat на пустой строке вернул false и выставил SOCK_EINVAL");
        pair.server.ClearError();

        // 4. ReadFormat при несовпадении типов -> false + SOCK_EINVAL.
        pair.client << "NOT_A_NUMBER\n";
        tr.Check(!pair.server.ReadFormat(dummy) && !pair.server.IsGood() && pair.server.LastError() == SOCK_EINVAL,
            "ReadFormat при несовпадении типа ('NOT_A_NUMBER' -> int) вернул false + SOCK_EINVAL");

        tr.EndSuite();
    }

    // =========================================================================
    // ТЕСТ 9: CANREAD VS CANWRITE ПРИ ОСТАТКЕ В _READBUF ПОСЛЕ ЗАКРЫТИЯ ПИРОМ
    // =========================================================================

    /**
     * @brief Проверяет исправленный баг `CanWrite()`: после закрытия соединения пиром при наличии
     *        непрочитанного хвоста в `_readBuf` метод `CanRead()` обязан возвращать `true`, а `CanWrite()` — `false`.
     * @param [in,out] tr Экземпляр `TestRunner` для логирования проверок.
     */
    void TestCanReadVsCanWriteAfterPeerClose(TestRunner& tr)
    {
        tr.BeginSuite("9. Разделение CanRead() и CanWrite() при остатке в _readBuf после закрытия пиром");

        StreamPair pair;
        if (!tr.Check(pair.ok, "Установлена пара StreamSocket"))
        {
            tr.EndSuite();
            return;
        }

        // Клиент отправляет две части одним блоком и закрывает соединение (TCP FIN).
        const std::string msg = "LINE_ONE\nTAIL";
        tr.Check(pair.client.WriteAll(msg.data(), static_cast<int>(msg.size())),
            "Клиент отправил 'LINE_ONE\\nTAIL' и закрывает сокет");
        pair.client.Close(0);

        // Читаем первую строку: recv(4096) забирает в _readBuf всё сразу, поэтому "TAIL" остаётся в _readBuf,
        // а в буфере ядра остаётся только сигнал TCP FIN от закрытого клиента.
        std::string first = pair.server.ReadLine('\n');
        tr.Check(first == "LINE_ONE" && pair.server.IsGood(), "Первая строка прочитана: '" + first + "'");

        // Даём ядру ОС до 200 мс на доставку TCP FIN на loopback, чтобы тест был на 100% детерминированным.
        for (int i = 0; i < 20 && pair.server.CanWrite(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));

        tr.Info("После получения TCP FIN при хвосте 'TAIL' в _readBuf: Available="
            + std::to_string(pair.server.Available())
            + ", CanRead=" + (pair.server.CanRead() ? "true" : "false")
            + ", CanWrite=" + (pair.server.CanWrite() ? "true" : "false"));

        tr.Check(pair.server.Available() == 4, "Available() == 4 (хвост 'TAIL' в локальном _readBuf)");
        tr.Check(pair.server.WaitForData(0), "WaitForData(0) == true (данные уже в _readBuf)");
        tr.Check(pair.server.CanRead(), "CanRead() == true, пока в _readBuf есть байты");
        tr.Check(!pair.server.CanWrite(), "CanWrite() == false, т.к. удалённая сторона уже закрыла сокет");

        // Дочитываем остаток "TAIL" из _readBuf через Read().
        char tailBuf[16] = {};
        int n1 = pair.server.Read(tailBuf, static_cast<int>(sizeof(tailBuf)));
        tr.Check(n1 == 4 && std::string(tailBuf, 4) == "TAIL",
            "Read() отдал оставшиеся 4 байта ('TAIL') из _readBuf");

        // Следующий Read() обязан вернуть ровно 0 (штатный EOF), а не -1!
        int n2 = pair.server.Read(tailBuf, static_cast<int>(sizeof(tailBuf)));
        tr.Check(n2 == 0 && pair.server.IsGood(),
            "Read() на штатном EOF вернул ровно 0 (не -1) и сохранил IsGood() == true");

        // Проверяем Close() и Dispose().
        tr.Check(pair.server.Close(0), "Close(0) на открытом потоке вернул true");
        tr.Check(!pair.server.Close(0), "Повторный Close(0) на закрытом потоке вернул false");
        tr.Check(pair.server.Dispose(), "Dispose() на закрытом потоке безопасен и возвращает true");

        tr.EndSuite();
    }

    // =========================================================================
    // ТЕСТ 10: ЗАКРЫТЫЙ ПОТОК И ТАЙМАУТ WAITFORDATA
    // =========================================================================

    /**
     * @brief Проверяет поведение `WaitForData(timeoutMs)` на тихом сокете и отказоустойчивость на закрытом `StreamSocket`.
     * @param [in,out] tr Экземпляр `TestRunner` для логирования проверок.
     */
    void TestWaitForDataAndClosedStream(TestRunner& tr)
    {
        tr.BeginSuite("10. Таймаут WaitForData() и операции на закрытом StreamSocket");

        // 1. WaitForData на тихом подключённом сокете -> false по таймауту, затем true при приходе данных.
        StreamPair pair;
        if (!tr.Check(pair.ok, "Установлена пара StreamSocket"))
        {
            tr.EndSuite();
            return;
        }

        using Clock = std::chrono::steady_clock;
        const auto t0 = Clock::now();
        bool hasData0 = pair.server.WaitForData(60);
        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count();
        tr.Info("Фактическое время WaitForData(60) на тихом сокете: " + std::to_string(elapsedMs) + " мс");
        tr.Check(!hasData0 && elapsedMs >= 40, "WaitForData(60) на тихом сокете выждал таймаут и вернул false");

        pair.client << 'K';
        tr.Check(pair.server.WaitForData(500), "После отправки 'K' WaitForData(500) вернул true");
        char ch = 0;
        pair.server >> ch;
        tr.Check(ch == 'K' && pair.server.IsGood(), "Символ 'K' успешно прочитан через operator>>");

        // 2. Все операции на закрытом потоке обязаны безопасно возвращать ошибку SOCK_EBADF.
        StreamSocket closedStream(Socket{});
        tr.Check(!closedStream.CanRead() && !closedStream.CanWrite(), "Закрытый поток: CanRead() == false, CanWrite() == false");
        tr.Check(closedStream.Available() == -1, "Закрытый поток: Available() == -1");
        tr.Check(!closedStream.WaitForData(10), "Закрытый поток: WaitForData(10) == false");
        tr.Check(closedStream.GetReadTimeout() == -1 && closedStream.SetReadTimeout(1000) == -1,
            "Закрытый поток: Get/SetReadTimeout возвращают -1");

        char buf[8] = "abc";
        tr.Check(closedStream.Write(buf, 3) == -1 && closedStream.LastError() == SOCK_EBADF,
            "Write() на закрытом потоке -> -1 + SOCK_EBADF");
        closedStream.ClearError();

        tr.Check(closedStream.Read(buf, 3) == -1 && closedStream.LastError() == SOCK_EBADF,
            "Read() на закрытом потоке -> -1 + SOCK_EBADF");
        closedStream.ClearError();

        ByteBuffer pkt;
        tr.Check(!closedStream.WritePacket(pkt) && closedStream.LastError() == SOCK_EBADF,
            "WritePacket() на закрытом потоке -> false + SOCK_EBADF");
        closedStream.ClearError();

        tr.Check(!closedStream.ReadPacket(pkt) && closedStream.LastError() == SOCK_EBADF,
            "ReadPacket() на закрытом потоке -> false + SOCK_EBADF");
        closedStream.ClearError();

        tr.Check(closedStream.ReadLine('\n').empty() && closedStream.LastError() == SOCK_EBADF,
            "ReadLine() на закрытом потоке -> \"\" + SOCK_EBADF");

        tr.EndSuite();
    }
} // anonymous namespace


int main()
{
    setlocale(LC_ALL, "");
    TestRunner tr;

    std::cout << "Запуск набора тестов StreamSocket...\n"
        << "Платформа: "
#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
        << "Windows (Winsock2)\n";
#else
        << "Linux (POSIX)\n";
#endif

    try
    {
        TestConstructorsMoveAndProperties(tr);
        TestRawAndExactIO(tr);
        TestReadLineAndMixedReading(tr);
        TestReadUntil(tr);
        TestBinaryPacketFraming(tr);
        TestStreamOperatorsValid(tr);
        TestStreamOperatorsInvalidInput(tr);
        TestWriteFormatAndReadFormat(tr);
        TestCanReadVsCanWriteAfterPeerClose(tr);
        TestWaitForDataAndClosedStream(tr);
    }
    catch (const std::exception& ex)
    {
        std::cerr << "\n[FATAL EXCEPTION] Неперехваченное std::exception: " << ex.what() << '\n';
        return 1;
    }
    catch (...)
    {
        std::cerr << "\n[FATAL EXCEPTION] Неперехваченное неизвестное исключение.\n";
        return 1;
    }

    return tr.PrintSummary();
}
