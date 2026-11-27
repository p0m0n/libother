/**
 * @file TestTCPServer.cpp
 * @brief Комплексный набор модульных и интеграционных тестов для `other::net::sockets::TCPServer`.
 *
 * @details
 * Файл содержит точку входа `main()` и 8 тестовых сценариев, проверяющих `TCPServer`
 * на Windows (Winsock2) и Linux (POSIX):
 * 1. Конструкторы, запуск/остановка (`Start`, `Stop`, `Active`, `IsListening`, `LocalPort`, `Uptime`),
 *    негативные сценарии (`Stop` на остановленном сервере, `PollOnce` без запуска).
 * 2. Move-конструктор и move-присваивание запущенного сервера с сохранением активных клиентов.
 * 3. Ручной режим слушателя: `Pending()`, `Accept(ip, port)`, `AcceptSocket()`, `AcceptStream()`
 *    и потоковый обмен через `StreamSocket`.
 * 4. Событийный режим (`PollOnce` / `SocketSelector`): `OnClientConnected`, `OnClientData`,
 *    `GetClientSocket()`, `GetClientInfo()` (`connectedAt`, `uptime`, счётчики), `OnClientDisconnected`.
 * 5. Отправка и рассылка: адресный `Send(id, ...)` и `Broadcast(..., excludeClientId)`.
 * 6. Пакетный фрейминг `SendPacket` / `BroadcastPacket` (`ByteBuffer` с 4-байтным Big-Endian
 *    заголовком) в связке с `StreamSocket::ReadPacket()`.
 * 7. Защита и управление клиентами: лимит `maxClients` (`totalRejected`), `DisconnectClient()`,
 *    `DisconnectAll()`, а также фоновый запуск `Run()` с потокобезопасной остановкой `RequestStop()`.
 * 8. Мини-нагрузка: многоклиентский эхо-обмен с проверкой целостности данных и статистики `ServerStats`.
 *
 * Сборка (пример):
 * - Linux:   `g++ -std=c++20 -O2 -pthread TestTCPServer.cpp TCPServer.cpp StreamSocket.cpp SocketSelector.cpp ByteBuffer.cpp Socket.cpp DateTime.cpp Utils.cpp -o TestTCPServer`
 * - Windows: `cl /std:c++20 /EHsc /O2 TestTCPServer.cpp TCPServer.cpp StreamSocket.cpp SocketSelector.cpp ByteBuffer.cpp Socket.cpp DateTime.cpp Utils.cpp`
 *
 * @see TCPServer.h, SocketSelector.h, StreamSocket.h, ByteBuffer.h, Socket.h
 */

#include "TCPServer.h"

#include <chrono>
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
     */
    class TestRunner
    {
    private:
        size_t      _totalPassed    = 0; ///< Суммарное число пройденных проверок.
        size_t      _totalFailed    = 0; ///< Суммарное число проваленных проверок.
        size_t      _suitesPassed   = 0; ///< Число полностью успешных сценариев.
        size_t      _suitesFailed   = 0; ///< Число сценариев с хотя бы одной ошибкой.
        size_t      _curSuiteFailed = 0; ///< Число ошибок в текущем сценарии.
        std::string _curSuiteName;       ///< Название текущего сценария.

    public:
        /**
         * @brief Начинает новый тестовый сценарий и печатает его заголовок в консоль.
         * @param [in] name Название сценария.
         * @throws std::bad_alloc При копировании строки названия.
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
         * @brief Завершает текущий сценарий и выводит его итоговый статус.
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
         * @brief Выводит информационное сообщение о ходе проверки.
         * @param [in] message Текст сообщения.
         */
        void Info(const std::string& message) const
        {
            std::cout << "  [INFO] " << message << '\n';
        }

        /**
         * @brief Проверяет условие и печатает `[PASS]` или `[FAIL]` с кодом ошибки при сбое.
         * @param [in] condition Проверяемое условие.
         * @param [in] message   Описание проверки.
         * @param [in] errCode   Необязательный явный код ошибки (`0` — взять из `GET_NET_ERROR()`).
         * @return Значение `condition`.
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
         * @brief Печатает итоговый отчёт и возвращает код выхода процесса.
         * @return `0` при полном успехе; `1` при наличии ошибок.
         */
        int PrintSummary() const
        {
            std::cout << "\n########################################################################\n"
                      << "  ИТОГОВЫЙ ОТЧЁТ ТЕСТИРОВАНИЯ TCPServer\n"
                      << "  Сценариев OK:    " << _suitesPassed << '\n'
                      << "  Сценариев FAIL:  " << _suitesFailed << '\n'
                      << "  Проверок PASS:   " << _totalPassed << '\n'
                      << "  Проверок FAIL:   " << _totalFailed << '\n'
                      << "########################################################################\n";

            return (_totalFailed == 0 && _suitesFailed == 0) ? 0 : 1;
        }
    };

    /**
     * @brief Создаёт и подключает локальный клиентский TCP-сокет к `127.0.0.1:port`.
     * @param [in]  port      Порт сервера.
     * @param [out] outClient Подключённый клиентский `Socket`.
     * @return `true` при успехе; `false` при сбое подключения.
     * @throws std::bad_alloc При нехватке памяти внутри `Socket::Connect()`.
     */
    bool ConnectLocalClient(int port, Socket& outClient)
    {
        Socket c(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (!c.IsCreated())
            return false;
        if (!c.Connect("127.0.0.1", port))
            return false;
        c.SetNoDelay(true);
        outClient = std::move(c);
        return true;
    }

    // =========================================================================
    // ТЕСТ 1: ЖИЗНЕННЫЙ ЦИКЛ, СТАРТ/СТОП И НЕГАТИВНЫЕ ПРОВЕРКИ
    // =========================================================================

    /**
     * @brief Проверяет конструкторы, `Start`, `Stop`, `Active`, `Uptime` и поведение до запуска.
     * @param [in,out] tr Регистратор проверок.
     */
    void TestLifecycleAndErrors(TestRunner& tr)
    {
        tr.BeginSuite("1. Жизненный цикл TCPServer (Start/Stop/Active/Uptime) и негативные проверки");

        TCPServer idleServer;
        tr.Check(!idleServer.IsListening() && !idleServer.Active(),
                 "Новый TCPServer не запущен (IsListening() == false, Active() == false)");
        tr.Check(idleServer.PollOnce(0) == -1 && GET_NET_ERROR() == SOCK_EBADF,
                 "PollOnce(0) на незапущенном сервере возвращает -1 и выставляет SOCK_EBADF");

        // Конструктор с немедленным запуском на эфемерном порту (0).
        TCPServer boundServer(AF_INET, "127.0.0.1", 0);
        const int port = boundServer.LocalPort();
        tr.Check(boundServer.IsListening() && boundServer.Active() && port > 0,
                 "Конструктор (AF_INET, \"127.0.0.1\", 0) запустил сервер на порту " + std::to_string(port));
        tr.Check(!boundServer.LocalAddress(false).empty() && boundServer.LocalAddress(true).find(':') != std::string::npos,
                 "LocalAddress: ip='" + boundServer.LocalAddress(false) + "', withPort='" + boundServer.LocalAddress(true) + "'");

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        tr.Check(boundServer.Uptime().Ticks() > 0 && boundServer.StartTime().Ticks() > 0,
                 "Uptime() и StartTime() корректно фиксируют время работы сервера");

        // Повторный Start() на другой порт должен корректно перезапустить сервер.
        tr.Check(boundServer.Start(0, "127.0.0.1"),
                 "Повторный Start(0, \"127.0.0.1\") перезапустил сервер на новом порту " + std::to_string(boundServer.LocalPort()));

        // Остановка Stop() и повторный Stop().
        tr.Check(boundServer.Stop() && !boundServer.IsListening() && !boundServer.Active(),
                 "Stop() остановил сервер и сбросил IsListening()/Active()");
        tr.Check(boundServer.Stop(), "Повторный Stop() на остановленном сервере безопасен и возвращает true");

        tr.EndSuite();
    }

    // =========================================================================
    // ТЕСТ 2: MOVE-СЕМАНТИКА ЗАПУЩЕННОГО СЕРВЕРА
    // =========================================================================

    /**
     * @brief Проверяет перемещение активного сервера с подключённым клиентом.
     * @param [in,out] tr Регистратор проверок.
     */
    void TestMoveSemantics(TestRunner& tr)
    {
        tr.BeginSuite("2. Move-семантика активного TCPServer");

        TCPServer srv(AF_INET, "127.0.0.1", 0);
        const int port = srv.LocalPort();
        if (!tr.Check(srv.IsListening() && port > 0, "Сервер запущен на порту " + std::to_string(port)))
        {
            tr.EndSuite();
            return;
        }

        Socket client;
        tr.Check(ConnectLocalClient(port, client), "Клиент подключился до перемещения сервера");

        ClientId connectedId = InvalidClientId;
        srv.SetOnClientConnected([&](ClientId id, const std::string&, int) { connectedId = id; });
        srv.PollOnce(200);
        tr.Check(connectedId != InvalidClientId && srv.HasClient(connectedId),
                 "Клиент зарегистрирован в исходном сервере с id=" + std::to_string(connectedId));

        TCPServer moved(std::move(srv));
        tr.Check(!srv.IsListening() && srv.ClientCount() == 0,
                 "Исходный сервер после move-конструктора остановлен и пуст");
        tr.Check(moved.IsListening() && moved.HasClient(connectedId) && moved.ClientCount() == 1,
                 "Перемещённый сервер сохранил прослушивание и активного клиента #" + std::to_string(connectedId));

        // Проверяем приём данных через перемещённый сервер.
        std::string received;
        moved.SetOnClientData([&](ClientId, const uint8_t* data, size_t size)
        {
            received.append(reinterpret_cast<const char*>(data), size);
        });

        const char msg[] = "AFTER_MOVE";
        tr.Check(client.SendAll(msg, static_cast<int>(sizeof(msg) - 1)), "Клиент отправил 'AFTER_MOVE'");
        moved.PollOnce(500);
        tr.Check(received == "AFTER_MOVE", "Перемещённый сервер принял данные: '" + received + "'");

        tr.EndSuite();
    }

    // =========================================================================
    // ТЕСТ 3: РУЧНОЙ РЕЖИМ СЛУШАТЕЛЯ (PENDING, ACCEPT, ACCEPTSTREAM)
    // =========================================================================

    /**
     * @brief Проверяет ручной режим работы (`Pending`, `Accept(ip, port)`, `AcceptStream` + `StreamSocket`).
     * @param [in,out] tr Регистратор проверок.
     */
    void TestManualAcceptAndStream(TestRunner& tr)
    {
        tr.BeginSuite("3. Ручной режим слушателя (Pending, Accept, AcceptStream + StreamSocket)");

        TCPServer srv(AF_INET, "127.0.0.1", 0);
        const int port = srv.LocalPort();
        if (!tr.Check(srv.IsListening() && port > 0, "Сервер запущен на порту " + std::to_string(port)))
        {
            tr.EndSuite();
            return;
        }

        tr.Check(!srv.Pending(0), "Pending(0) возвращает false при пустой очереди подключений");

        Socket rawClient;
        tr.Check(ConnectLocalClient(port, rawClient), "Клиент подключился к серверу");
        tr.Check(srv.Pending(500), "Pending(500) обнаружил входящее подключение");

        StreamSocket serverStream = srv.AcceptStream();
        if (!tr.Check(serverStream.SockInternal().IsCreated() && serverStream.IsGood(),
                      "AcceptStream() вернул валидный StreamSocket"))
        {
            tr.EndSuite();
            return;
        }

        StreamSocket clientStream(std::move(rawClient));
        clientStream << "HELLO_STREAM\n" << int32_t(12345);
        tr.Check(clientStream.IsGood(), "clientStream отправил строку и число");

        std::string line;
        int32_t     num = 0;
        serverStream >> line >> num;
        tr.Check(serverStream.IsGood() && line == "HELLO_STREAM" && num == 12345,
                 "serverStream принял line='" + line + "', num=" + std::to_string(num));

        // Проверяем Accept(ip, port) на втором клиенте.
        Socket rawClient2;
        tr.Check(ConnectLocalClient(port, rawClient2), "Второй клиент подключился");
        std::string clientIp;
        int         clientPort = 0;
        Socket      accepted2  = srv.Accept(clientIp, clientPort);
        tr.Check(accepted2.IsCreated() && !clientIp.empty() && clientPort > 0,
                 "Accept(ip, port) вернул клиента " + clientIp + ":" + std::to_string(clientPort));

        tr.EndSuite();
    }

    // =========================================================================
    // ТЕСТ 4: СОБЫТИЙНЫЙ ЦИКЛ, GETCLIENTSOCKET, GETCLIENTINFO И ОТКЛЮЧЕНИЕ
    // =========================================================================

    /**
     * @brief Проверяет колбэки Event Loop, `GetClientSocket()`, `GetClientInfo()` и уведомление об отключении.
     * @param [in,out] tr Регистратор проверок.
     */
    void TestEventLoopClientInfoAndDisconnect(TestRunner& tr)
    {
        tr.BeginSuite("4. Event Loop (PollOnce), GetClientSocket(), GetClientInfo() и отключение");

        TCPServer srv(AF_INET, "127.0.0.1", 0);
        const int port = srv.LocalPort();
        if (!tr.Check(srv.IsListening() && port > 0, "Сервер запущен на порту " + std::to_string(port)))
        {
            tr.EndSuite();
            return;
        }

        ClientId    connectedId    = InvalidClientId;
        std::string connectedIp;
        int         connectedPort  = 0;
        ClientId    disconnectedId = InvalidClientId;
        int         disconnectErr  = -1;
        std::string rxData;

        srv.SetOnClientConnected([&](ClientId id, const std::string& ip, int p)
        {
            connectedId   = id;
            connectedIp   = ip;
            connectedPort = p;
        });

        srv.SetOnClientData([&](ClientId, const uint8_t* data, size_t size)
        {
            rxData.append(reinterpret_cast<const char*>(data), size);
        });

        srv.SetOnClientDisconnected([&](ClientId id, int err)
        {
            disconnectedId = id;
            disconnectErr  = err;
        });

        Socket client;
        tr.Check(ConnectLocalClient(port, client), "Клиент подключился");
        srv.PollOnce(500);

        tr.Check(connectedId != InvalidClientId && !connectedIp.empty() && connectedPort > 0,
                 "OnClientConnected: id=" + std::to_string(connectedId)
                 + ", addr=" + connectedIp + ":" + std::to_string(connectedPort));

        // Проверяем GetClientSocket() и GetClientInfo().
        const Socket* cliSock = srv.GetClientSocket(connectedId);
        tr.Check(cliSock != nullptr && cliSock->IsCreated() && cliSock->RemotePort() == connectedPort,
                 "GetClientSocket(id) вернул активный сокет клиента");

        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        const char payload[] = "CLIENT_INFO_PAYLOAD";
        client.SendAll(payload, static_cast<int>(sizeof(payload) - 1));
        srv.PollOnce(500);

        ClientInfo info;
        tr.Check(srv.GetClientInfo(connectedId, info), "GetClientInfo(id) заполнил структуру клиента");
        tr.Info("ClientInfo: id=" + std::to_string(info.id)
                + ", handle=" + std::to_string(info.handle)
                + ", rxBytes=" + std::to_string(info.bytesReceived)
                + ", connectedAtTicks=" + std::to_string(info.connectedAt.Ticks())
                + ", uptimeTicks=" + std::to_string(info.uptime.Ticks()));
        tr.Check(info.connectedAt.Ticks() > 0 && info.uptime.Ticks() > 0
                 && info.bytesReceived == sizeof(payload) - 1 && rxData == payload,
                 "connectedAt, uptime и bytesReceived корректны; данные приняты полностью");

        // Штатное закрытие клиентом -> PollOnce должен вызвать OnClientDisconnected с errorCode == 0.
        client.Close();
        for (int i = 0; i < 10 && disconnectedId == InvalidClientId; ++i)
            srv.PollOnce(100);

        tr.Check(disconnectedId == connectedId && disconnectErr == 0 && !srv.HasClient(connectedId),
                 "OnClientDisconnected сработал с errorCode=0, клиент удалён из сервера");
        tr.Check(srv.GetClientSocket(connectedId) == nullptr,
                 "GetClientSocket(id) после отключения возвращает nullptr");

        tr.EndSuite();
    }

    // =========================================================================
    // ТЕСТ 5: SEND И BROADCAST (С ИСКЛЮЧЕНИЕМ ОТПРАВИТЕЛЯ)
    // =========================================================================

    /**
     * @brief Проверяет адресный `Send()` и широковещательный `Broadcast()` с параметром `excludeClientId`.
     * @param [in,out] tr Регистратор проверок.
     */
    void TestSendAndBroadcast(TestRunner& tr)
    {
        tr.BeginSuite("5. Адресный Send() и широковещательный Broadcast() (с excludeClientId)");

        TCPServer srv(AF_INET, "127.0.0.1", 0);
        const int port = srv.LocalPort();
        if (!tr.Check(srv.IsListening() && port > 0, "Сервер запущен на порту " + std::to_string(port)))
        {
            tr.EndSuite();
            return;
        }

        std::vector<ClientId> ids;
        srv.SetOnClientConnected([&](ClientId id, const std::string&, int) { ids.push_back(id); });

        Socket c1, c2, c3;
        tr.Check(ConnectLocalClient(port, c1) && ConnectLocalClient(port, c2) && ConnectLocalClient(port, c3),
                 "Подключены 3 клиента");
        for (int i = 0; i < 10 && ids.size() < 3; ++i)
            srv.PollOnce(100);

        if (!tr.Check(ids.size() == 3 && srv.GetClientIds().size() == 3,
                      "Сервер зафиксировал всех 3 клиентов (ClientCount=" + std::to_string(srv.ClientCount()) + ")"))
        {
            tr.EndSuite();
            return;
        }

        c1.SetBlocking(false);
        c2.SetBlocking(false);
        c3.SetBlocking(false);

        // 1. Адресный Send только клиенту #2.
        tr.Check(srv.Send(ids[1], std::string("ONLY_C2")), "Send(ids[1], \"ONLY_C2\") успешен");
        tr.Check(!srv.Send(999999ULL, "NO_SUCH_CLIENT") && GET_NET_ERROR() == SOCK_ENOENT,
                 "Send на несуществующий ClientId возвращает false + SOCK_ENOENT");

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        char buf[64] = {};
        int n2 = c2.Recv(buf, sizeof(buf) - 1);
        int n1 = c1.Recv(buf, sizeof(buf) - 1);
        tr.Check(n2 == 7 && std::string(buf, static_cast<size_t>(n2)) == "ONLY_C2", "c2 получил 'ONLY_C2'");
        tr.Check(n1 < 0 && GET_NET_ERROR() == SOCK_EWOULDBLOCK, "c1 ничего не получил (EWOULDBLOCK)");

        // 2. Broadcast всем, кроме ids[0].
        const size_t delivered = srv.Broadcast(std::string("ALL_EXCEPT_C1"), /*excludeClientId=*/ids[0]);
        tr.Check(delivered == 2, "Broadcast с исключением ids[0] доставлен " + std::to_string(delivered) + "/2 клиентам");

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        std::memset(buf, 0, sizeof(buf));
        int r2 = c2.Recv(buf, sizeof(buf) - 1);
        char b2[64] = {};
        std::memcpy(b2, buf, r2 > 0 ? static_cast<size_t>(r2) : 0);

        std::memset(buf, 0, sizeof(buf));
        int r3 = c3.Recv(buf, sizeof(buf) - 1);
        char b3[64] = {};
        std::memcpy(b3, buf, r3 > 0 ? static_cast<size_t>(r3) : 0);

        int r1 = c1.Recv(buf, sizeof(buf) - 1);
        tr.Check(r2 == 13 && std::string(b2, 13) == "ALL_EXCEPT_C1"
                 && r3 == 13 && std::string(b3, 13) == "ALL_EXCEPT_C1"
                 && r1 < 0 && GET_NET_ERROR() == SOCK_EWOULDBLOCK,
                 "c2 и c3 получили 'ALL_EXCEPT_C1', а исключённый c1 не получил ничего");

        tr.EndSuite();
    }

    // =========================================================================
    // ТЕСТ 6: SENDPACKET / BROADCASTPACKET + STREAMSOCKET::READPACKET
    // =========================================================================

    /**
     * @brief Проверяет отправку пакетов `ByteBuffer` через `SendPacket` и `BroadcastPacket`
     *        с приёмом на клиентской стороне через `StreamSocket::ReadPacket()`.
     * @param [in,out] tr Регистратор проверок.
     */
    void TestPacketFramingWithStreamSocket(TestRunner& tr)
    {
        tr.BeginSuite("6. Пакетный фрейминг SendPacket / BroadcastPacket + StreamSocket::ReadPacket()");

        TCPServer srv(AF_INET, "127.0.0.1", 0);
        const int port = srv.LocalPort();
        if (!tr.Check(srv.IsListening() && port > 0, "Сервер запущен на порту " + std::to_string(port)))
        {
            tr.EndSuite();
            return;
        }

        ClientId clientId = InvalidClientId;
        srv.SetOnClientConnected([&](ClientId id, const std::string&, int) { clientId = id; });

        Socket rawClient;
        tr.Check(ConnectLocalClient(port, rawClient), "Клиент подключился");
        srv.PollOnce(500);
        if (!tr.Check(clientId != InvalidClientId, "Клиент зарегистрирован в сервере"))
        {
            tr.EndSuite();
            return;
        }

        rawClient.SetRecvTimeout(2000);
        StreamSocket clientStream(std::move(rawClient));

        ByteBuffer pkt(128, ByteOrder::BigEndian);
        pkt << uint16_t(0x1234U) << int32_t(-987654) << std::string("SERVER_PACKET");

        tr.Check(srv.SendPacket(clientId, pkt), "SendPacket() отправил пакет клиенту");
        tr.Check(srv.BroadcastPacket(pkt) == 1, "BroadcastPacket() отправил копию пакета клиенту");

        ByteBuffer rx1(0, ByteOrder::BigEndian);
        ByteBuffer rx2(0, ByteOrder::BigEndian);
        tr.Check(clientStream.ReadPacket(rx1) && clientStream.ReadPacket(rx2),
                 "StreamSocket::ReadPacket() принял оба пакета без рассинхронизации границ");

        uint16_t code = 0;
        int32_t  val  = 0;
        std::string label;
        rx1 >> code >> val >> label;
        tr.Check(code == 0x1234U && val == -987654 && label == "SERVER_PACKET" && rx1.Size() == rx2.Size(),
                 "Содержимое пакета совпало (code=0x1234, val=-987654, label='" + label + "')");

        tr.EndSuite();
    }

    // =========================================================================
    // ТЕСТ 7: ЛИМИТ MAXCLIENTS, DISCONNECTCLIENT И RUN() + REQUESTSTOP()
    // =========================================================================

    /**
     * @brief Проверяет лимит `maxClients`, `DisconnectClient()` и запуск `Run()` из другого потока с `RequestStop()`.
     * @param [in,out] tr Регистратор проверок.
     */
    void TestMaxClientsDisconnectAndRunStop(TestRunner& tr)
    {
        tr.BeginSuite("7. Лимит maxClients, DisconnectClient() и Run() + RequestStop()");

        ServerConfig cfg;
        cfg.maxClients = 2;

        TCPServer srv(AF_INET, "127.0.0.1", 0, cfg);
        const int port = srv.LocalPort();
        if (!tr.Check(srv.IsListening() && port > 0, "Сервер с maxClients=2 запущен на порту " + std::to_string(port)))
        {
            tr.EndSuite();
            return;
        }

        std::vector<ClientId> ids;
        std::mutex mtx;
        srv.SetOnClientConnected([&](ClientId id, const std::string&, int)
        {
            std::lock_guard<std::mutex> lock(mtx);
            ids.push_back(id);
        });

        // Запускаем фоновый поток с Run(), чтобы проверить RequestStop().
        std::thread loopThread([&]() { srv.Run(20); });

        Socket c1, c2, c3;
        tr.Check(ConnectLocalClient(port, c1) && ConnectLocalClient(port, c2), "2 разрешённых клиента подключены");

        for (int i = 0; i < 20; ++i)
        {
            {
                std::lock_guard<std::mutex> lock(mtx);
                if (ids.size() >= 2)
                    break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        // 3-й клиент должен быть отклонён сервером из-за maxClients = 2.
        Socket c3Try(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        c3Try.SetRecvTimeout(500);
        if (c3Try.Connect("127.0.0.1", port))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(60));
            char probe = 0;
            int n = c3Try.Recv(&probe, 1);
            tr.Check(n <= 0, "3-й клиент при maxClients=2 отклонён сервером (Recv=" + std::to_string(n) + ")");
        }
        else
        {
            tr.Check(true, "3-й клиент при maxClients=2 отклонён при подключении");
        }
        c3Try.Close();

        tr.Check(srv.Stats().totalRejected >= 1,
                 "Статистика зафиксировала отклонённые подключения: totalRejected=" + std::to_string(srv.Stats().totalRejected));

        // Потокобезопасно останавливаем Run() через RequestStop().
        srv.RequestStop();
        loopThread.join();
        tr.Check(true, "Run() корректно завершился после RequestStop()");

        // После выхода из Run() в однопоточном контексте проверяем DisconnectClient().
        ClientId targetId = InvalidClientId;
        {
            std::lock_guard<std::mutex> lock(mtx);
            if (!ids.empty())
                targetId = ids.front();
        }

        if (targetId != InvalidClientId)
        {
            tr.Check(srv.DisconnectClient(targetId, /*graceful=*/false) && !srv.HasClient(targetId),
                     "DisconnectClient(" + std::to_string(targetId) + ", graceful=false) отключил клиента");
            tr.Check(!srv.DisconnectClient(targetId, false),
                     "Повторный DisconnectClient для уже отключённого клиента вернул false");
        }

        tr.EndSuite();
    }

    // =========================================================================
    // ТЕСТ 8: МНОГОКЛИЕНТСКИЙ ЭХО-ОБМЕН И СТАТИСТИКА SERVERSTATS
    // =========================================================================

    /**
     * @brief Проверяет параллельный эхо-обмен нескольких клиентов через `TCPServer` и счётчики `ServerStats`.
     * @param [in,out] tr Регистратор проверок.
     */
    void TestMultiClientEchoAndStats(TestRunner& tr)
    {
        tr.BeginSuite("8. Многоклиентский эхо-обмен и проверка ServerStats");

        constexpr size_t kClients = 25;

        TCPServer srv(AF_INET, "127.0.0.1", 0);
        srv.Reserve(kClients);
        const int port = srv.LocalPort();
        if (!tr.Check(srv.IsListening() && port > 0, "Сервер запущен на порту " + std::to_string(port)))
        {
            tr.EndSuite();
            return;
        }

        // Эхо-сервер: при получении данных от клиента сразу отправляет их обратно этому же клиенту.
        srv.SetOnClientData([&](ClientId id, const uint8_t* data, size_t size)
        {
            srv.Send(id, reinterpret_cast<const char*>(data), size);
        });

        std::vector<Socket> clients;
        clients.reserve(kClients);
        for (size_t i = 0; i < kClients; ++i)
        {
            Socket c;
            if (!ConnectLocalClient(port, c))
            {
                tr.Check(false, "Не удалось подключить клиента #" + std::to_string(i));
                tr.EndSuite();
                return;
            }
            c.SetRecvTimeout(2000);
            clients.push_back(std::move(c));
        }

        // Запускаем обработку событий сервера в фоновом потоке на время эхо-обмена.
        std::thread serverThread([&]() { srv.Run(15); });

        size_t echoSuccess = 0;
        for (size_t i = 0; i < kClients; ++i)
        {
            std::string msg = "ECHO_MSG_" + std::to_string(i) + "_END";
            if (!clients[i].SendAll(msg.data(), static_cast<int>(msg.size())))
                continue;

            std::string reply(msg.size(), '\0');
            if (clients[i].RecvAll(reply.data(), static_cast<int>(reply.size())) && reply == msg)
                ++echoSuccess;
        }

        for (auto& c : clients)
            c.Close();

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        srv.RequestStop();
        serverThread.join();

        ServerStats st = srv.Stats();
        tr.Info("ServerStats: accepted=" + std::to_string(st.totalAccepted)
                + ", disconnected=" + std::to_string(st.totalDisconnected)
                + ", rxBytes=" + std::to_string(st.totalBytesReceived)
                + ", txBytes=" + std::to_string(st.totalBytesSent));

        tr.Check(echoSuccess == kClients,
                 "Эхо-обмен прошёл успешно для всех " + std::to_string(echoSuccess) + "/" + std::to_string(kClients) + " клиентов");
        tr.Check(st.totalAccepted == kClients && st.totalBytesReceived > 0 && st.totalBytesSent == st.totalBytesReceived,
                 "Счётчики ServerStats корректно учли подключения и объём отправленных/принятых байтов");

        tr.EndSuite();
    }
} // anonymous namespace



int main()
{
    setlocale(LC_ALL, "");

    TestRunner tr;

    std::cout << "Запуск набора тестов TCPServer...\n"
              << "Платформа: "
#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
              << "Windows (Winsock2)\n";
#else
              << "Linux (POSIX)\n";
#endif

    try
    {
        TestLifecycleAndErrors(tr);
        TestMoveSemantics(tr);
        TestManualAcceptAndStream(tr);
        TestEventLoopClientInfoAndDisconnect(tr);
        TestSendAndBroadcast(tr);
        TestPacketFramingWithStreamSocket(tr);
        TestMaxClientsDisconnectAndRunStop(tr);
        TestMultiClientEchoAndStats(tr);
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
Запуск набора тестов TCPServer...
Платформа: Windows(Winsock2)

========================================================================
[TEST] 1. Жизненный цикл TCPServer(Start / Stop / Active / Uptime) и негативные проверки
========================================================================
[PASS] Новый TCPServer не запущен(IsListening() == false, Active() == false)
[PASS] PollOnce(0) на незапущенном сервере возвращает - 1 и выставляет SOCK_EBADF
[PASS] Конструктор(AF_INET, "127.0.0.1", 0) запустил сервер на порту 27230
[PASS] LocalAddress: ip = '127.0.0.1', withPort = '127.0.0.1:27230'
[PASS] Uptime() и StartTime() корректно фиксируют время работы сервера
[PASS] Повторный Start(0, "127.0.0.1") перезапустил сервер на новом порту 27230
[PASS] Stop() остановил сервер и сбросил IsListening() / Active()
[PASS] Повторный Stop() на остановленном сервере безопасен и возвращает true
[SUITE OK]     1. Жизненный цикл TCPServer(Start / Stop / Active / Uptime) и негативные проверки

========================================================================
[TEST] 2. Move - семантика активного TCPServer
========================================================================
[PASS] Сервер запущен на порту 27232
[PASS] Клиент подключился до перемещения сервера
[PASS] Клиент зарегистрирован в исходном сервере с id = 1
[PASS] Исходный сервер после move - конструктора остановлен и пуст
[PASS] Перемещённый сервер сохранил прослушивание и активного клиента #1
[PASS] Клиент отправил 'AFTER_MOVE'
[PASS] Перемещённый сервер принял данные : 'AFTER_MOVE'
[SUITE OK]     2. Move - семантика активного TCPServer

========================================================================
[TEST] 3. Ручной режим слушателя(Pending, Accept, AcceptStream + StreamSocket)
========================================================================
[PASS] Сервер запущен на порту 27234
[PASS] Pending(0) возвращает false при пустой очереди подключений
[PASS] Клиент подключился к серверу
[PASS] Pending(500) обнаружил входящее подключение
[PASS] AcceptStream() вернул валидный StreamSocket
[PASS] clientStream отправил строку и число
[PASS] serverStream принял line = 'HELLO_STREAM', num = 12345
[PASS] Второй клиент подключился
[PASS] Accept(ip, port) вернул клиента 127.0.0.1:27236
[SUITE OK]     3. Ручной режим слушателя(Pending, Accept, AcceptStream + StreamSocket)

========================================================================
[TEST] 4. Event Loop(PollOnce), GetClientSocket(), GetClientInfo() и отключение
========================================================================
[PASS] Сервер запущен на порту 27237
[PASS] Клиент подключился
[PASS] OnClientConnected: id = 1, addr = 127.0.0.1 : 27238
[PASS] GetClientSocket(id) вернул активный сокет клиента
[PASS] GetClientInfo(id) заполнил структуру клиента
[INFO] ClientInfo : id = 1, handle = 236, rxBytes = 19, connectedAtTicks = 639311608648928441, uptimeTicks = 375077
[PASS] connectedAt, uptime и bytesReceived корректны; данные приняты полностью
[PASS] OnClientDisconnected сработал с errorCode = 0, клиент удалён из сервера
[PASS] GetClientSocket(id) после отключения возвращает nullptr
[SUITE OK]     4. Event Loop(PollOnce), GetClientSocket(), GetClientInfo() и отключение

========================================================================
[TEST] 5. Адресный Send() и широковещательный Broadcast() (с excludeClientId)
========================================================================
[PASS] Сервер запущен на порту 27239
[PASS] Подключены 3 клиента
[PASS] Сервер зафиксировал всех 3 клиентов(ClientCount = 3)
[PASS] Send(ids[1], "ONLY_C2") успешен
[PASS] Send на несуществующий ClientId возвращает false + SOCK_ENOENT
[PASS] c2 получил 'ONLY_C2'
[PASS] c1 ничего не получил(EWOULDBLOCK)
[PASS] Broadcast с исключением ids[0] доставлен 2 / 2 клиентам
[PASS] c2 и c3 получили 'ALL_EXCEPT_C1', а исключённый c1 не получил ничего
[SUITE OK]     5. Адресный Send() и широковещательный Broadcast() (с excludeClientId)

========================================================================
[TEST] 6. Пакетный фрейминг SendPacket / BroadcastPacket + StreamSocket::ReadPacket()
========================================================================
[PASS] Сервер запущен на порту 27243
[PASS] Клиент подключился
[PASS] Клиент зарегистрирован в сервере
[PASS] SendPacket() отправил пакет клиенту
[PASS] BroadcastPacket() отправил копию пакета клиенту
[PASS] StreamSocket::ReadPacket() принял оба пакета без рассинхронизации границ
[PASS] Содержимое пакета совпало(code = 0x1234, val = -987654, label = 'SERVER_PACKET')
[SUITE OK]     6. Пакетный фрейминг SendPacket / BroadcastPacket + StreamSocket::ReadPacket()

========================================================================
[TEST] 7. Лимит maxClients, DisconnectClient() и Run() + RequestStop()
========================================================================
[PASS] Сервер с maxClients = 2 запущен на порту 27245
[PASS] 2 разрешённых клиента подключены
[PASS] 3 - й клиент при maxClients = 2 отклонён сервером(Recv = 0)
[PASS] Статистика зафиксировала отклонённые подключения : totalRejected = 1
[PASS] Run() корректно завершился после RequestStop()
[PASS] DisconnectClient(1, graceful = false) отключил клиента
[PASS] Повторный DisconnectClient для уже отключённого клиента вернул false
[SUITE OK]     7. Лимит maxClients, DisconnectClient() и Run() + RequestStop()

========================================================================
[TEST] 8. Многоклиентский эхо - обмен и проверка ServerStats
========================================================================
[PASS] Сервер запущен на порту 27249
[INFO] ServerStats: accepted = 25, disconnected = 25, rxBytes = 365, txBytes = 365
[PASS] Эхо - обмен прошёл успешно для всех 25 / 25 клиентов
[PASS] Счётчики ServerStats корректно учли подключения и объём отправленных / принятых байтов
[SUITE OK]     8. Многоклиентский эхо - обмен и проверка ServerStats

########################################################################
ИТОГОВЫЙ ОТЧЁТ ТЕСТИРОВАНИЯ TCPServer
Сценариев OK : 8
Сценариев FAIL : 0
Проверок PASS : 58
Проверок FAIL : 0
########################################################################
*/
