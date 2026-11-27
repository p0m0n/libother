/**
 * @file ServerApp.cpp
 * @brief Отдельное консольное серверное приложение на базе `TCPServer` и `ByteBuffer`.
 *
 * @details
 * Запускается как самостоятельный бинарник:
 *   `ServerApp [host] [port]` (по умолчанию `127.0.0.1` и `9090`).
 * Принимает входящие подключения, печатает сообщения клиентов в консоль и даёт возможность
 * отвечать клиенту прямо из консоли сервера.
 *
 * Команды в консоли сервера:
 * - `/list` — показать список активных клиентов (`ClientId`, IP:порт);
 * - `/stats` — показать статистику сервера;
 * - `/send <id> <сообщение>` — отправить сообщение конкретному клиенту;
 * - `/quit` или `/exit` — остановить сервер и выйти;
 * - любой другой текст — если подключён 1 клиент, отправляется ему; если несколько — рассылается всем (`BroadcastPacket`).
 */

#include "TCPServer.h"
#include "ByteBuffer.h"
#include "Utils.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>


namespace
{
    using namespace other::net::sockets;

    /**
     * @brief Извлекает все готовые пакеты `ByteBuffer` из буфера накопления клиента и выводит их в консоль.
     * @param [in,out] server   Сервер для отправки ответа/управления подключением.
     * @param [in,out] accMap   Буферы входящих байтов по каждому `ClientId`.
     * @param [in,out] ioMtx    Мьютекс для синхронизации доступа к `accMap` и `std::cout`.
     * @param [in]     clientId Идентификатор клиента.
     * @param [in]     data     Принятый блок байтов.
     * @param [in]     size     Число принятых байтов.
     */
    void ProcessIncomingBytes(
        TCPServer& server,
        std::unordered_map<ClientId, std::vector<uint8_t>>& accMap,
        std::mutex& ioMtx,
        ClientId clientId,
        const uint8_t* data,
        size_t size)
    {
        std::lock_guard<std::mutex> lock(ioMtx);
        auto& buf = accMap[clientId];
        buf.insert(buf.end(), data, data + size);

        while (buf.size() >= sizeof(uint32_t))
        {
            uint32_t netLen = 0;
            std::memcpy(&netLen, buf.data(), sizeof(netLen));
            const uint32_t payLen = ntohl(netLen);

            if (payLen > StreamSocket::DefaultMaxPacketSize)
            {
                std::cerr << "\n[SERVER] Клиент #" << clientId << " прислал слишком большой пакет (" << payLen << " Б) — отключаем.\n> " << std::flush;
                server.DisconnectClient(clientId, false);
                accMap.erase(clientId);
                return;
            }

            if (buf.size() < sizeof(uint32_t) + static_cast<size_t>(payLen))
                break;

            ByteBuffer packet(payLen, ByteOrder::BigEndian);
            if (payLen > 0)
                packet.WriteBytes(buf.data() + sizeof(uint32_t), payLen);
            packet.Rewind();

            std::string msg;
            if (payLen > 0)
                packet >> msg;

            std::cout << "\n[Клиент #" << clientId << "]: " << msg << "\n> " << std::flush;

            buf.erase(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(sizeof(uint32_t) + payLen));
        }
    }

    /**
     * @brief Формирует и отправляет текстовый пакет `ByteBuffer` указанному клиенту.
     * @param [in,out] server   Сервер.
     * @param [in]     clientId Идентификатор получателя.
     * @param [in]     text     Текст сообщения.
     * @return `true` при успешной отправке/постановке в очередь; `false` при ошибке.
     */
    bool SendTextPacket(TCPServer& server, ClientId clientId, const std::string& text)
    {
        ByteBuffer pkt(text.size() + 8U, ByteOrder::BigEndian);
        pkt << text;
        return server.SendPacket(clientId, pkt);
    }
} // namespace


/**
 * @brief Точка входа консольного сервера.
 * @param [in] argc Число аргументов командной строки.
 * @param [in] argv Аргументы: `[host] [port]`.
 * @return `0` при штатном завершении; `1` при ошибке запуска.
 */
int main(int argc, char* argv[])
{
    other::utils::InitConsoleUtf8();
	
    using namespace other::net::sockets;
	
    const std::string host = (argc >= 2) ? argv[1] : "127.0.0.1";
    const int         port = (argc >= 3) ? std::atoi(argv[2]) : 9090;

    if (port <= 0 || port > 65535)
    {
        std::cerr << "[ERR] Некорректный порт: " << port << "\n";
        return 1;
    }

    ServerConfig cfg;
    cfg.noDelay = true;

    TCPServer server(AF_INET, host, port, cfg);
    if (!server.IsListening())
    {
        const int err = GET_NET_ERROR();
        std::cerr << "[ERR] Не удалось запустить сервер на " << host << ":" << port
                  << " | code=" << err << " (" << SocketErrorToString(err) << ")\n";
        return 1;
    }

    std::mutex ioMtx;
    std::unordered_map<ClientId, std::vector<uint8_t>> accMap;
    std::atomic<bool> running{ true };

    server.SetOnClientConnected([&](ClientId id, const std::string& ip, int clientPort)
    {
        std::lock_guard<std::mutex> lock(ioMtx);
        std::cout << "\n[SERVER] Подключился клиент #" << id << " (" << ip << ":" << clientPort << ")\n> " << std::flush;
    });

    server.SetOnClientData([&](ClientId id, const uint8_t* data, size_t size)
    {
        ProcessIncomingBytes(server, accMap, ioMtx, id, data, size);
    });

    server.SetOnClientDisconnected([&](ClientId id, int errCode)
    {
        std::lock_guard<std::mutex> lock(ioMtx);
        accMap.erase(id);
        std::cout << "\n[SERVER] Клиент #" << id << " отключился (code=" << errCode << ")\n> " << std::flush;
    });

    server.SetOnError([&](int errCode, const std::string& ctx)
    {
        std::lock_guard<std::mutex> lock(ioMtx);
        std::cerr << "\n[SERVER ERR] " << ctx << " | code=" << errCode
                  << " (" << SocketErrorToString(errCode) << ")\n> " << std::flush;
    });

    // Сетевой Event Loop работает в фоновом потоке, чтобы основной поток свободно читал std::cin.
    std::thread netThread([&]()
    {
        while (running.load(std::memory_order_relaxed) && server.IsListening())
        {
            server.PollOnce(25);
        }
    });

    {
        std::lock_guard<std::mutex> lock(ioMtx);
        std::cout << "=== Сервер запущен на " << server.LocalAddress(true) << " ===\n"
                  << "Команды: /list, /stats, /send <id> <текст>, /quit\n"
                  << "Просто введите текст и нажмите Enter для отправки клиенту.\n";
    }

    std::string line;
    while (running.load(std::memory_order_relaxed))
    {
        {
            std::lock_guard<std::mutex> lock(ioMtx);
            std::cout << "> " << std::flush;
        }

        if (!std::getline(std::cin, line))
            break;

        if (line.empty())
            continue;

        if (line == "/quit" || line == "/exit")
        {
            running.store(false, std::memory_order_relaxed);
            break;
        }

        if (line == "/list")
        {
            std::lock_guard<std::mutex> lock(ioMtx);
            const auto ids = server.GetClientIds();
            if (ids.empty())
            {
                std::cout << "[SERVER] Активных клиентов нет.\n";
            }
            else
            {
                std::cout << "[SERVER] Активные клиенты (" << ids.size() << "):\n";
                for (ClientId id : ids)
                {
                    ClientInfo info;
                    if (server.GetClientInfo(id, info))
                        std::cout << "  #" << id << " -> " << info.ip << ":" << info.port << "\n";
                }
            }
            continue;
        }

        if (line == "/stats")
        {
            std::lock_guard<std::mutex> lock(ioMtx);
            const ServerStats st = server.Stats();
            std::cout << "[SERVER] active=" << st.activeClients
                      << ", accepted=" << st.totalAccepted
                      << ", disconnected=" << st.totalDisconnected
                      << ", rxBytes=" << st.totalBytesReceived
                      << ", txBytes=" << st.totalBytesSent << "\n";
            continue;
        }

        if (line.rfind("/send ", 0) == 0)
        {
            std::istringstream iss(line.substr(6));
            ClientId targetId = InvalidClientId;
            if (!(iss >> targetId))
            {
                std::lock_guard<std::mutex> lock(ioMtx);
                std::cerr << "[SERVER] Формат команды: /send <id> <текст>\n";
                continue;
            }
            std::string payload;
            std::getline(iss, payload);
            if (!payload.empty() && payload[0] == ' ')
                payload.erase(0, 1);

            if (payload.empty())
            {
                std::lock_guard<std::mutex> lock(ioMtx);
                std::cerr << "[SERVER] Пустое сообщение не отправлено.\n";
                continue;
            }

            std::lock_guard<std::mutex> lock(ioMtx);
            if (!SendTextPacket(server, targetId, payload))
            {
                const int err = GET_NET_ERROR();
                std::cerr << "[SERVER] Не удалось отправить клиенту #" << targetId
                          << " | code=" << err << " (" << SocketErrorToString(err) << ")\n";
            }
            continue;
        }

        // Обычный ввод текста: если клиент один — шлём ему, если несколько — делаем BroadcastPacket.
        std::lock_guard<std::mutex> lock(ioMtx);
        const auto ids = server.GetClientIds();
        if (ids.empty())
        {
            std::cout << "[SERVER] Нет подключённых клиентов.\n";
            continue;
        }

        if (ids.size() == 1)
        {
            if (!SendTextPacket(server, ids.front(), line))
            {
                const int err = GET_NET_ERROR();
                std::cerr << "[SERVER] Ошибка отправки: " << err << " (" << SocketErrorToString(err) << ")\n";
            }
        }
        else
        {
            ByteBuffer pkt(line.size() + 8U, ByteOrder::BigEndian);
            pkt << line;
            const size_t sentTo = server.BroadcastPacket(pkt);
            std::cout << "[SERVER] Отправлено всем (" << sentTo << " клиент(ам)).\n";
        }
    }

    running.store(false, std::memory_order_relaxed);
    server.RequestStop();
    if (netThread.joinable())
        netThread.join();
    server.Stop();

    std::cout << "[SERVER] Работа завершена.\n";
    return 0;
}
