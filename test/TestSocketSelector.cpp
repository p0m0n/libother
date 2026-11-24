#include "SocketSelector.h"

#include <chrono>
#include <cstring>
#include <deque>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>


namespace
{
    using namespace other::net::sockets;

    /**
     * @class TestRunner
     * @brief Вспомогательный регистратор проверок с подробным консольным логированием успешных и ошибочных шагов.
     *
     * Ведёт подсчёт пройденных и проваленных утверждений (как в рамках текущего сценария, так и суммарно
     * по всему запуску), печатает системный код ошибки ОС при сбоях и формирует итоговый отчёт.
     */
    class TestRunner
    {
    private:
        size_t      _totalPassed      = 0; ///< Общее число успешных проверок за весь запуск.
        size_t      _totalFailed      = 0; ///< Общее число проваленных проверок за весь запуск.
        size_t      _suitesPassed     = 0; ///< Количество полностью пройденных тестовых сценариев.
        size_t      _suitesFailed     = 0; ///< Количество сценариев с хотя бы одной ошибкой.
        size_t      _curSuiteFailed   = 0; ///< Число ошибок в текущем выполняемом сценарии.
        std::string _curSuiteName;         ///< Название текущего сценария.

    public:
        /**
         * @brief Начинает новый тестовый сценарий и выводит его заголовок в консоль.
         * @param [in] name Название тестового сценария.
         * @throws std::bad_alloc При нехватке памяти для сохранения имени сценария.
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
         * @brief Завершает текущий сценарий и выводит его итоговый статус (`OK` или `FAILED`).
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
                          << " (errors: " << _curSuiteFailed << ")\n";
            }
        }

        /**
         * @brief Выводит информационное сообщение о ходе выполнения теста.
         * @param [in] message Текст информационного сообщения.
         */
        void Info(const std::string& message) const
        {
            std::cout << "  [INFO] " << message << '\n';
        }

        /**
         * @brief Проверяет булево условие и логирует результат (`[PASS]` или `[FAIL]`).
         * @param [in] condition Проверяемое условие.
         * @param [in] message   Описание проверяемого инварианта.
         * @param [in] logOsErr  Если `true` — при провале дополнительно печатает `GET_NET_ERROR()` и его расшифровку.
         * @return Значение `condition` (`true` при успехе, `false` при провале).
         */
        bool Check(bool condition, const std::string& message, bool logOsErr = false)
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
            if (logOsErr)
            {
                const int err = GET_NET_ERROR();
                std::cerr << " | OS error=" << err << " (" << SocketErrorToString(err) << ")";
            }
            std::cerr << '\n';
            return false;
        }

        /**
         * @brief Выводит финальную сводку по всем запущенным сценариям и проверкам.
         * @return `0`, если все проверки пройдены успешно; `1`, если зафиксирована хотя бы одна ошибка.
         */
        int PrintSummary() const
        {
            std::cout << "\n########################################################################\n"
                      << "  ИТОГОВЫЙ ОТЧЁТ ТЕСТИРОВАНИЯ SocketSelector\n"
                      << "  Бэкенд ОС:       " << SocketSelector::BackendName()
                      << " (Edge-Triggered: " << (SocketSelector::SupportsEdgeTriggered() ? "да" : "нет") << ")\n"
                      << "  Сценариев OK:    " << _suitesPassed << '\n'
                      << "  Сценариев FAIL:  " << _suitesFailed << '\n'
                      << "  Проверок PASS:   " << _totalPassed << '\n'
                      << "  Проверок FAIL:   " << _totalFailed << '\n'
                      << "########################################################################\n";

            return (_totalFailed == 0 && _suitesFailed == 0) ? 0 : 1;
        }
    };

    /**
     * @brief Форматирует битовую маску `SelectorEvent::*` в читаемую строку для консольных логов.
     * @param [in] mask Битовая маска событий селектора.
     * @return Строка вида `"Read|Write"` или `"None"`.
     * @throws std::bad_alloc При нехватке памяти для формирования строки.
     */
    std::string FormatEventMask(uint32_t mask)
    {
        if (mask == SelectorEvent::None)
            return "None";

        std::string out;
        auto appendFlag = [&](uint32_t bit, const char* name)
        {
            if ((mask & bit) != 0U)
            {
                if (!out.empty())
                    out += '|';
                out += name;
            }
        };

        appendFlag(SelectorEvent::Read,    "Read");
        appendFlag(SelectorEvent::Write,   "Write");
        appendFlag(SelectorEvent::Error,   "Error");
        appendFlag(SelectorEvent::HangUp,  "HangUp");
        appendFlag(SelectorEvent::Invalid, "Invalid");
        return out;
    }

    /**
     * @brief Создаёт и запускает локальный слушающий TCP-сокет на `127.0.0.1` со свободным эфемерным портом.
     * @param [out] outServer Сюда перемещается созданный серверный сокет.
     * @param [out] outPort   Сюда записывается автоматически назначенный ОС порт.
     * @param [in]  backlog   Размер очереди ожидающих подключений (по умолчанию `SOMAXCONN`).
     * @return `true` при успехе; `false` при сбое создания, привязки или перехода в режим `Listen`.
     * @throws std::bad_alloc При нехватке памяти внутри `Socket::Bind()`.
     */
    bool CreateLoopbackTcpServer(Socket& outServer, int& outPort, int backlog = SOMAXCONN)
    {
        Socket server(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (!server.IsCreated())
            return false;

        server.SetReuseAddress(true);
        if (!server.Bind("127.0.0.1", 0))
            return false;

        int port = server.LocalPort();
        if (port <= 0)
            return false;

        if (!server.Listen(backlog))
            return false;

        outServer = std::move(server);
        outPort   = port;
        return true;
    }

    /**
     * @brief Создаёт соединённую пару локальных TCP-сокетов (`client` <-> `serverPeer`) в неблокирующем режиме.
     * @param [in]  listenSock  Активный слушающий TCP-сокет на `127.0.0.1`.
     * @param [in]  port        Порт слушающего сокета.
     * @param [out] outClient   Клиентская сторона соединения (переведена в неблокирующий режим).
     * @param [out] outAccepted Серверная сторона соединения (переведена в неблокирующий режим).
     * @return `true` при успешном соединении и принятии; `false` при ошибке.
     * @throws std::bad_alloc При нехватке памяти внутри `Socket::Connect()`.
     */
    bool CreateConnectedTcpPair(Socket& listenSock, int port, Socket& outClient, Socket& outAccepted)
    {
        Socket client(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (!client.IsCreated())
            return false;

        // Подключаемся в блокирующем режиме: ядро сразу ставит клиента в очередь listenSock.
        if (!client.Connect("127.0.0.1", port))
            return false;

        Socket accepted = listenSock.Accept();
        if (!accepted.IsCreated())
            return false;

        client.SetBlocking(false);
        accepted.SetBlocking(false);

        outClient   = std::move(client);
        outAccepted = std::move(accepted);
        return true;
    }

    // =========================================================================
    // ТЕСТ 1: ИНИЦИАЛИЗАЦИЯ, СВОЙСТВА БЭКЕНДА И БАЗОВОЕ СОСТОЯНИЕ
    // =========================================================================

    /**
     * @brief Проверяет создание селектора, метаданные бэкенда, `Reserve`, `SetMaxEventsPerPoll` и `Poll` на пустом наборе.
     * @param [in,out] tr Экземпляр `TestRunner` для логирования проверок.
     */
    void TestBackendAndInitialState(TestRunner& tr)
    {
        tr.BeginSuite("1. Инициализация, свойства бэкенда и базовое состояние");

        SocketSelector selector(64, 512);
        tr.Info(std::string("Активный бэкенд: ") + SocketSelector::BackendName()
                + ", SupportsEdgeTriggered=" + (SocketSelector::SupportsEdgeTriggered() ? "true" : "false"));

        tr.Check(selector.IsValid(), "Селектор успешно инициализирован (IsValid() == true)", true);
        tr.Check(selector.Empty(), "Новый селектор пуст (Empty() == true)");
        tr.Check(selector.Size() == 0, "Size() == 0");
        tr.Check(selector.Sockets().empty(), "Sockets() пуст");
        tr.Check(selector.ReadyEvents().empty(), "ReadyEvents() пуст");
        tr.Check(selector.MaxEventsPerPoll() == 512, "MaxEventsPerPoll() == 512");

        // Проверяем граничный случай SetMaxEventsPerPoll(0) -> автоматическое приведение к 1.
        selector.SetMaxEventsPerPoll(0);
        tr.Check(selector.MaxEventsPerPoll() == 1, "SetMaxEventsPerPoll(0) приводится к 1");

        selector.SetMaxEventsPerPoll(2048);
        tr.Check(selector.MaxEventsPerPoll() == 2048, "SetMaxEventsPerPoll(2048) применён");

        // Резервирование под большое количество соединений не должно менять Size().
        selector.Reserve(10000);
        tr.Check(selector.Empty() && selector.Size() == 0, "Reserve(10000) не меняет Size() и Empty()");

        // Poll на пустом селекторе должен мгновенно вернуть 0.
        int ready = selector.Poll(100);
        tr.Check(ready == 0, "Poll(100) на пустом селекторе мгновенно возвращает 0");

        tr.EndSuite();
    }

    // =========================================================================
    // ТЕСТ 2: НЕВАЛИДНЫЕ И ЗАКРЫТЫЕ СОКЕТЫ (НЕГАТИВНЫЕ СЦЕНАРИИ)
    // =========================================================================

    /**
     * @brief Проверяет корректную реакцию селектора на закрытые, пустые и незарегистрированные сокеты.
     * @param [in,out] tr Экземпляр `TestRunner` для логирования проверок.
     */
    void TestInvalidAndClosedSockets(TestRunner& tr)
    {
        tr.BeginSuite("2. Негативные сценарии: пустые, закрытые и отсутствующие сокеты");

        SocketSelector selector;
        Socket emptySock; // Дескриптор INVALID_SOCKET.
        Socket validSock(AF_INET, SOCK_STREAM, IPPROTO_TCP);

        tr.Check(emptySock.IsClosed(), "Пустой Socket имеет IsClosed() == true");
        tr.Check(validSock.IsCreated(), "Валидный TCP Socket создан", true);

        // Add закрытого сокета обязан вернуть false и выставить SOCK_EBADF.
        bool addedEmpty = selector.Add(emptySock, true, false);
        int errAfterAdd = GET_NET_ERROR();
        tr.Check(!addedEmpty, "Add(пустой сокет) возвращает false");
        tr.Check(errAfterAdd == SOCK_EBADF, "После Add(пустой сокет) установлен SOCK_EBADF (факт: "
                 + SocketErrorToString(errAfterAdd) + ")");
        tr.Check(selector.Size() == 0, "После неудачного Add Size() остаётся 0");

        // Modify незарегистрированного валидного сокета -> false и SOCK_ENOENT.
        bool modMissing = selector.Modify(validSock, true, true);
        int errAfterMod = GET_NET_ERROR();
        tr.Check(!modMissing, "Modify(отсутствующий сокет) возвращает false");
        tr.Check(errAfterMod == SOCK_ENOENT, "После Modify(отсутствующий сокет) установлен SOCK_ENOENT (факт: "
                 + SocketErrorToString(errAfterMod) + ")");

        // Modify закрытого сокета -> false и SOCK_EBADF.
        bool modClosed = selector.Modify(emptySock, true, false);
        tr.Check(!modClosed && GET_NET_ERROR() == SOCK_EBADF, "Modify(закрытый сокет) возвращает false + SOCK_EBADF");

        // Remove отсутствующих сокетов и INVALID_SOCKET.
        tr.Check(!selector.Remove(validSock), "Remove(отсутствующий Socket) возвращает false");
        tr.Check(!selector.Remove(emptySock), "Remove(закрытый Socket) возвращает false");
        tr.Check(!selector.Remove(INVALID_SOCKET), "Remove(INVALID_SOCKET) возвращает false");

        // Проверки Contains / GetEvents / IsReadable / IsWritable / HasError на пустом/отсутствующем сокете.
        tr.Check(!selector.Contains(emptySock), "Contains(закрытый Socket) == false");
        tr.Check(!selector.Contains(INVALID_SOCKET), "Contains(INVALID_SOCKET) == false");
        tr.Check(!selector.Contains(validSock), "Contains(отсутствующий Socket) == false");
        tr.Check(selector.GetEvents(emptySock) == SelectorEvent::None, "GetEvents(закрытый Socket) == None");
        tr.Check(!selector.IsReadable(emptySock) && !selector.IsWritable(emptySock) && !selector.HasError(emptySock),
                 "IsReadable/IsWritable/HasError для закрытого сокета возвращают false");

        tr.EndSuite();
    }

    // =========================================================================
    // ТЕСТ 3: ADD, MODIFY, REMOVE (SWAP-AND-POP), УДАЛЕНИЕ ЗАКРЫТОГО И CLEAR
    // =========================================================================

    /**
     * @brief Проверяет управление набором дескрипторов: дублирующий `Add`, `Remove(Socket)`, `Remove(socket_t)`,
     *        резервное удаление после `Close()` и полную очистку `Clear()`.
     * @param [in,out] tr Экземпляр `TestRunner` для логирования проверок.
     */
    void TestAddModifyRemoveAndClear(TestRunner& tr)
    {
        tr.BeginSuite("3. Add, Modify, Remove (swap-and-pop), удаление закрытого сокета и Clear");

        SocketSelector selector;
        Socket s1(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        Socket s2(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        Socket s3(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        Socket s4(AF_INET, SOCK_STREAM, IPPROTO_TCP);

        if (!tr.Check(s1.IsCreated() && s2.IsCreated() && s3.IsCreated() && s4.IsCreated(),
                      "Созданы 4 тестовых TCP-сокета", true))
        {
            tr.EndSuite();
            return;
        }

        const socket_t h2 = s2.SockHandle();

        tr.Check(selector.Add(s1, true, false), "Add(s1) успешен", true);
        tr.Check(selector.Add(s2, true, false), "Add(s2) успешен", true);
        tr.Check(selector.Add(s3, true, true),  "Add(s3) успешен", true);
        tr.Check(selector.Add(s4, true, false), "Add(s4) успешен", true);
        tr.Check(selector.Size() == 4 && selector.Sockets().size() == 4,
                 "Size() == 4 и Sockets().size() == 4");

        // Повторный Add того же сокета не должен создавать дубликат.
        tr.Check(selector.Add(s1, true, true), "Повторный Add(s1) обновляет маску без дублирования", true);
        tr.Check(selector.Size() == 4, "Size() остался равен 4");

        // Проверка Contains по Socket и по socket_t.
        tr.Check(selector.Contains(s1) && selector.Contains(h2) && selector.Contains(s3) && selector.Contains(s4),
                 "Contains() подтверждает наличие всех 4 сокетов");

        // Удаление элемента из середины (s2) по дескриптору -> проверяет swap-and-pop (s4 переезжает на место s2).
        tr.Check(selector.Remove(h2), "Remove(h2 по socket_t) из середины успешен");
        tr.Check(selector.Size() == 3 && !selector.Contains(s2), "После Remove(h2): Size() == 3 и s2 отсутствует");
        tr.Check(selector.Contains(s1) && selector.Contains(s3) && selector.Contains(s4),
                 "После swap-and-pop оставшиеся s1, s3, s4 по-прежнему находятся за O(1)");

        // Проверка резервного удаления, когда сокет закрыли РАНЬШЕ вызова Remove().
        s3.Close();
        tr.Check(s3.IsClosed(), "s3 закрыт до вызова Remove()");
        tr.Check(selector.Remove(s3), "Резервный Remove(закрытый s3) нашёл запись через _sockets и удалил её");
        tr.Check(selector.Size() == 2, "После удаления закрытого s3: Size() == 2 (утечки дескриптора нет)");

        // Проверяем Modify на перемещённом при swap-and-pop сокете s4.
        tr.Check(selector.Modify(s4, true, true), "Modify(s4) после swap-and-pop работает корректно", true);

        // Полная очистка Clear() и повторное использование того же селектора.
        selector.Clear();
        tr.Check(selector.Empty() && selector.Size() == 0 && selector.Sockets().empty(),
                 "Clear() полностью очистил селектор");
        tr.Check(selector.Add(s1, true, false) && selector.Size() == 1,
                 "Повторный Add(s1) после Clear() работает корректно", true);

        tr.EndSuite();
    }

    // =========================================================================
    // ТЕСТ 4: СЕМАНТИКА ПЕРЕМЕЩЕНИЯ (MOVE CONSTRUCTOR / MOVE ASSIGNMENT)
    // =========================================================================

    /**
     * @brief Проверяет корректность конструктора перемещения и перемещающего присваивания `SocketSelector`.
     * @param [in,out] tr Экземпляр `TestRunner` для логирования проверок.
     */
    void TestMoveSemantics(TestRunner& tr)
    {
        tr.BeginSuite("4. Move-семантика SocketSelector (конструктор и присваивание)");

        Socket server;
        int port = 0;
        if (!tr.Check(CreateLoopbackTcpServer(server, port), "Создан сервер на 127.0.0.1:" + std::to_string(port), true))
        {
            tr.EndSuite();
            return;
        }

        Socket client;
        Socket peer;
        if (!tr.Check(CreateConnectedTcpPair(server, port, client, peer), "Установлена TCP-пара client <-> peer", true))
        {
            tr.EndSuite();
            return;
        }

        SocketSelector original;
        tr.Check(original.Add(peer, true, false), "peer добавлен в original", true);

        // 1. Конструктор перемещения.
        SocketSelector movedCtor(std::move(original));
        tr.Check(movedCtor.IsValid() && movedCtor.Size() == 1 && movedCtor.Contains(peer),
                 "После move-конструктора movedCtor валиден и содержит peer");
        tr.Check(original.Empty(), "После move-конструктора original пуст");

        // 2. Оператор перемещающего присваивания.
        SocketSelector movedAssign;
        movedAssign = std::move(movedCtor);
        tr.Check(movedAssign.IsValid() && movedAssign.Size() == 1 && movedAssign.Contains(peer),
                 "После move-присваивания movedAssign валиден и содержит peer");

        // 3. Проверяем, что перемещённый селектор реально получает события из ядра ОС.
        const char msg[] = "MOVE_OK";
        tr.Check(client.SendAll(msg, static_cast<int>(sizeof(msg) - 1)), "client отправил 'MOVE_OK'", true);

        int ready = movedAssign.Poll(1000);
        tr.Check(ready == 1 && movedAssign.IsReadable(peer),
                 "movedAssign.Poll(1000) поймал событие Read на peer (ready=" + std::to_string(ready) + ")", true);

        char buf[32] = {};
        int n = peer.Recv(buf, static_cast<int>(sizeof(buf) - 1));
        tr.Check(n == 7 && std::string(buf, static_cast<size_t>(n)) == "MOVE_OK",
                 "peer прочитал '" + std::string(buf, n > 0 ? static_cast<size_t>(n) : 0) + "'");

        tr.EndSuite();
    }

    // =========================================================================
    // ТЕСТ 5: ТАЙМАУТЫ POLL И СБРОС СОБЫТИЙ МЕЖДУ ЭПОХАМИ
    // =========================================================================

    /**
     * @brief Проверяет точность таймаута `Poll()` на тихом сокете и сброс старых событий на следующем вызове `Poll()`.
     * @param [in,out] tr Экземпляр `TestRunner` для логирования проверок.
     */
    void TestPollTimeoutAndEpochReset(TestRunner& tr)
    {
        tr.BeginSuite("5. Таймаут Poll() и сброс старых событий между вызовами");

        Socket server;
        int port = 0;
        if (!tr.Check(CreateLoopbackTcpServer(server, port), "Создан сервер на порту " + std::to_string(port), true))
        {
            tr.EndSuite();
            return;
        }

        Socket client;
        Socket peer;
        if (!tr.Check(CreateConnectedTcpPair(server, port, client, peer), "Установлена TCP-пара", true))
        {
            tr.EndSuite();
            return;
        }

        SocketSelector selector;
        tr.Check(selector.Add(peer, true, false), "peer добавлен в селектор (только чтение)", true);

        // 1. Мгновенный Poll(0) на тихом сокете -> 0.
        int ready0 = selector.Poll(0);
        tr.Check(ready0 == 0 && selector.ReadyEvents().empty(), "Poll(0) без данных вернул 0");

        // 2. Положительный таймаут 80 мс на тихом сокете.
        using Clock = std::chrono::steady_clock;
        const auto t0 = Clock::now();
        int ready80 = selector.Poll(80);
        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count();
        tr.Info("Фактическое время ожидания Poll(80): " + std::to_string(elapsedMs) + " мс");
        tr.Check(ready80 == 0 && elapsedMs >= 50, "Poll(80) выждал таймаут и вернул 0");

        // 3. Отправляем 1 байт -> Poll должен сработать, затем после чтения и нового Poll(0) старое событие должно исчезнуть.
        const char ch = 'X';
        tr.Check(client.SendAll(&ch, 1), "Отправлен 1 байт 'X'", true);
        int readyData = selector.Poll(500);
        tr.Check(readyData == 1 && selector.IsReadable(peer), "Poll(500) зафиксировал Read");

        char recvCh = 0;
        tr.Check(peer.Recv(&recvCh, 1) == 1 && recvCh == 'X', "Байт 'X' вычитан из буфера");

        // Новая эпоха: данных больше нет, IsReadable(peer) обязан сброситься в false!
        int readyAfterDrain = selector.Poll(0);
        tr.Check(readyAfterDrain == 0 && !selector.IsReadable(peer) && selector.GetEvents(peer) == SelectorEvent::None,
                 "После нового Poll(0) флаг IsReadable(peer) сброшен в false (эпоха обновилась)");

        tr.EndSuite();
    }

    // =========================================================================
    // ТЕСТ 6: TCP ACCEPT, ДВУСТОРОННИЙ ОБМЕН И ДИНАМИЧЕСКИЙ MODIFY(WATCHWRITE)
    // =========================================================================

    /**
     * @brief Проверяет полный цикл TCP-сервера: обнаружение входящего клиента (`Accept`), двусторонний обмен
     *        и динамическое переключение флага `watchWrite` через `Modify()`.
     * @param [in,out] tr Экземпляр `TestRunner` для логирования проверок.
     */
    void TestTcpAcceptReadWriteModify(TestRunner& tr)
    {
        tr.BeginSuite("6. TCP Accept, двусторонний обмен и динамический Modify(watchWrite)");

        Socket server;
        int port = 0;
        if (!tr.Check(CreateLoopbackTcpServer(server, port), "Слушающий сервер запущен на 127.0.0.1:" + std::to_string(port), true))
        {
            tr.EndSuite();
            return;
        }
        server.SetBlocking(false);

        SocketSelector selector;
        tr.Check(selector.Add(server, true, false), "Слушающий сервер добавлен в селектор", true);

        // Клиент инициирует подключение.
        Socket client(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        tr.Check(client.Connect("127.0.0.1", port), "Клиент подключился к серверу", true);
        client.SetBlocking(false);

        // Селектор должен сигнализировать о готовности server к Accept().
        int readyAccept = selector.Poll(1000);
        tr.Check(readyAccept == 1 && selector.IsReadable(server),
                 "Poll(1000) поймал входящее подключение на слушающем сокете", true);

        Socket accepted = server.Accept();
        if (!tr.Check(accepted.IsCreated(), "server.Accept() вернул валидный клиентский сокет", true))
        {
            tr.EndSuite();
            return;
        }
        accepted.SetBlocking(false);

        // Регистрируем accepted только на чтение; проверяем, что Write пока не срабатывает.
        tr.Check(selector.Add(accepted, true, false), "accepted добавлен (watchRead=true, watchWrite=false)", true);
        int quietPoll = selector.Poll(0);
        tr.Check(quietPoll == 0 && !selector.IsWritable(accepted),
                 "При watchWrite=false событие Write не срабатывает");

        // Включаем watchWrite через Modify() -> пустой буфер отправки ОС должен сразу дать Write.
        tr.Check(selector.Modify(accepted, true, true), "Modify(accepted, read=true, write=true) успешен", true);
        int writeReady = selector.Poll(500);
        tr.Check(writeReady >= 1 && selector.IsWritable(accepted),
                 "После включения watchWrite селектор сразу вернул Write (маска: "
                 + FormatEventMask(selector.GetEvents(accepted)) + ")");

        // Проверяем корректность структуры SelectorEvent в ReadyEvents().
        bool foundInReady = false;
        for (const auto& ev : selector.ReadyEvents())
        {
            if (ev.handle == accepted.SockHandle() && ev.socket == &accepted && ev.IsWritable())
                foundInReady = true;
        }
        tr.Check(foundInReady, "ReadyEvents() содержит корректный SelectorEvent для accepted");

        // Отправляем ответ от сервера к клиенту и отключаем watchWrite.
        const std::string serverMsg = "HELLO_FROM_SERVER";
        tr.Check(accepted.SendAll(serverMsg.data(), static_cast<int>(serverMsg.size())),
                 "Сервер отправил '" + serverMsg + "'", true);
        tr.Check(selector.Modify(accepted, true, false), "Modify(accepted, read=true, write=false) отключил Write", true);

        // Клиент читает сообщение и шлёт ответ обратно.
        tr.Check(selector.Add(client, true, false), "client добавлен в селектор на чтение", true);
        int clientReadReady = selector.Poll(1000);
        tr.Check(clientReadReady >= 1 && selector.IsReadable(client), "client получил событие Read", true);

        char recvBuf[64] = {};
        int r1 = client.Recv(recvBuf, static_cast<int>(sizeof(recvBuf) - 1));
        tr.Check(r1 == static_cast<int>(serverMsg.size()) && std::string(recvBuf, static_cast<size_t>(r1)) == serverMsg,
                 "client принял '" + std::string(recvBuf, r1 > 0 ? static_cast<size_t>(r1) : 0) + "'");

        const std::string clientMsg = "PING_FROM_CLIENT";
        tr.Check(client.SendAll(clientMsg.data(), static_cast<int>(clientMsg.size())),
                 "client отправил '" + clientMsg + "'", true);

        int serverReadReady = selector.Poll(1000);
        tr.Check(serverReadReady >= 1 && selector.IsReadable(accepted), "accepted получил событие Read", true);

        std::memset(recvBuf, 0, sizeof(recvBuf));
        int r2 = accepted.Recv(recvBuf, static_cast<int>(sizeof(recvBuf) - 1));
        tr.Check(r2 == static_cast<int>(clientMsg.size()) && std::string(recvBuf, static_cast<size_t>(r2)) == clientMsg,
                 "accepted принял '" + std::string(recvBuf, r2 > 0 ? static_cast<size_t>(r2) : 0) + "'");

        tr.EndSuite();
    }

    // =========================================================================
    // ТЕСТ 7: ШТАТНОЕ ЗАКРЫТИЕ (HANGUP / EOF) И НЕПОТЕРЯННЫЙ ХВОСТ ДАННЫХ
    // =========================================================================

    /**
     * @brief Проверяет, что при отправке финального сообщения и немедленном закрытии сокета пиром
     *        селектор не помечает это как критическую ошибку (`HasCriticalError() == false`), а данные и EOF читаются чисто.
     * @param [in,out] tr Экземпляр `TestRunner` для логирования проверок.
     */
    void TestGracefulCloseAndHangUp(TestRunner& tr)
    {
        tr.BeginSuite("7. Штатное закрытие (HangUp / EOF) и разделение HasCriticalError vs IsHangUp");

        Socket server;
        int port = 0;
        if (!tr.Check(CreateLoopbackTcpServer(server, port), "Сервер запущен на порту " + std::to_string(port), true))
        {
            tr.EndSuite();
            return;
        }

        Socket client;
        Socket peer;
        if (!tr.Check(CreateConnectedTcpPair(server, port, client, peer), "Установлена TCP-пара", true))
        {
            tr.EndSuite();
            return;
        }

        SocketSelector selector;
        tr.Check(selector.Add(peer, true, false), "peer добавлен в селектор", true);

        // Клиент отправляет финальную порцию данных и сразу корректно закрывает соединение (TCP FIN).
        const std::string finalPayload = "FINAL_PAYLOAD_BEFORE_FIN";
        tr.Check(client.SendAll(finalPayload.data(), static_cast<int>(finalPayload.size())),
                 "client отправил финальные данные", true);
        client.DoSend();
        client.Close();
        tr.Info("client вызвал DoSend() и Close() (отправлен TCP FIN)");

        // Читаем всё до EOF через цикл Poll -> ReadyEvents.
        std::string accumulated;
        bool sawEof           = false;
        bool sawCriticalError = false;
        bool sawHangUp        = false;

        for (int iter = 0; iter < 10 && !sawEof; ++iter)
        {
            int ready = selector.Poll(500);
            if (ready <= 0)
                break;

            for (const auto& ev : selector.ReadyEvents())
            {
                if (ev.handle != peer.SockHandle())
                    continue;

                tr.Info("Итерация " + std::to_string(iter) + ": события peer = " + FormatEventMask(ev.events));
                if (ev.HasCriticalError())
                    sawCriticalError = true;
                if (ev.IsHangUp())
                    sawHangUp = true;

                // Сначала всегда вычитываем доступные байты до EWOULDBLOCK или EOF (0).
                if (ev.IsReadable())
                {
                    char tmp[64] = {};
                    while (true)
                    {
                        int n = peer.Recv(tmp, static_cast<int>(sizeof(tmp)));
                        if (n > 0)
                        {
                            accumulated.append(tmp, static_cast<size_t>(n));
                        }
                        else if (n == 0)
                        {
                            sawEof = true;
                            break;
                        }
                        else
                        {
                            // SOCK_EWOULDBLOCK — буфер на этой итерации опустошён.
                            break;
                        }
                    }
                }
            }
        }

        tr.Check(!sawCriticalError, "При штатном FIN не возникла критическая ошибка (HasCriticalError() == false)");
        tr.Check(accumulated == finalPayload, "Все финальные данные до FIN получены без потерь ('" + accumulated + "')");
        tr.Check(sawEof, "Получен штатный EOF (Recv() == 0)");
        tr.Info("Флаг IsHangUp() в процессе закрытия: " + std::string(sawHangUp ? "сработал" : "не выставлялся ядром (EOF пришёл через Read)"));

        tr.EndSuite();
    }

    // =========================================================================
    // ТЕСТ 8: БЕЗОПАСНЫЙ REMOVE() ПРЯМО ВНУТРИ ЦИКЛА ПО READYEVENTS()
    // =========================================================================

    /**
     * @brief Проверяет безопасность вызова `Remove()` и `Close()` непосредственно во время итерации по `ReadyEvents()`.
     * @param [in,out] tr Экземпляр `TestRunner` для логирования проверок.
     */
    void TestRemoveDuringReadyEventsLoop(TestRunner& tr)
    {
        tr.BeginSuite("8. Безопасное удаление сокетов (Remove) прямо внутри цикла по ReadyEvents()");

        Socket server;
        int port = 0;
        if (!tr.Check(CreateLoopbackTcpServer(server, port), "Сервер запущен на порту " + std::to_string(port), true))
        {
            tr.EndSuite();
            return;
        }

        constexpr size_t kPairs = 5;
        std::deque<Socket> clients;
        std::deque<Socket> peers;
        SocketSelector selector;

        for (size_t i = 0; i < kPairs; ++i)
        {
            Socket c;
            Socket p;
            if (!CreateConnectedTcpPair(server, port, c, p))
            {
                tr.Check(false, "Не удалось создать пару #" + std::to_string(i), true);
                tr.EndSuite();
                return;
            }
            clients.push_back(std::move(c));
            peers.push_back(std::move(p));
            selector.Add(peers.back(), true, false);
        }

        // Все клиенты отправляют данные одновременно.
        for (size_t i = 0; i < kPairs; ++i)
        {
            std::string m = "M" + std::to_string(i);
            clients[i].SendAll(m.data(), static_cast<int>(m.size()));
        }

        size_t removedCount = 0;
        for (int attempt = 0; attempt < 10 && removedCount < kPairs; ++attempt)
        {
            int ready = selector.Poll(500);
            if (ready <= 0)
                break;

            // Удаляем сокет прямо внутри range-for по selector.ReadyEvents()!
            for (const auto& ev : selector.ReadyEvents())
            {
                if (ev.IsReadable())
                {
                    if (selector.Remove(ev.handle))
                        ++removedCount;
                }
            }
        }

        tr.Check(removedCount == kPairs, "Удалены все " + std::to_string(removedCount) + "/" + std::to_string(kPairs)
                 + " сокетов прямо во время обхода ReadyEvents()");
        tr.Check(selector.Empty() && selector.Size() == 0, "После цикла селектор полностью пуст");

        tr.EndSuite();
    }

    // =========================================================================
    // ТЕСТ 9: UDP-СОКЕТЫ В SOCKETSELECTOR
    // =========================================================================

    /**
     * @brief Проверяет работу мультиплексора с датаграммными UDP-сокетами (`SOCK_DGRAM`).
     * @param [in,out] tr Экземпляр `TestRunner` для логирования проверок.
     */
    void TestUdpSockets(TestRunner& tr)
    {
        tr.BeginSuite("9. Работа SocketSelector с UDP-сокетами (SOCK_DGRAM)");

        Socket udpReceiver(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        Socket udpSender(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

        if (!tr.Check(udpReceiver.IsCreated() && udpSender.IsCreated(), "Созданы два UDP-сокета", true))
        {
            tr.EndSuite();
            return;
        }

        if (!tr.Check(udpReceiver.Bind("127.0.0.1", 0), "udpReceiver привязан к 127.0.0.1:0", true))
        {
            tr.EndSuite();
            return;
        }
        const int udpPort = udpReceiver.LocalPort();
        udpReceiver.SetBlocking(false);

        SocketSelector selector;
        tr.Check(selector.Add(udpReceiver, true, false), "udpReceiver добавлен в селектор", true);

        const std::string dgram = "UDP_SELECTOR_TEST_PAYLOAD";
        int sent = udpSender.SendTo(dgram.data(), static_cast<int>(dgram.size()), "127.0.0.1", udpPort);
        tr.Check(sent == static_cast<int>(dgram.size()),
                 "udpSender отправил " + std::to_string(sent) + " байт на порт " + std::to_string(udpPort), true);

        int ready = selector.Poll(1000);
        tr.Check(ready == 1 && selector.IsReadable(udpReceiver), "Poll(1000) поймал входящую UDP-датаграмму", true);

        char buf[64] = {};
        std::string fromHost;
        int fromPort = 0;
        int recvd = udpReceiver.RecvFrom(buf, static_cast<int>(sizeof(buf) - 1), fromHost, fromPort);
        tr.Check(recvd == static_cast<int>(dgram.size()) && std::string(buf, static_cast<size_t>(recvd)) == dgram,
                 "udpReceiver принял '" + std::string(buf, recvd > 0 ? static_cast<size_t>(recvd) : 0)
                 + "' от " + fromHost + ":" + std::to_string(fromPort));

        tr.EndSuite();
    }

    // =========================================================================
    // ТЕСТ 10: EDGE-TRIGGERED РЕЖИМ (EPOLLET НА LINUX / FALLBACK НА WINDOWS)
    // =========================================================================

    /**
     * @brief Проверяет поведение режима `edgeTriggered = true` (`EPOLLET` на Linux; безопасный Level-Triggered на Windows).
     * @param [in,out] tr Экземпляр `TestRunner` для логирования проверок.
     */
    void TestEdgeTriggeredMode(TestRunner& tr)
    {
        tr.BeginSuite("10. Режим Edge-Triggered (EPOLLET на Linux / Level-Triggered fallback на Windows)");

        Socket server;
        int port = 0;
        if (!tr.Check(CreateLoopbackTcpServer(server, port), "Сервер запущен на порту " + std::to_string(port), true))
        {
            tr.EndSuite();
            return;
        }

        Socket client;
        Socket peer;
        if (!tr.Check(CreateConnectedTcpPair(server, port, client, peer), "Установлена TCP-пара", true))
        {
            tr.EndSuite();
            return;
        }

        SocketSelector selector;
        tr.Check(selector.Add(peer, true, false, /*edgeTriggered=*/true),
                 "Add(peer, read=true, write=false, edgeTriggered=true) успешен", true);

        // Отправляем 10 байт, а читаем сначала только 4.
        const std::string part1 = "ABCDEFGHIJ";
        tr.Check(client.SendAll(part1.data(), static_cast<int>(part1.size())), "Отправлено 10 байт 'ABCDEFGHIJ'", true);

        int r1 = selector.Poll(1000);
        tr.Check(r1 == 1 && selector.IsReadable(peer), "Первый Poll(1000) поймал Read", true);

        char chunk[16] = {};
        int n1 = peer.Recv(chunk, 4);
        tr.Check(n1 == 4 && std::string(chunk, 4) == "ABCD", "Прочитаны первые 4 байта ('ABCD'), в буфере остались 6 байт");

        // Второй Poll(50) при оставшихся 6 байтах:
        // - на Linux (EPOLLET) обязан вернуть 0 (повторного уведомления нет до прихода новых данных);
        // - на Windows (WSAPoll, Level-Triggered) вернёт 1, пока буфер не пуст.
        int r2 = selector.Poll(50);
        if (SocketSelector::SupportsEdgeTriggered())
        {
            tr.Check(r2 == 0, "Linux EPOLLET: повторный Poll(50) вернул 0 при недочитанном буфере (корректный Edge-Triggered)");

            // Приход новых данных снова поднимает фронт события.
            const std::string part2 = "1234";
            tr.Check(client.SendAll(part2.data(), static_cast<int>(part2.size())), "Отправлена новая порция '1234'", true);
            int r3 = selector.Poll(1000);
            tr.Check(r3 == 1 && selector.IsReadable(peer), "Linux EPOLLET: после новой порции Poll(1000) снова сработал");

            // Полностью опустошаем буфер до SOCK_EWOULDBLOCK.
            std::string rest;
            while (true)
            {
                int n = peer.Recv(chunk, static_cast<int>(sizeof(chunk)));
                if (n > 0)
                    rest.append(chunk, static_cast<size_t>(n));
                else
                    break;
            }
            tr.Check(rest == "EFGHIJ1234", "Буфер полностью вычитан: остаток '" + rest + "'");
        }
        else
        {
            tr.Info("Windows WSAPoll работает в режиме Level-Triggered (флаг edgeTriggered безопасно игнорируется)");
            tr.Check(r2 == 1 && selector.IsReadable(peer),
                     "Windows Level-Triggered: повторный Poll(50) вернул 1, пока в буфере остались байты");

            int n2 = peer.Recv(chunk, 6);
            tr.Check(n2 == 6 && std::string(chunk, 6) == "EFGHIJ", "Оставшиеся 6 байт ('EFGHIJ') успешно дочитаны");
        }

        tr.EndSuite();
    }

    // =========================================================================
    // ТЕСТ 11: НАГРУЗОЧНЫЙ ТЕСТ МНОЖЕСТВЕННЫХ ОДНОВРЕМЕННЫХ СОЕДИНЕНИЙ
    // =========================================================================

    /**
     * @brief Нагрузочный однопоточный Event Loop: создаёт множество TCP-пар, проводит полный эхо-обмен
     *        через `SocketSelector::ReadyEvents()` с батчингом и проверяет целостность каждого сообщения.
     * @param [in,out] tr Экземпляр `TestRunner` для логирования проверок.
     * @note Константу `kStressPairs` можно увеличить до 1000..500000+ на Linux после поднятия `ulimit -n`.
     */
    void TestManyConnectionsStress(TestRunner& tr)
    {
        tr.BeginSuite("11. Стресс-тест множественных одновременных TCP-соединений (Event Loop O(K))");

        // 200 пар = 400 активных сокетов (безопасно работает при стандартных лимитах ОС Linux ulimit=1024 и Windows).
        // Для экстремальной проверки увеличьте kStressPairs и поднимите системный лимит дескрипторов.
        constexpr size_t kStressPairs   = 200;
        constexpr size_t kPollBatchSize = 64; // Специально меньше kStressPairs, чтобы протестировать батчинг.

        Socket server;
        int port = 0;
        if (!tr.Check(CreateLoopbackTcpServer(server, port, SOMAXCONN),
                      "Сервер стресс-теста запущен на 127.0.0.1:" + std::to_string(port), true))
        {
            tr.EndSuite();
            return;
        }

        using Clock = std::chrono::steady_clock;
        const auto tStart = Clock::now();

        SocketSelector selector(kStressPairs * 2, kPollBatchSize);
        tr.Check(selector.IsValid(), "Селектор стресс-теста инициализирован с Reserve("
                 + std::to_string(kStressPairs * 2) + "), batch=" + std::to_string(kPollBatchSize), true);

        // Храним сокеты в std::deque, чтобы их адреса в памяти никогда не менялись при push_back.
        std::deque<Socket> clients;
        std::deque<Socket> peers;
        std::unordered_map<socket_t, size_t> peerHandleToId;
        std::unordered_map<socket_t, size_t> clientHandleToId;
        peerHandleToId.reserve(kStressPairs);
        clientHandleToId.reserve(kStressPairs);

        // Шаг 1: создаём kStressPairs соединений и регистрируем обе стороны в одном селекторе.
        bool allConnected = true;
        for (size_t i = 0; i < kStressPairs; ++i)
        {
            Socket c;
            Socket p;
            if (!CreateConnectedTcpPair(server, port, c, p))
            {
                tr.Check(false, "Сбой при создании пары #" + std::to_string(i), true);
                allConnected = false;
                break;
            }

            clients.push_back(std::move(c));
            peers.push_back(std::move(p));

            const socket_t ch = clients.back().SockHandle();
            const socket_t ph = peers.back().SockHandle();
            clientHandleToId.emplace(ch, i);
            peerHandleToId.emplace(ph, i);

            if (!selector.Add(clients.back(), true, false) || !selector.Add(peers.back(), true, false))
            {
                tr.Check(false, "Сбой Add() на паре #" + std::to_string(i), true);
                allConnected = false;
                break;
            }
        }

        if (!allConnected)
        {
            tr.EndSuite();
            return;
        }

        const auto tConnected = Clock::now();
        tr.Check(selector.Size() == kStressPairs * 2,
                 "Зарегистрировано " + std::to_string(selector.Size()) + " одновременных сокетов за "
                 + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(tConnected - tStart).count()) + " мс");

        // Шаг 2: каждый клиент шлёт уникальное фиксированное 16-байтное сообщение "ECHO_XXXXXXXXXX\n".
        std::vector<std::string> expectedMsgs(kStressPairs);
        std::vector<std::string> peerBuffers(kStressPairs);
        std::vector<std::string> clientBuffers(kStressPairs);
        std::vector<bool>        completed(kStressPairs, false);

        for (size_t i = 0; i < kStressPairs; ++i)
        {
            std::ostringstream oss;
            oss << "ECHO_" << std::setw(10) << std::setfill('0') << i << '\n';
            expectedMsgs[i] = oss.str();

            if (!clients[i].SendAll(expectedMsgs[i].data(), static_cast<int>(expectedMsgs[i].size())))
            {
                tr.Check(false, "Сбой отправки клиентом #" + std::to_string(i), true);
                tr.EndSuite();
                return;
            }
        }

        // Шаг 3: единый однопоточный Event Loop (серверные peer эхо-отвечают, клиенты проверяют ответ).
        size_t completedCount = 0;
        size_t pollCalls      = 0;
        size_t totalEvents    = 0;
        size_t mismatchCount  = 0;
        const auto loopDeadline = Clock::now() + std::chrono::seconds(10);

        while (completedCount < kStressPairs && Clock::now() < loopDeadline)
        {
            int ready = selector.Poll(200);
            ++pollCalls;
            if (ready < 0)
            {
                tr.Check(false, "Ошибка Poll() в стресс-тесте", true);
                break;
            }
            if (ready == 0)
                continue;

            totalEvents += static_cast<size_t>(ready);

            for (const auto& ev : selector.ReadyEvents())
            {
                if (!ev.IsReadable())
                    continue;

                // Событие на серверной стороне -> читаем запрос и отправляем эхо обратно.
                auto pit = peerHandleToId.find(ev.handle);
                if (pit != peerHandleToId.end())
                {
                    const size_t id = pit->second;
                    char buf[64] = {};
                    while (true)
                    {
                        int n = peers[id].Recv(buf, static_cast<int>(sizeof(buf)));
                        if (n > 0)
                            peerBuffers[id].append(buf, static_cast<size_t>(n));
                        else
                            break;
                    }
                    if (peerBuffers[id].size() == expectedMsgs[id].size())
                    {
                        peers[id].SendAll(peerBuffers[id].data(), static_cast<int>(peerBuffers[id].size()));
                    }
                    continue;
                }

                // Событие на клиентской стороне -> читаем эхо-ответ и сверяем с эталоном.
                auto cit = clientHandleToId.find(ev.handle);
                if (cit != clientHandleToId.end())
                {
                    const size_t id = cit->second;
                    char buf[64] = {};
                    while (true)
                    {
                        int n = clients[id].Recv(buf, static_cast<int>(sizeof(buf)));
                        if (n > 0)
                            clientBuffers[id].append(buf, static_cast<size_t>(n));
                        else
                            break;
                    }
                    if (!completed[id] && clientBuffers[id].size() >= expectedMsgs[id].size())
                    {
                        completed[id] = true;
                        ++completedCount;
                        if (clientBuffers[id] != expectedMsgs[id])
                            ++mismatchCount;

                        // Сразу снимаем завершённую пару с наблюдения прямо внутри цикла.
                        selector.Remove(clients[id]);
                        selector.Remove(peers[id]);
                    }
                }
            }
        }

        const auto exchangeMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - tConnected).count();
        tr.Info("Эхо-обмен завершён за " + std::to_string(exchangeMs) + " мс | вызовов Poll(): "
                + std::to_string(pollCalls) + " | обработано событий: " + std::to_string(totalEvents));

        tr.Check(completedCount == kStressPairs,
                 "Все пары завершили эхо-обмен: " + std::to_string(completedCount) + "/" + std::to_string(kStressPairs));
        tr.Check(mismatchCount == 0,
                 "Ошибок целостности данных: " + std::to_string(mismatchCount));
        tr.Check(selector.Empty(),
                 "После завершения стресс-теста селектор полностью пуст (Size() == " + std::to_string(selector.Size()) + ")");

        tr.EndSuite();
    }
} // anonymous namespace

int main()
{
    setlocale(LC_ALL, "");

    TestRunner tr;

    std::cout << "Запуск набора тестов SocketSelector...\n"
              << "Платформа: "
#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
              << "Windows (Winsock2)"
#else
              << "Linux (POSIX)"
#endif
              << " | Бэкенд: " << other::net::sockets::SocketSelector::BackendName() << '\n';

    try
    {
        TestBackendAndInitialState(tr);
        TestInvalidAndClosedSockets(tr);
        TestAddModifyRemoveAndClear(tr);
        TestMoveSemantics(tr);
        TestPollTimeoutAndEpochReset(tr);
        TestTcpAcceptReadWriteModify(tr);
        TestGracefulCloseAndHangUp(tr);
        TestRemoveDuringReadyEventsLoop(tr);
        TestUdpSockets(tr);
        TestEdgeTriggeredMode(tr);
        TestManyConnectionsStress(tr);
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
Запуск набора тестов SocketSelector...
Платформа: Windows(Winsock2) | Бэкенд : WSAPoll

========================================================================
[TEST] 1. Инициализация, свойства бэкенда и базовое состояние
========================================================================
[INFO] Активный бэкенд : WSAPoll, SupportsEdgeTriggered = false
[PASS] Селектор успешно инициализирован(IsValid() == true)
[PASS] Новый селектор пуст(Empty() == true)
[PASS] Size() == 0
[PASS] Sockets() пуст
[PASS] ReadyEvents() пуст
[PASS] MaxEventsPerPoll() == 512
[PASS] SetMaxEventsPerPoll(0) приводится к 1
[PASS] SetMaxEventsPerPoll(2048) применён
[PASS] Reserve(10000) не меняет Size() и Empty()
[PASS] Poll(100) на пустом селекторе мгновенно возвращает 0
[SUITE OK]     1. Инициализация, свойства бэкенда и базовое состояние

========================================================================
[TEST] 2. Негативные сценарии : пустые, закрытые и отсутствующие сокеты
========================================================================
[PASS] Пустой Socket имеет IsClosed() == true
[PASS] Валидный TCP Socket создан
[PASS] Add(пустой сокет) возвращает false
[PASS] После Add(пустой сокет) установлен SOCK_EBADF(факт : File handle is not valid.)
[PASS] После неудачного Add Size() остаётся 0
[PASS] Modify(отсутствующий сокет) возвращает false
[PASS] После Modify(отсутствующий сокет) установлен SOCK_ENOENT(факт: No such file, directory, or resource.)
[PASS] Modify(закрытый сокет) возвращает false + SOCK_EBADF
[PASS] Remove(отсутствующий Socket) возвращает false
[PASS] Remove(закрытый Socket) возвращает false
[PASS] Remove(INVALID_SOCKET) возвращает false
[PASS] Contains(закрытый Socket) == false
[PASS] Contains(INVALID_SOCKET) == false
[PASS] Contains(отсутствующий Socket) == false
[PASS] GetEvents(закрытый Socket) == None
[PASS] IsReadable / IsWritable / HasError для закрытого сокета возвращают false
[SUITE OK]     2. Негативные сценарии : пустые, закрытые и отсутствующие сокеты

========================================================================
[TEST] 3. Add, Modify, Remove(swap - and -pop), удаление закрытого сокета и Clear
========================================================================
[PASS] Созданы 4 тестовых TCP - сокета
[PASS] Add(s1) успешен
[PASS] Add(s2) успешен
[PASS] Add(s3) успешен
[PASS] Add(s4) успешен
[PASS] Size() == 4 и Sockets().size() == 4
[PASS] Повторный Add(s1) обновляет маску без дублирования
[PASS] Size() остался равен 4
[PASS] Contains() подтверждает наличие всех 4 сокетов
[PASS] Remove(h2 по socket_t) из середины успешен
[PASS] После Remove(h2) : Size() == 3 и s2 отсутствует
[PASS] После swap - and -pop оставшиеся s1, s3, s4 по - прежнему находятся за O(1)
[PASS] s3 закрыт до вызова Remove()
[PASS] Резервный Remove(закрытый s3) нашёл запись через _sockets и удалил её
[PASS] После удаления закрытого s3 : Size() == 2 (утечки дескриптора нет)
[PASS] Modify(s4) после swap - and -pop работает корректно
[PASS] Clear() полностью очистил селектор
[PASS] Повторный Add(s1) после Clear() работает корректно
[SUITE OK]     3. Add, Modify, Remove(swap - and -pop), удаление закрытого сокета и Clear

========================================================================
[TEST] 4. Move - семантика SocketSelector(конструктор и присваивание)
========================================================================
[PASS] Создан сервер на 127.0.0.1:0
[PASS] Установлена TCP - пара client < ->peer
[PASS] peer добавлен в original
[PASS] После move - конструктора movedCtor валиден и содержит peer
[PASS] После move - конструктора original пуст
[PASS] После move - присваивания movedAssign валиден и содержит peer
[PASS] client отправил 'MOVE_OK'
[PASS] movedAssign.Poll(1000) поймал событие Read на peer(ready = 1)
[PASS] peer прочитал 'MOVE_OK'
[SUITE OK]     4. Move - семантика SocketSelector(конструктор и присваивание)

========================================================================
[TEST] 5. Таймаут Poll() и сброс старых событий между вызовами
========================================================================
[PASS] Создан сервер на порту 0
[PASS] Установлена TCP - пара
[PASS] peer добавлен в селектор(только чтение)
[PASS] Poll(0) без данных вернул 0
[INFO] Фактическое время ожидания Poll(80) : 88 мс
[PASS] Poll(80) выждал таймаут и вернул 0
[PASS] Отправлен 1 байт 'X'
[PASS] Poll(500) зафиксировал Read
[PASS] Байт 'X' вычитан из буфера
[PASS] После нового Poll(0) флаг IsReadable(peer) сброшен в false (эпоха обновилась)
[SUITE OK]     5. Таймаут Poll() и сброс старых событий между вызовами

========================================================================
[TEST] 6. TCP Accept, двусторонний обмен и динамический Modify(watchWrite)
========================================================================
[PASS] Слушающий сервер запущен на 127.0.0.1:0
[PASS] Слушающий сервер добавлен в селектор
[PASS] Клиент подключился к серверу
[PASS] Poll(1000) поймал входящее подключение на слушающем сокете
[PASS] server.Accept() вернул валидный клиентский сокет
[PASS] accepted добавлен(watchRead = true, watchWrite = false)
[PASS] При watchWrite = false событие Write не срабатывает
[PASS] Modify(accepted, read = true, write = true) успешен
[PASS] После включения watchWrite селектор сразу вернул Write(маска: Write)
[PASS] ReadyEvents() содержит корректный SelectorEvent для accepted
[PASS] Сервер отправил 'HELLO_FROM_SERVER'
[PASS] Modify(accepted, read = true, write = false) отключил Write
[PASS] client добавлен в селектор на чтение
[PASS] client получил событие Read
[PASS] client принял 'HELLO_FROM_SERVER'
[PASS] client отправил 'PING_FROM_CLIENT'
[PASS] accepted получил событие Read
[PASS] accepted принял 'PING_FROM_CLIENT'
[SUITE OK]     6. TCP Accept, двусторонний обмен и динамический Modify(watchWrite)

========================================================================
[TEST] 7. Штатное закрытие(HangUp / EOF) и разделение HasCriticalError vs IsHangUp
========================================================================
[PASS] Сервер запущен на порту 0
[PASS] Установлена TCP - пара
[PASS] peer добавлен в селектор
[PASS] client отправил финальные данные
[INFO] client вызвал DoSend() и Close() (отправлен TCP FIN)
[INFO] Итерация 0: события peer = Read | HangUp
[PASS] При штатном FIN не возникла критическая ошибка(HasCriticalError() == false)
[PASS] Все финальные данные до FIN получены без потерь('FINAL_PAYLOAD_BEFORE_FIN')
[PASS] Получен штатный EOF(Recv() == 0)
[INFO] Флаг IsHangUp() в процессе закрытия : сработал
[SUITE OK]     7. Штатное закрытие(HangUp / EOF) и разделение HasCriticalError vs IsHangUp

========================================================================
[TEST] 8. Безопасное удаление сокетов(Remove) прямо внутри цикла по ReadyEvents()
========================================================================
[PASS] Сервер запущен на порту 0
[PASS] Удалены все 5 / 5 сокетов прямо во время обхода ReadyEvents()
[PASS] После цикла селектор полностью пуст
[SUITE OK]     8. Безопасное удаление сокетов(Remove) прямо внутри цикла по ReadyEvents()

========================================================================
[TEST] 9. Работа SocketSelector с UDP - сокетами(SOCK_DGRAM)
========================================================================
[PASS] Созданы два UDP - сокета
[PASS] udpReceiver привязан к 127.0.0.1:0
[PASS] udpReceiver добавлен в селектор
[PASS] udpSender отправил 25 байт на порт 64060
[PASS] Poll(1000) поймал входящую UDP - датаграмму
[PASS] udpReceiver принял 'UDP_SELECTOR_TEST_PAYLOAD' от 127.0.0.1 : 64061
[SUITE OK]     9. Работа SocketSelector с UDP - сокетами(SOCK_DGRAM)

========================================================================
[TEST] 10. Режим Edge - Triggered(EPOLLET на Linux / Level - Triggered fallback на Windows)
========================================================================
[PASS] Сервер запущен на порту 0
[PASS] Установлена TCP - пара
[PASS] Add(peer, read = true, write = false, edgeTriggered = true) успешен
[PASS] Отправлено 10 байт 'ABCDEFGHIJ'
[PASS] Первый Poll(1000) поймал Read
[PASS] Прочитаны первые 4 байта('ABCD'), в буфере остались 6 байт
[INFO] Windows WSAPoll работает в режиме Level - Triggered(флаг edgeTriggered безопасно игнорируется)
[PASS] Windows Level - Triggered: повторный Poll(50) вернул 1, пока в буфере остались байты
[PASS] Оставшиеся 6 байт('EFGHIJ') успешно дочитаны
[SUITE OK]     10. Режим Edge - Triggered(EPOLLET на Linux / Level - Triggered fallback на Windows)

========================================================================
[TEST] 11. Стресс - тест множественных одновременных TCP - соединений(Event Loop O(K))
========================================================================
[PASS] Сервер стресс - теста запущен на 127.0.0.1:0
[PASS] Селектор стресс - теста инициализирован с Reserve(400), batch = 64
[PASS] Зарегистрировано 400 одновременных сокетов за 66 мс
[INFO] Эхо - обмен завершён за 23 мс | вызовов Poll() : 8 | обработано событий : 400
[PASS] Все пары завершили эхо - обмен : 200 / 200
[PASS] Ошибок целостности данных : 0
[PASS] После завершения стресс - теста селектор полностью пуст(Size() == 0)
[SUITE OK]     11. Стресс - тест множественных одновременных TCP - соединений(Event Loop O(K))

########################################################################
ИТОГОВЫЙ ОТЧЁТ ТЕСТИРОВАНИЯ SocketSelector
Бэкенд ОС : WSAPoll(Edge - Triggered: нет)
Сценариев OK : 11
Сценариев FAIL : 0
Проверок PASS : 110
Проверок FAIL : 0
########################################################################
*/