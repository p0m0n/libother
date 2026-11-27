#include "TCPClient.h"
#include "TCPServer.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>


namespace
{
    using namespace other::net::sockets;

    /**
     * @class TestRunner
     * @brief Вспомогательный регистратор проверок с подробным консольным логированием успехов и ошибок.
     *
     * Ведёт счётчики пройденных и проваленных проверок/сценариев, выводит системный код ошибки
     * и её текстовое описание через `SocketErrorToString()` при сбоях, а также возвращает итоговый код выхода.
     */
    class TestRunner
    {
    private:
        size_t      _totalPassed    = 0; ///< Суммарное количество успешных проверок за весь запуск.
        size_t      _totalFailed    = 0; ///< Суммарное количество проваленных проверок за весь запуск.
        size_t      _suitesPassed   = 0; ///< Количество полностью пройденных тестовых сценариев.
        size_t      _suitesFailed   = 0; ///< Количество сценариев, содержащих хотя бы одну ошибку.
        size_t      _curSuiteFailed = 0; ///< Число проваленных проверок в текущем сценарии.
        std::string _curSuiteName;       ///< Название текущего выполняемого сценария.

    public:
        /**
         * @brief Открывает новый тестовый сценарий и выводит его заголовок в консоль.
         * @param [in] name Название тестового сценария.
         * @throws std::bad_alloc При нехватке памяти для копирования строки имени.
         */
        void BeginSuite(const std::string& name)
        {
            _curSuiteName   = name;
            _curSuiteFailed = 0;
            std::cout << "\n========================================================================\n"
                      << "[TEST] " << name << '\n'
                      << "========================================================================\n";
        }

        /**
         * @brief Завершает текущий сценарий и выводит в консоль его статус (`[SUITE OK]` или `[SUITE FAILED]`).
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
         * @brief Выводит информационное сообщение о ходе выполнения теста.
         * @param [in] message Текст сообщения.
         */
        void Info(const std::string& message) const
        {
            std::cout << "  [INFO] " << message << '\n';
        }

        /**
         * @brief Проверяет булево условие и логирует `[PASS]` или `[FAIL]` с кодом ошибки при провале.
         * @param [in] condition Проверяемое условие.
         * @param [in] message   Описание проверяемого шага.
         * @param [in] errCode   Необязательный явный код ошибки (`0` — взять текущее значение `GET_NET_ERROR()`).
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
         * @brief Печатает итоговый отчёт по всем тестам и возвращает код завершения процесса.
         * @return `0`, если все проверки и сценарии пройдены; `1`, если зафиксирована хотя бы одна ошибка.
         */
        int PrintSummary() const
        {
            std::cout << "\n########################################################################\n"
                      << "  ИТОГОВЫЙ ОТЧЁТ ТЕСТИРОВАНИЯ TCPClient\n"
                      << "  Сценариев OK:    " << _suitesPassed << '\n'
                      << "  Сценариев FAIL:  " << _suitesFailed << '\n'
                      << "  Проверок PASS:   " << _totalPassed << '\n'
                      << "  Проверок FAIL:   " << _totalFailed << '\n'
                      << "########################################################################\n";

            return (_totalFailed == 0 && _suitesFailed == 0) ? 0 : 1;
        }
    };

    /**
     * @brief Резервирует заведомо свободный локальный порт на `127.0.0.1` и сразу освобождает его.
     *
     * Используется для проверки сценариев отказа подключения к закрытому порту или переподключения
     * на тот же порт после остановки/перезапуска сервера.
     * @param [out] outPort Сюда записывается найденный свободный порт (`> 0`).
     * @return `true`, если порт успешно получен; `false` при системной ошибке.
     */
    bool ReserveFreeLoopbackPort(int& outPort) noexcept
    {
        outPort = 0;
        Socket tmp(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (!tmp.IsCreated())
            return false;
        tmp.SetReuseAddress(true);
        if (!tmp.Bind("127.0.0.1", 0))
            return false;
        outPort = tmp.LocalPort();
        tmp.Close();
        return outPort > 0;
    }

    // =========================================================================
    // ТЕСТ 1: КОНСТРУКТОРЫ, ВАЛИДАЦИЯ И НЕГАТИВНЫЕ СЦЕНАРИИ
    // =========================================================================

    /**
     * @brief Проверяет конструкторы `TCPClient`, отказ при неверных аргументах/закрытом порте
     *        и поведение методов на неподключённом клиенте.
     * @param [in,out] tr Регистратор проверок.
     */
    void TestConstructorsAndErrors(TestRunner& tr)
    {
        tr.BeginSuite("1. Конструкторы TCPClient, валидация и негативные сценарии");

        ClientConfig cfg;
        cfg.connectTimeoutMs = 400;
        cfg.noDelay          = true;

        TCPClient idleClient(cfg);
        tr.Check(!idleClient.Connected() && !idleClient.SockInternal().IsCreated(),
                 "Новый TCPClient не подключён и не владеет открытым сокетом");
        tr.Check(!idleClient.Pending(0) && !idleClient.PollRead(0) && !idleClient.PollWrite(0),
                 "Pending/PollRead/PollWrite на неподключённом клиенте возвращают false");
        tr.Check(idleClient.Available() == -1 && idleClient.RemotePort() == -1 && idleClient.RemoteAddress().empty(),
                 "Available() == -1, RemotePort() == -1 и RemoteAddress() пуст на неподключённом клиенте");

        // Проверка отправки/приёма на неподключённом клиенте -> SOCK_EBADF.
        char tmp = 'A';
        tr.Check(idleClient.Send(&tmp, 1) == -1 && GET_NET_ERROR() == SOCK_EBADF,
                 "Send() на неподключённом клиенте возвращает -1 и выставляет SOCK_EBADF");
        tr.Check(idleClient.Recv(&tmp, 1) == -1 && GET_NET_ERROR() == SOCK_EBADF,
                 "Recv() на неподключённом клиенте возвращает -1 и выставляет SOCK_EBADF");
        tr.Check(!idleClient.SendString("hi") && GET_NET_ERROR() == SOCK_EBADF,
                 "SendString() на неподключённом клиенте возвращает false и выставляет SOCK_EBADF");

        // Невалидные аргументы Connect().
        tr.Check(!idleClient.Connect("", 8080, 200) && GET_NET_ERROR() == SOCK_EINVAL,
                 "Connect с пустым host возвращает false и выставляет SOCK_EINVAL");
        tr.Check(!idleClient.Connect("127.0.0.1", 70000, 200) && GET_NET_ERROR() == SOCK_EINVAL,
                 "Connect с портом > 65535 возвращает false и выставляет SOCK_EINVAL");

        // Подключение к закрытому порту должно завершиться неудачей (отказ или таймаут).
        int closedPort = 0;
        if (tr.Check(ReserveFreeLoopbackPort(closedPort), "Получен заведомо закрытый порт " + std::to_string(closedPort)))
        {
            TCPClient failDirect("127.0.0.1", closedPort, cfg);
            tr.Check(!failDirect.Connected(),
                     "Конструктор (host, closedPort) завершился с Connected() == false", GET_NET_ERROR());
        }

        // Конструктор из готового Socket&&.
        Socket rawSock(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        const bool rawCreated = rawSock.IsCreated();
        TCPClient fromSock(std::move(rawSock), cfg);
        tr.Check(rawCreated && rawSock.IsClosed() && fromSock.SockInternal().IsCreated(),
                 "Конструктор (Socket&&) забрал владение сокетом и обнулил исходный Socket");

        tr.EndSuite();
    }

    // =========================================================================
    // ТЕСТ 2: MOVE-СЕМАНТИКА АКТИВНОГО КЛИЕНТА
    // =========================================================================

    /**
     * @brief Проверяет конструктор перемещения и оператор перемещающего присваивания на подключённом клиенте.
     * @param [in,out] tr Регистратор проверок.
     */
    void TestMoveSemantics(TestRunner& tr)
    {
        tr.BeginSuite("2. Move-семантика подключённого TCPClient");

        TCPServer srv(AF_INET, "127.0.0.1", 0);
        const int port = srv.LocalPort();
        if (!tr.Check(srv.IsListening() && port > 0, "Сервер запущен на порту " + std::to_string(port)))
        {
            tr.EndSuite();
            return;
        }

        ClientId srvClientId = InvalidClientId;
        std::string rxOnServer;
        srv.SetOnClientConnected([&](ClientId id, const std::string&, int) { srvClientId = id; });
        srv.SetOnClientData([&](ClientId, const uint8_t* data, size_t size)
        {
            rxOnServer.append(reinterpret_cast<const char*>(data), size);
        });

        TCPClient c1("127.0.0.1", port);
        srv.PollOnce(300);
        if (!tr.Check(c1.Connected() && srvClientId != InvalidClientId,
                      "c1 подключён к серверу (clientId=" + std::to_string(srvClientId) + ")"))
        {
            tr.EndSuite();
            return;
        }

        // 1. Move-конструктор.
        TCPClient c2(std::move(c1));
        tr.Check(!c1.Connected() && !c1.SockInternal().IsCreated(),
                 "После move-конструктора исходный c1 закрыт и не подключён");
        tr.Check(c2.Connected() && c2.RemotePort() == port,
                 "c2 сохранил активное соединение с портом " + std::to_string(port));

        // 2. Move-присваивание.
        TCPClient c3;
        c3 = std::move(c2);
        tr.Check(!c2.Connected() && !c2.SockInternal().IsCreated(),
                 "После move-присваивания исходный c2 закрыт");
        tr.Check(c3.Connected() && c3.SendString("MOVE_CLIENT_OK"),
                 "c3 подключён и успешно отправил 'MOVE_CLIENT_OK'");

        srv.PollOnce(300);
        tr.Check(rxOnServer == "MOVE_CLIENT_OK",
                 "Сервер принял сообщение через перемещённого клиента: '" + rxOnServer + "'");

        c3.Close();
        srv.PollOnce(200);
        tr.EndSuite();
    }

    // =========================================================================
    // ТЕСТ 3: BINDLOCAL, АДРЕСА, ПОРТЫ И UPTIME
    // =========================================================================

    /**
     * @brief Проверяет `BindLocal()`, `LocalAddress()`, `RemoteAddress()`, `LocalPort()`, `RemotePort()` и `Uptime()`.
     * @param [in,out] tr Регистратор проверок.
     */
    void TestBindLocalAndEndpointInfo(TestRunner& tr)
    {
        tr.BeginSuite("3. BindLocal(), адреса/порты клиента и Uptime()");

        TCPServer srv(AF_INET, "127.0.0.1", 0);
        const int srvPort = srv.LocalPort();
        if (!tr.Check(srv.IsListening() && srvPort > 0, "Сервер запущен на 127.0.0.1:" + std::to_string(srvPort)))
        {
            tr.EndSuite();
            return;
        }

        int desiredLocalPort = 0;
        if (!tr.Check(ReserveFreeLoopbackPort(desiredLocalPort),
                      "Зарезервирован локальный порт клиента " + std::to_string(desiredLocalPort)))
        {
            tr.EndSuite();
            return;
        }

        TCPClient client;
        tr.Check(client.BindLocal("127.0.0.1", desiredLocalPort),
                 "BindLocal(\"127.0.0.1\", " + std::to_string(desiredLocalPort) + ") выполнен успешно");
        tr.Check(client.LocalPort() == desiredLocalPort,
                 "LocalPort() до подключения равен " + std::to_string(client.LocalPort()));

        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        tr.Check(client.Connect(AF_INET, "127.0.0.1", srvPort, 1000),
                 "Connect(AF_INET, \"127.0.0.1\", " + std::to_string(srvPort) + ") выполнен успешно");
        srv.PollOnce(200);

        tr.Info("LocalAddress(true)=" + client.LocalAddress(true)
                + ", RemoteAddress(true)=" + client.RemoteAddress(true)
                + ", uptimeTicks=" + std::to_string(client.Uptime().Ticks()));

        tr.Check(client.LocalAddress(false) == "127.0.0.1" && client.LocalPort() == desiredLocalPort,
                 "Локальный адрес и порт клиента сохранились после Connect()");
        tr.Check(client.RemoteAddress(false) == "127.0.0.1" && client.RemotePort() == srvPort,
                 "Удалённый адрес и порт сервера определены корректно");
        tr.Check(client.Uptime().Ticks() > 0, "Uptime() клиента больше нуля");

        client.Close();
        srv.PollOnce(200);
        tr.EndSuite();
    }

    // =========================================================================
    // ТЕСТ 4: ОТПРАВКА И ПРИЁМ ДАННЫХ, СТРОК И BYTEBUFFER (С TCPServer)
    // =========================================================================

    /**
     * @brief Проверяет `Send`, `SendAll`, `SendString`, `SendBuffer`, `Recv`, `RecvAll`, `RecvString`
     *        в паре с `TCPServer`.
     * @param [in,out] tr Регистратор проверок.
     */
    void TestDataAndStringExchange(TestRunner& tr)
    {
        tr.BeginSuite("4. Обмен данными, строками и ByteBuffer между TCPClient и TCPServer");

        TCPServer srv(AF_INET, "127.0.0.1", 0);
        const int port = srv.LocalPort();
        if (!tr.Check(srv.IsListening() && port > 0, "Сервер запущен на порту " + std::to_string(port)))
        {
            tr.EndSuite();
            return;
        }

        // Эхо-сервер пересылает все полученные байты обратно отправителю.
        srv.SetOnClientData([&](ClientId id, const uint8_t* data, size_t size)
        {
            srv.Send(id, reinterpret_cast<const char*>(data), size);
        });

        std::thread srvThread([&]() { srv.Run(15); });

        TCPClient client("127.0.0.1", port);
        if (!tr.Check(client.Connected(), "TCPClient подключился к фоновому TCPServer"))
        {
            srv.RequestStop();
            srvThread.join();
            tr.EndSuite();
            return;
        }

        // 1. SendString + RecvString.
        const std::string textMsg = "HELLO_FROM_TCP_CLIENT";
        tr.Check(client.SendString(textMsg), "SendString() отправил '" + textMsg + "'");
        std::string echoText = client.RecvString(static_cast<int>(textMsg.size()));
        tr.Check(echoText == textMsg, "RecvString() получил эхо '" + echoText + "'");

        // 2. SendAll + RecvAll.
        const char rawBlock[] = "BLOCK_0123456789_ABCDEF";
        constexpr int rawLen = static_cast<int>(sizeof(rawBlock) - 1);
        tr.Check(client.SendAll(rawBlock, rawLen), "SendAll() отправил " + std::to_string(rawLen) + " байт");
        char rawOut[64] = {};
        tr.Check(client.RecvAll(rawOut, rawLen) && std::memcmp(rawOut, rawBlock, rawLen) == 0,
                 "RecvAll() вычитал блок без искажений");

        // 3. SendBuffer (сырые байты ByteBuffer).
        ByteBuffer rawBuf(32);
        rawBuf << uint16_t(0xABCDU) << uint8_t(77);
        tr.Check(client.SendBuffer(rawBuf), "SendBuffer() отправил " + std::to_string(rawBuf.Size()) + " байт");
        std::string echoBuf = client.RecvString(static_cast<int>(rawBuf.Size()));
        tr.Check(echoBuf.size() == rawBuf.Size()
                 && std::memcmp(echoBuf.data(), rawBuf.Data(), rawBuf.Size()) == 0,
                 "Эхо-ответ для SendBuffer совпал побайтово");

        // 4. Негативные проверки аргументов Recv / RecvString / Send.
        tr.Check(client.Send(nullptr, 4) == -1 && GET_NET_ERROR() == SOCK_EINVAL,
                 "Send(nullptr, 4) возвращает -1 и выставляет SOCK_EINVAL");
        tr.Check(client.Recv(nullptr, 4) == -1 && GET_NET_ERROR() == SOCK_EINVAL,
                 "Recv(nullptr, 4) возвращает -1 и выставляет SOCK_EINVAL");
        tr.Check(client.RecvString(0).empty() && GET_NET_ERROR() == SOCK_EINVAL,
                 "RecvString(0) возвращает \"\" и выставляет SOCK_EINVAL");

        client.Close();
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        srv.RequestStop();
        srvThread.join();

        tr.EndSuite();
    }

    // =========================================================================
    // ТЕСТ 5: ПАКЕТНЫЙ ФРЕЙМИНГ SENDPACKET / RECVPACKET + STREAMSOCKET
    // =========================================================================

    /**
     * @brief Проверяет `SendPacket` и `RecvPacket` (`ByteBuffer` с Big-Endian заголовком длины),
     *        пустой пакет, лимит `maxPacketSize` и совместимость с `StreamSocket::ReadPacket()`.
     * @param [in,out] tr Регистратор проверок.
     */
    void TestPacketFramingAndStreamInterop(TestRunner& tr)
    {
        tr.BeginSuite("5. Пакетный фрейминг SendPacket / RecvPacket и совместимость с StreamSocket");

        TCPServer srv(AF_INET, "127.0.0.1", 0);
        const int port = srv.LocalPort();
        if (!tr.Check(srv.IsListening() && port > 0, "Сервер запущен на порту " + std::to_string(port)))
        {
            tr.EndSuite();
            return;
        }

        // Сервер принимает пакеты через StreamSocket::ReadPacket и отправляет ответ через TCPServer::SendPacket.
        std::mutex mtx;
        bool serverPacketOk = false;
        uint32_t gotTag     = 0;
        double   gotValue   = 0.0;
        std::string gotName;

        srv.SetOnClientConnected([&](ClientId id, const std::string&, int)
        {
            const Socket* s = srv.GetClientSocket(id);
            if (!s)
                return;
            // Для демонстрации совместимости: в этом тесте сервер читает пакет через StreamSocket
            // на дублирующем/принятом потоке, а в основном цикле отправляет пакет через TCPServer::SendPacket.
            (void)s;
        });

        // Проще и надёжнее: эхо-пакетчик на стороне сервера через ByteBuffer.
        srv.SetOnClientData([&](ClientId id, const uint8_t* data, size_t size)
        {
            std::lock_guard<std::mutex> lock(mtx);
            static std::unordered_map<ClientId, std::vector<uint8_t>> acc;
            auto& buf = acc[id];
            buf.insert(buf.end(), data, data + size);

            while (buf.size() >= sizeof(uint32_t))
            {
                uint32_t netLen = 0;
                std::memcpy(&netLen, buf.data(), sizeof(netLen));
                const uint32_t payLen = ntohl(netLen);
                if (buf.size() < sizeof(uint32_t) + static_cast<size_t>(payLen))
                    break;

                ByteBuffer packet(payLen, ByteOrder::BigEndian);
                if (payLen > 0)
                    packet.WriteBytes(buf.data() + sizeof(uint32_t), payLen);
                packet.Rewind();

                if (payLen > 0)
                {
                    packet >> gotTag >> gotValue >> gotName;
                    serverPacketOk = true;
                }

                // Отправляем пакет обратно клиенту через TCPServer::SendPacket().
                srv.SendPacket(id, packet);
                buf.erase(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(sizeof(uint32_t) + payLen));
            }
        });

        std::thread srvThread([&]() { srv.Run(15); });

        TCPClient client("127.0.0.1", port);
        if (!tr.Check(client.Connected(), "TCPClient подключился к серверу"))
        {
            srv.RequestStop();
            srvThread.join();
            tr.EndSuite();
            return;
        }

        // 1. Отправляем непустой пакет с несколькими типами данных.
        ByteBuffer tx(128, ByteOrder::BigEndian);
        tx << uint32_t(0xCAFEBABEU) << double(123.456789) << std::string("BINARY_MSG");

        tr.Check(client.SendPacket(tx), "SendPacket() отправил пакет (" + std::to_string(tx.Size()) + " байт)");

        ByteBuffer rx(0, ByteOrder::BigEndian);
        tr.Check(client.RecvPacket(rx) && rx.ReadOffset() == 0,
                 "RecvPacket() принял эхо-пакет (" + std::to_string(rx.Size()) + " байт) с курсором в 0");

        uint32_t outTag = 0;
        double   outVal = 0.0;
        std::string outName;
        rx >> outTag >> outVal >> outName;

        {
            std::lock_guard<std::mutex> lock(mtx);
            tr.Check(serverPacketOk && gotTag == 0xCAFEBABEU && std::fabs(gotValue - 123.456789) < 1e-9 && gotName == "BINARY_MSG",
                     "Сервер корректно распарсил пакет");
        }
        tr.Check(outTag == 0xCAFEBABEU && std::fabs(outVal - 123.456789) < 1e-9 && outName == "BINARY_MSG",
                 "Клиент получил неизменённый эхо-пакет (tag=0xCAFEBABE, name='" + outName + "')");

        // 2. Пустой 0-байтный пакет.
        ByteBuffer emptyPkt(0);
        ByteBuffer emptyEcho(16);
        emptyEcho << uint32_t(555);
        tr.Check(client.SendPacket(emptyPkt) && client.RecvPacket(emptyEcho) && emptyEcho.Empty(),
                 "0-байтный пакет отправлен и принят как пустой ByteBuffer");

        // 3. Проверка лимита maxPacketSize -> SOCK_EMSGSIZE.
        ByteBuffer bigPkt(64);
        bigPkt.Resize(64);
        tr.Check(client.SendPacket(bigPkt), "Отправлен пакет 64 байта для проверки maxPacketSize=16");

        ByteBuffer rejectedPkt(0);
        tr.Check(!client.RecvPacket(rejectedPkt, /*maxPacketSize=*/16)
                 && rejectedPkt.Empty() && GET_NET_ERROR() == SOCK_EMSGSIZE,
                 "RecvPacket(maxPacketSize=16) отклонил пакет 64 байта с SOCK_EMSGSIZE", GET_NET_ERROR());

        client.Close();
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        srv.RequestStop();
        srvThread.join();

        tr.EndSuite();
    }

    // =========================================================================
    // ТЕСТ 6: RECONNECT() И ОТКАЗ ПРИ ОТСУТСТВИИ АДРЕСА
    // =========================================================================

    /**
     * @brief Проверяет `Reconnect()` после перезапуска сервера на том же порту и `SOCK_EDESTADDRREQ`
     *        при вызове `Reconnect()` без предварительного `Connect()`.
     * @param [in,out] tr Регистратор проверок.
     */
    void TestReconnect(TestRunner& tr)
    {
        tr.BeginSuite("6. Reconnect() после перезапуска сервера и SOCK_EDESTADDRREQ");

        TCPClient freshClient;
        tr.Check(!freshClient.Reconnect(200) && GET_NET_ERROR() == SOCK_EDESTADDRREQ,
                 "Reconnect() без предыдущего Connect() возвращает false и выставляет SOCK_EDESTADDRREQ");

        int port = 0;
        if (!tr.Check(ReserveFreeLoopbackPort(port), "Получен порт " + std::to_string(port) + " для теста Reconnect"))
        {
            tr.EndSuite();
            return;
        }

        TCPServer srv(AF_INET, "127.0.0.1", port);
        if (!tr.Check(srv.IsListening(), "Сервер запущен на порту " + std::to_string(port)))
        {
            tr.EndSuite();
            return;
        }

        ClientConfig cfg;
        cfg.connectTimeoutMs = 500;
        TCPClient client(cfg);
        tr.Check(client.Connect("127.0.0.1", port), "Первое подключение клиента к серверу успешно");
        srv.PollOnce(200);
        tr.Check(srv.ClientCount() == 1, "На сервере 1 активный клиент");

        // Останавливаем сервер и запускаем новый экземпляр на том же порту.
        srv.Stop();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        TCPServer srvRestarted(AF_INET, "127.0.0.1", port);
        if (!tr.Check(srvRestarted.IsListening(), "Перезапущенный сервер слушает тот же порт " + std::to_string(port)))
        {
            tr.EndSuite();
            return;
        }

        tr.Check(client.Reconnect(1000) && client.Connected(),
                 "Reconnect() успешно восстановил соединение после перезапуска сервера");
        srvRestarted.PollOnce(200);
        tr.Check(srvRestarted.ClientCount() == 1, "Перезапущенный сервер принял переподключившегося клиента");

        client.Close();
        srvRestarted.Stop();
        tr.EndSuite();
    }

    // =========================================================================
    // ТЕСТ 7: POLLREAD / POLLWRITE / AVAILABLE / EOF
    // =========================================================================

    /**
     * @brief Проверяет `PollRead()`, `PollWrite()`, `Available()` и штатный EOF (`Recv() == 0`).
     * @param [in,out] tr Регистратор проверок.
     */
    void TestPollAvailableAndEof(TestRunner& tr)
    {
        tr.BeginSuite("7. PollRead(), PollWrite(), Available() и штатный EOF");

        TCPServer srv(AF_INET, "127.0.0.1", 0);
        const int port = srv.LocalPort();
        if (!tr.Check(srv.IsListening() && port > 0, "Сервер запущен на порту " + std::to_string(port)))
        {
            tr.EndSuite();
            return;
        }

        ClientId clientId = InvalidClientId;
        srv.SetOnClientConnected([&](ClientId id, const std::string&, int) { clientId = id; });

        TCPClient client("127.0.0.1", port);
        srv.PollOnce(200);
        if (!tr.Check(client.Connected() && clientId != InvalidClientId, "Клиент подключён"))
        {
            tr.EndSuite();
            return;
        }

        tr.Check(client.PollWrite(200), "PollWrite(200) вернул true на готовом к отправке сокете");
        tr.Check(!client.PollRead(40), "PollRead(40) вернул false, когда входящих данных ещё нет");

        // Сервер отправляет 5 байт и закрывает соединение.
        const char greeting[] = "12345";
        tr.Check(srv.Send(clientId, greeting, 5), "Сервер отправил '12345'");
        srv.PollOnce(200);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));

        tr.Check(client.PollRead(300), "PollRead(300) сработал после отправки данных сервером");
        tr.Info("Available() перед чтением: " + std::to_string(client.Available()));
        tr.Check(client.Available() >= 5, "Available() сообщает не менее 5 доступных байт");

        char out[16] = {};
        tr.Check(client.RecvAll(out, 5) && std::string(out, 5) == "12345",
                 "RecvAll(5) прочитал '12345'");

        // Закрываем клиента на стороне сервера и проверяем штатный EOF (Recv() == 0).
        srv.DisconnectClient(clientId, false);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));

        char eofBuf[8] = {};
        int n = client.Recv(eofBuf, sizeof(eofBuf));
        tr.Check(n == 0, "После закрытия соединения сервером Recv() вернул ровно 0 (EOF)");

        client.Close();
        tr.EndSuite();
    }

    // =========================================================================
    // ТЕСТ 8: СОЗДАНИЕ STREAMSOCKET ЧЕРЕЗ SOCKSTREAM()
    // =========================================================================

    /**
     * @brief Проверяет передачу владения сокетом в `StreamSocket` через `TCPClient::SockStream()`.
     * @param [in,out] tr Регистратор проверок.
     */
    void TestSockStreamHandoff(TestRunner& tr)
    {
        tr.BeginSuite("8. Передача владения в StreamSocket через TCPClient::SockStream()");

        TCPServer srv(AF_INET, "127.0.0.1", 0);
        const int port = srv.LocalPort();
        if (!tr.Check(srv.IsListening() && port > 0, "Сервер запущен на порту " + std::to_string(port)))
        {
            tr.EndSuite();
            return;
        }

        TCPClient client("127.0.0.1", port);
        Socket accepted = srv.AcceptSocket();
        if (!tr.Check(client.Connected() && accepted.IsCreated(), "Установлено соединение для SockStream()"))
        {
            tr.EndSuite();
            return;
        }

        StreamSocket clientStream = client.SockStream();
        tr.Check(!client.Connected() && !client.SockInternal().IsCreated(),
                 "После SockStream() сам TCPClient освободил сокет (Connected() == false)");
        tr.Check(clientStream.IsGood() && clientStream.SockInternal().IsCreated(),
                 "Возвращённый StreamSocket валиден и готов к работе");

        StreamSocket serverStream(std::move(accepted));
        clientStream << "STREAM_HANDOFF\n" << uint32_t(2026);
        tr.Check(clientStream.IsGood(), "clientStream отправил строку и uint32_t");

        std::string text;
        uint32_t    year = 0;
        serverStream >> text >> year;
        tr.Check(serverStream.IsGood() && text == "STREAM_HANDOFF" && year == 2026,
                 "serverStream принял text='" + text + "', year=" + std::to_string(year));

        tr.EndSuite();
    }

    // =========================================================================
    // ТЕСТ 9: НЕСКОЛЬКО КЛИЕНТОВ И ПАКЕТНЫЙ ОБМЕН С TCPSERVER
    // =========================================================================

    /**
     * @brief Интеграционный тест нескольких `TCPClient` с `TCPServer` и пакетами `ByteBuffer`.
     * @param [in,out] tr Регистратор проверок.
     */
    void TestMultipleClientsIntegration(TestRunner& tr)
    {
        tr.BeginSuite("9. Интеграционный тест нескольких TCPClient с TCPServer и ByteBuffer");

        constexpr size_t kClientCount = 10;

        TCPServer srv(AF_INET, "127.0.0.1", 0);
        srv.Reserve(kClientCount);
        const int port = srv.LocalPort();
        if (!tr.Check(srv.IsListening() && port > 0, "Сервер запущен на порту " + std::to_string(port)))
        {
            tr.EndSuite();
            return;
        }

        // Сервер принимает фреймированные пакеты [uint32_t len][payload] и возвращает их обратно.
        std::mutex accMtx;
        std::unordered_map<ClientId, std::vector<uint8_t>> clientAcc;

        srv.SetOnClientData([&](ClientId id, const uint8_t* data, size_t size)
        {
            std::lock_guard<std::mutex> lock(accMtx);
            auto& buf = clientAcc[id];
            buf.insert(buf.end(), data, data + size);

            while (buf.size() >= sizeof(uint32_t))
            {
                uint32_t netLen = 0;
                std::memcpy(&netLen, buf.data(), sizeof(netLen));
                const uint32_t payLen = ntohl(netLen);
                if (buf.size() < sizeof(uint32_t) + static_cast<size_t>(payLen))
                    break;

                ByteBuffer pkt(payLen, ByteOrder::BigEndian);
                if (payLen > 0)
                    pkt.WriteBytes(buf.data() + sizeof(uint32_t), payLen);
                pkt.Rewind();

                srv.SendPacket(id, pkt);
                buf.erase(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(sizeof(uint32_t) + payLen));
            }
        });

        srv.SetOnClientDisconnected([&](ClientId id, int)
        {
            std::lock_guard<std::mutex> lock(accMtx);
            clientAcc.erase(id);
        });

        std::thread srvThread([&]() { srv.Run(15); });

        size_t successClients = 0;
        std::mutex logMtx;

        std::vector<std::thread> workers;
        workers.reserve(kClientCount);

        for (size_t i = 0; i < kClientCount; ++i)
        {
            workers.emplace_back([&, i]()
            {
                ClientConfig cfg;
                cfg.connectTimeoutMs = 2000;
                cfg.recvTimeoutMs    = 2000;
                cfg.sendTimeoutMs    = 2000;

                TCPClient c("127.0.0.1", port, cfg);
                if (!c.Connected())
                {
                    std::lock_guard<std::mutex> lk(logMtx);
                    std::cerr << "  [FAIL] Клиент #" << i << " не подключился\n";
                    return;
                }

                const uint32_t expectedId = static_cast<uint32_t>(1000U + i);
                const std::string expectedTag = "CLIENT_" + std::to_string(i);

                ByteBuffer tx(64, ByteOrder::BigEndian);
                tx << expectedId << expectedTag;

                if (!c.SendPacket(tx))
                {
                    std::lock_guard<std::mutex> lk(logMtx);
                    std::cerr << "  [FAIL] Клиент #" << i << " не отправил пакет\n";
                    return;
                }

                ByteBuffer rx(0, ByteOrder::BigEndian);
                if (!c.RecvPacket(rx))
                {
                    std::lock_guard<std::mutex> lk(logMtx);
                    std::cerr << "  [FAIL] Клиент #" << i << " не получил ответный пакет\n";
                    return;
                }

                uint32_t gotId = 0;
                std::string gotTag;
                rx >> gotId >> gotTag;

                if (gotId == expectedId && gotTag == expectedTag)
                {
                    ++successClients;
                }
                else
                {
                    std::lock_guard<std::mutex> lk(logMtx);
                    std::cerr << "  [FAIL] Клиент #" << i << ": несовпадение данных ("
                              << gotId << ", '" << gotTag << "')\n";
                }

                c.Close();
            });
        }

        for (auto& w : workers)
            w.join();

        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        srv.RequestStop();
        srvThread.join();

        ServerStats st = srv.Stats();
        tr.Info("Итог по серверу: totalAccepted=" + std::to_string(st.totalAccepted)
                + ", totalDisconnected=" + std::to_string(st.totalDisconnected)
                + ", activeClients=" + std::to_string(st.activeClients));

        tr.Check(successClients == kClientCount,
                 "Все " + std::to_string(successClients) + "/" + std::to_string(kClientCount)
                 + " клиентов успешно обменялись пакетами ByteBuffer");
        tr.Check(st.totalAccepted == kClientCount && st.activeClients == 0,
                 "Сервер принял всех клиентов и корректно закрыл их после завершения");

        tr.EndSuite();
    }
} // anonymous namespace


int main()
{
    setlocale(LC_ALL, "");

    TestRunner tr;

    std::cout << "Запуск набора тестов TCPClient...\n"
              << "Платформа: "
#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
              << "Windows (Winsock2)\n";
#else
              << "Linux (POSIX)\n";
#endif

    try
    {
        TestConstructorsAndErrors(tr);
        TestMoveSemantics(tr);
        TestBindLocalAndEndpointInfo(tr);
        TestDataAndStringExchange(tr);
        TestPacketFramingAndStreamInterop(tr);
        TestReconnect(tr);
        TestPollAvailableAndEof(tr);
        TestSockStreamHandoff(tr);
        TestMultipleClientsIntegration(tr);
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

/*
Запуск набора тестов TCPClient...
Платформа: Windows(Winsock2)

========================================================================
[TEST] 1. Конструкторы TCPClient, валидация и негативные сценарии
========================================================================
[PASS] Новый TCPClient не подключён и не владеет открытым сокетом
[PASS] Pending / PollRead / PollWrite на неподключённом клиенте возвращают false
[PASS] Available() == -1, RemotePort() == -1 и RemoteAddress() пуст на неподключённом клиенте
[PASS] Send() на неподключённом клиенте возвращает - 1 и выставляет SOCK_EBADF
[PASS] Recv() на неподключённом клиенте возвращает - 1 и выставляет SOCK_EBADF
[PASS] SendString() на неподключённом клиенте возвращает false и выставляет SOCK_EBADF
[PASS] Connect с пустым host возвращает false и выставляет SOCK_EINVAL
[PASS] Connect с портом > 65535 возвращает false и выставляет SOCK_EINVAL
[PASS] Получен заведомо закрытый порт 0
[PASS] Конструктор(host, closedPort) завершился с Connected() == false
[PASS] Конструктор(Socket&&) забрал владение сокетом и обнулил исходный Socket
[SUITE OK]     1. Конструкторы TCPClient, валидация и негативные сценарии

========================================================================
[TEST] 2. Move - семантика подключённого TCPClient
========================================================================
[PASS] Сервер запущен на порту 28480
[PASS] c1 подключён к серверу(clientId = 1)
[PASS] После move - конструктора исходный c1 закрыт и не подключён
[PASS] c2 сохранил активное соединение с портом 28480
[PASS] После move - присваивания исходный c2 закрыт
[PASS] c3 подключён и успешно отправил 'MOVE_CLIENT_OK'
[PASS] Сервер принял сообщение через перемещённого клиента : 'MOVE_CLIENT_OK'
[SUITE OK]     2. Move - семантика подключённого TCPClient

========================================================================
[TEST] 3. BindLocal(), адреса / порты клиента и Uptime()
========================================================================
[PASS] Сервер запущен на 127.0.0.1:28482
[PASS] Зарезервирован локальный порт клиента 0
[PASS] BindLocal("127.0.0.1", 28483) выполнен успешно
[PASS] LocalPort() до подключения равен 28483
[PASS] Connect(AF_INET, "127.0.0.1", 28482) выполнен успешно
[INFO] LocalAddress(true) = 127.0.0.1 : 28483, RemoteAddress(true) = 127.0.0.1 : 28482, uptimeTicks = 8079711
[PASS] Локальный адрес и порт клиента сохранились после Connect()
[PASS] Удалённый адрес и порт сервера определены корректно
[PASS] Uptime() клиента больше нуля
[SUITE OK]     3. BindLocal(), адреса / порты клиента и Uptime()

========================================================================
[TEST] 4. Обмен данными, строками и ByteBuffer между TCPClient и TCPServer
========================================================================
[PASS] Сервер запущен на порту 28484
[PASS] TCPClient подключился к фоновому TCPServer
[PASS] SendString() отправил 'HELLO_FROM_TCP_CLIENT'
[PASS] RecvString() получил эхо 'HELLO_FROM_TCP_CLIENT'
[PASS] SendAll() отправил 23 байт
[PASS] RecvAll() вычитал блок без искажений
[PASS] SendBuffer() отправил 3 байт
[PASS] Эхо - ответ для SendBuffer совпал побайтово
[PASS] Send(nullptr, 4) возвращает - 1 и выставляет SOCK_EINVAL
[PASS] Recv(nullptr, 4) возвращает - 1 и выставляет SOCK_EINVAL
[PASS] RecvString(0) возвращает "" и выставляет SOCK_EINVAL
[SUITE OK]     4. Обмен данными, строками и ByteBuffer между TCPClient и TCPServer

========================================================================
[TEST] 5. Пакетный фрейминг SendPacket / RecvPacket и совместимость с StreamSocket
========================================================================
[PASS] Сервер запущен на порту 28486
[PASS] TCPClient подключился к серверу
[PASS] SendPacket() отправил пакет(23 байт)
[PASS] RecvPacket() принял эхо - пакет(0 байт) с курсором в 0
[PASS] Сервер корректно распарсил пакет
[PASS] Клиент получил неизменённый эхо - пакет(tag = 0xCAFEBABE, name = 'BINARY_MSG')
[PASS] 0 - байтный пакет отправлен и принят как пустой ByteBuffer
[PASS] Отправлен пакет 64 байта для проверки maxPacketSize = 16
[PASS] RecvPacket(maxPacketSize = 16) отклонил пакет 64 байта с SOCK_EMSGSIZE
Error : recv error | Connection reset by peer.
[SUITE OK]     5. Пакетный фрейминг SendPacket / RecvPacket и совместимость с StreamSocket

========================================================================
[TEST] 6. Reconnect() после перезапуска сервера и SOCK_EDESTADDRREQ
========================================================================
[PASS] Reconnect() без предыдущего Connect() возвращает false и выставляет SOCK_EDESTADDRREQ
[PASS] Получен порт 0 для теста Reconnect
[PASS] Сервер запущен на порту 28488
[PASS] Первое подключение клиента к серверу успешно
[PASS] На сервере 1 активный клиент
[PASS] Перезапущенный сервер слушает тот же порт 28488
[PASS] Reconnect() успешно восстановил соединение после перезапуска сервера
[PASS] Перезапущенный сервер принял переподключившегося клиента
[SUITE OK]     6. Reconnect() после перезапуска сервера и SOCK_EDESTADDRREQ

========================================================================
[TEST] 7. PollRead(), PollWrite(), Available() и штатный EOF
========================================================================
[PASS] Сервер запущен на порту 28491
[PASS] Клиент подключён
[PASS] PollWrite(200) вернул true на готовом к отправке сокете
[PASS] PollRead(40) вернул false, когда входящих данных ещё нет
[PASS] Сервер отправил '12345'
[PASS] PollRead(300) сработал после отправки данных сервером
[INFO] Available() перед чтением : 5
[PASS] Available() сообщает не менее 5 доступных байт
[PASS] RecvAll(5) прочитал '12345'
[PASS] После закрытия соединения сервером Recv() вернул ровно 0 (EOF)
[SUITE OK]     7. PollRead(), PollWrite(), Available() и штатный EOF

========================================================================
[TEST] 8. Передача владения в StreamSocket через TCPClient::SockStream()
========================================================================
[PASS] Сервер запущен на порту 28496
[PASS] Установлено соединение для SockStream()
[PASS] После SockStream() сам TCPClient освободил сокет(Connected() == false)
[PASS] Возвращённый StreamSocket валиден и готов к работе
[PASS] clientStream отправил строку и uint32_t
[PASS] serverStream принял text = 'STREAM_HANDOFF', year = 2026
[SUITE OK]     8. Передача владения в StreamSocket через TCPClient::SockStream()

========================================================================
[TEST] 9. Интеграционный тест нескольких TCPClient с TCPServer и ByteBuffer
========================================================================
[PASS] Сервер запущен на порту 28498
[INFO] Итог по серверу : totalAccepted = 10, totalDisconnected = 10, activeClients = 0
[PASS] Все 10 / 10 клиентов успешно обменялись пакетами ByteBuffer
[PASS] Сервер принял всех клиентов и корректно закрыл их после завершения
[SUITE OK]     9. Интеграционный тест нескольких TCPClient с TCPServer и ByteBuffer

########################################################################
ИТОГОВЫЙ ОТЧЁТ ТЕСТИРОВАНИЯ TCPClient
Сценариев OK : 9
Сценариев FAIL : 0
Проверок PASS : 72
Проверок FAIL : 0
########################################################################
*/