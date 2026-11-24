#include "Socket.h"

#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace other::net::sockets;

namespace
{
    int g_totalTests  = 0;
    int g_passedTests = 0;
    int g_failedTests = 0;

    void ReportTest(const std::string& name, bool condition)
    {
        ++g_totalTests;
        if (condition)
            ++g_passedTests;
        else
            ++g_failedTests;

        std::cout << std::left << std::setw(58) << name
                  << ": " << (condition ? "[  OK  ]" : "[ FAIL ]") << std::endl;
    }

    // 1. Проверка поддержки IPv4/IPv6, конструкторов, перемещения, Attach/Detach, Clone и Dispose.
    void TestLifecycleAndOwnership()
    {
        std::cout << "\n=== 1. Lifecycle, constructors, Move, Attach/Detach ===\n";

        ReportTest("Socket::SupportsIPv4()", Socket::SupportsIPv4());

        // Пустой конструктор
        Socket emptySock;
        ReportTest("Default ctor (IsClosed, !IsCreated, !IsBound)",
            emptySock.IsClosed() && !emptySock.IsCreated() && !emptySock.IsBound() &&
            emptySock.SockHandle() == INVALID_SOCKET && emptySock.LocalPort() == -1);

        // Создание IPv4 TCP сокета
        Socket s1(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        ReportTest("Socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)",
            s1.IsCreated() && !s1.IsClosed() &&
            s1.AddressFamily() == AF_INET &&
            s1.SockType() == SOCK_STREAM &&
            s1.ProtocolType() == IPPROTO_TCP &&
            s1.StartTime().Ticks() > 0);

        // Move-конструктор
        socket_t origHandle = s1.SockHandle();
        Socket s2(std::move(s1));
        ReportTest("Move ctor (transfers handle, clears source)",
            s2.IsCreated() && s2.SockHandle() == origHandle && s1.IsClosed());

        // Move-присваивание
        Socket s3;
        s3 = std::move(s2);
        ReportTest("Move assignment (transfers handle, clears source)",
            s3.IsCreated() && s3.SockHandle() == origHandle && s2.IsClosed());

        // Detach + Attach (проверяем, что метаданные восстанавливаются)
        socket_t detached = s3.Detach();
        ReportTest("Detach() (returns handle, clears socket)",
            detached == origHandle && s3.IsClosed() && s3.AddressFamily() == 0);

        Socket s4;
        bool attached = s4.Attach(detached);
        ReportTest("Attach() (restores handle, family, type, protocol)",
            attached && s4.IsCreated() && s4.SockHandle() == origHandle &&
            s4.AddressFamily() == AF_INET && s4.SockType() == SOCK_STREAM && s4.ProtocolType() == IPPROTO_TCP);

        // SockClone
        Socket cloned = s4.SockClone();
        ReportTest("SockClone() (new handle, same family/type/protocol)",
            cloned.IsCreated() && cloned.SockHandle() != s4.SockHandle() &&
            cloned.AddressFamily() == s4.AddressFamily() && cloned.SockType() == s4.SockType());

        // Close & CloseTime & SockUptime
        bool closed = s4.Close();
        ReportTest("Close() (IsClosed, CloseTime set, SockUptime >= 0)",
            closed && s4.IsClosed() && s4.CloseTime().Ticks() > 0 && s4.SockUptime().Ticks() >= 0);

        // Dispose
        ReportTest("Dispose() (closes and resets metadata)",
            cloned.Dispose() && cloned.IsClosed() && cloned.AddressFamily() == 0);
    }

    // 2. Проверка сокетных опций.
    void TestSocketOptions()
    {
        std::cout << "\n=== 2. Socket options (ReuseAddr, KeepAlive, NoDelay, Buffers, Timeouts, Linger, TTL) ===\n";

        Socket tcp(AF_INET, SOCK_STREAM, IPPROTO_TCP);

        ReportTest("SetReuseAddress(true) / IsReuseAddress()",
            tcp.SetReuseAddress(true) && tcp.IsReuseAddress());

        ReportTest("SetKeepAlive(true) / IsKeepAlive()",
            tcp.SetKeepAlive(true) && tcp.IsKeepAlive());

        ReportTest("SetNoDelay(true) / IsNoDelay()",
            tcp.SetNoDelay(true) && tcp.IsNoDelay());

        ReportTest("SetBlocking(false) -> IsBlocking() == false",
            tcp.SetBlocking(false) && !tcp.IsBlocking());

        ReportTest("SetBlocking(true) -> IsBlocking() == true",
            tcp.SetBlocking(true) && tcp.IsBlocking());

        ReportTest("SetSendTimeout(1500) / GetSendTimeout()",
            tcp.SetSendTimeout(1500) == 0 && tcp.GetSendTimeout() >= 1000);

        ReportTest("SetRecvTimeout(2000) / GetRecvTimeout()",
            tcp.SetRecvTimeout(2000) == 0 && tcp.GetRecvTimeout() >= 1500);

        ReportTest("SetSendTimeout(-1) -> infinite (-1)",
            tcp.SetSendTimeout(-1) == 0 && tcp.GetSendTimeout() == -1);

        ReportTest("SetSendBufferSize / GetSendBufferSize",
            tcp.SetSendBufferSize(32768) == 0 && tcp.GetSendBufferSize() > 0);

        ReportTest("SetRecvBufferSize / GetRecvBufferSize",
            tcp.SetRecvBufferSize(32768) == 0 && tcp.GetRecvBufferSize() > 0);

        bool lingerOn = false;
        int lingerSec = 0;
        ReportTest("SetLingerState(true, 2) / GetLingerState()",
            tcp.SetLingerState(true, 2) == 0 && tcp.GetLingerState(lingerOn, lingerSec) == 0 &&
            lingerOn == true && lingerSec == 2);

        ReportTest("SetTtl(64) / GetTtl()",
            tcp.SetTtl(64) && tcp.GetTtl() == 64);

        ReportTest("SetDontFragment(true) / IsDontFragment()",
            tcp.SetDontFragment(true) && tcp.IsDontFragment());

        Socket udp(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        ReportTest("SetBroadcast(true) / IsBroadcast() on UDP",
            udp.SetBroadcast(true) && udp.IsBroadcast());

        ReportTest("SetMulticastLoopback(false) / IsMulticastLoopback()",
            udp.SetMulticastLoopback(false) && !udp.IsMulticastLoopback());

        if (Socket::SupportsIPv6())
        {
            Socket dual(SOCK_STREAM, IPPROTO_TCP);
            ReportTest("Dual-Stack IPv6 ctor (IsDualMode)",
                dual.IsCreated() && dual.AddressFamily() == AF_INET6 && dual.IsDualMode());
        }
    }

    // 3. Тест TCP: Bind, Listen, Connect, Accept, Send/Recv/Offset/All, Available, Poll, Select, Disconnect.
    void TestTcpLoopback()
    {
        std::cout << "\n=== 3. TCP: Bind, Listen, Connect, Accept, Send/Recv, Poll, Select ===\n";

        Socket server(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        server.SetReuseAddress(true);

        ReportTest("Bind(\"127.0.0.1\", 0) (ephemeral port) + IsBound()",
            server.Bind("127.0.0.1", 0) && server.IsBound() && server.LocalPort() > 0);

        ReportTest("Bind(invalid port 70000 -> false)", !server.Bind(70000));

        const int port = server.LocalPort();
        ReportTest("LocalAddress(true) contains 127.0.0.1",
            server.LocalAddress(true).find("127.0.0.1:" + std::to_string(port)) != std::string::npos);

        ReportTest("Listen(4)", server.Listen(4));

        bool clientOk = false;
        std::thread clientThread([port, &clientOk]() {
            Socket client(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            // Тестируем Connect с таймаутом 2000 мс и SendAllOffset
            if (!client.Connect("127.0.0.1", port, 2000))
                return;

            const char msg[] = "XXHelloTCP";
            if (!client.SendAllOffset(msg, 2, 8))
                return;

            char reply[16] = {};
            if (!client.RecvAll(reply, 5))
                return;

            clientOk = (std::memcmp(reply, "ACK!!", 5) == 0) &&
                       (client.RemotePort() == port) &&
                       (client.RemoteAddress(false) == "127.0.0.1");

            client.Disconnect(false);
        });

        // Проверяем Select() на прослушивающем сокете
        std::vector<Socket*> readList  = { &server };
        std::vector<Socket*> writeList = {};
        std::vector<Socket*> errList   = {};
        int sel = Socket::Select(readList, writeList, errList, 2000);
        ReportTest("Socket::Select() detects incoming connection",
            sel > 0 && readList.size() == 1 && readList[0] == &server);

        Socket peer = server.Accept();
        ReportTest("Accept() + IsConnected()",
            peer.IsCreated() && peer.IsConnected() && peer.RemotePort() > 0);

        ReportTest("PollRead(2000) + PollWrite(1000)",
            peer.PollRead(2000) && peer.PollWrite(1000));

        char buf[16] = {};
        bool recvOk = peer.RecvAllOffset(buf, 2, 8);
        ReportTest("RecvAllOffset() received \"HelloTCP\"",
            recvOk && std::memcmp(buf + 2, "HelloTCP", 8) == 0);

        int sent = peer.Send("ACK!!", 5);
        ReportTest("Send(\"ACK!!\") == 5", sent == 5);

        clientThread.join();
        ReportTest("Client thread round-trip (SendOffset, RecvAll, RemoteAddress)", clientOk);

        // Проверяем счётчики трафика и GetSocketError()
        ReportTest("BytesSent() == 5 && BytesReceived() == 8", peer.BytesSent() == 5 && peer.BytesRecv() == 8);
        peer.ResetStats();
        ReportTest("ResetStats() zeroes BytesSent & BytesReceived", peer.BytesSent() == 0 && peer.BytesRecv() == 0);
        ReportTest("GetSocketError() == 0 on healthy socket", server.SockError() == 0);

        // Проверка JoinMulticastGroup с невалидным IP
        Socket mcast(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        ReportTest("JoinMulticastGroup(invalid IP -> false)",
            !mcast.JoinMulticastGroup("not.an.ip"));

        // Даём ядру обработать FIN от клиента и проверяем IsConnected()
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        ReportTest("IsConnected() == false after peer disconnect", !peer.IsConnected());
    }

    // 4. Тест UDP: SendTo / SendToOffset / RecvFrom / RecvFromOffset.
    void TestUdpLoopback()
    {
        std::cout << "\n=== 4. UDP: SendTo / SendToOffset / RecvFrom / RecvFromOffset ===\n";

        Socket receiver(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        Socket sender(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

        ReportTest("UDP Bind(\"127.0.0.1\", 0)",
            receiver.Bind("127.0.0.1", 0) && receiver.LocalPort() > 0);

        int recvPort = receiver.LocalPort();
        const char payload[] = "___UDP_PAYLOAD";
        int sent = sender.SendToOffset(payload, 3, 11, "127.0.0.1", recvPort);
        ReportTest("UDP SendToOffset() == 11", sent == 11);

        ReportTest("UDP PollRead(1000) && Available() > 0",
            receiver.PollRead(1000) && receiver.Available() >= 11);

        char recvBuf[32] = {};
        std::string fromHost;
        int fromPort = 0;
        int got = receiver.RecvFromOffset(recvBuf, 1, 20, fromHost, fromPort);
        ReportTest("UDP RecvFromOffset() data + sender host/port",
            got == 11 && std::memcmp(recvBuf + 1, "UDP_PAYLOAD", 11) == 0 &&
            fromHost == "127.0.0.1" && fromPort > 0);
    }
}

int main()
{
    std::cout << "====================================================================\n";
    std::cout << "                     Socket Global Test Suite                       \n";
    std::cout << "====================================================================\n";

    TestLifecycleAndOwnership();
    TestSocketOptions();
    TestTcpLoopback();
    TestUdpLoopback();

    std::cout << "\n====================================================================\n";
    std::cout << "TOTAL: " << g_passedTests << " / " << g_totalTests << " completed, "
              << g_failedTests << " failed -> "
              << (g_failedTests == 0 ? "[  OK  ]" : "[ FAIL ]") << std::endl;
    std::cout << "====================================================================\n";

    return (g_failedTests == 0) ? 0 : 1;
}
