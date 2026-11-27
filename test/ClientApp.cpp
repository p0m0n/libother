/**
 * @file ClientApp.cpp
 * @brief Отдельное консольное клиентское приложение на базе `TCPClient` и `ByteBuffer`.
 *
 * @details
 * Запускается как самостоятельный бинарник:
 *   `ClientApp [host] [port]` (по умолчанию `127.0.0.1` и `9090`).
 * Подключается к серверу, читает вводимые в консоль строки и отправляет их как пакеты `ByteBuffer`,
 * одновременно принимая и выводя в консоль сообщения от сервера.
 *
 * Команды в консоли клиента:
 * - `/quit` или `/exit` — закрыть соединение и выйти;
 * - любой другой текст — отправляется серверу после нажатия Enter.
 */

#include "TCPClient.h"
#include "ByteBuffer.h"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>


/**
 * @brief Точка входа консольного клиента.
 * @param [in] argc Число аргументов командной строки.
 * @param [in] argv Аргументы: `[host] [port]`.
 * @return `0` при штатном завершении; `1` при ошибке подключения.
 */
int main(int argc, char* argv[])
{
	utils::InitConsoleUtf8();
	
    using namespace other::net::sockets;

    const std::string host = (argc >= 2) ? argv[1] : "127.0.0.1";
    const int         port = (argc >= 3) ? std::atoi(argv[2]) : 9090;

    if (port <= 0 || port > 65535)
    {
        std::cerr << "[ERR] Некорректный порт: " << port << "\n";
        return 1;
    }

    ClientConfig cfg;
    cfg.connectTimeoutMs = 3000;
    cfg.noDelay          = true;

    TCPClient client(host, port, cfg);
    if (!client.Connected())
    {
        const int err = GET_NET_ERROR();
        std::cerr << "[CLIENT ERR] Не удалось подключиться к " << host << ":" << port
                  << " | code=" << err << " (" << SocketErrorToString(err) << ")\n";
        return 1;
    }

    std::mutex ioMtx;
    std::atomic<bool> running{ true };

    {
        std::lock_guard<std::mutex> lock(ioMtx);
        std::cout << "=== Клиент подключён к " << client.RemoteAddress(true)
                  << " (локально: " << client.LocalAddress(true) << ") ===\n"
                  << "Введите сообщение и нажмите Enter (/quit или /exit для выхода).\n";
    }

    // Поток приёма входящих пакетов от сервера.
    std::thread recvThread([&]()
    {
        while (running.load(std::memory_order_relaxed) && client.Connected())
        {
            if (!client.PollRead(100))
                continue;

            ByteBuffer rx(0, ByteOrder::BigEndian);
            if (!client.RecvPacket(rx))
            {
                if (running.load(std::memory_order_relaxed) && client.Connected())
                {
                    const int err = GET_NET_ERROR();
                    std::lock_guard<std::mutex> lock(ioMtx);
                    if (err == 0)
                        std::cout << "\n[CLIENT] Сервер закрыл соединение.\n" << std::flush;
                    else
                        std::cerr << "\n[CLIENT ERR] Ошибка приёма пакета: " << err
                                  << " (" << SocketErrorToString(err) << ")\n" << std::flush;
                }
                running.store(false, std::memory_order_relaxed);
                break;
            }

            std::string reply;
            if (!rx.Empty())
                rx >> reply;

            {
                std::lock_guard<std::mutex> lock(ioMtx);
                std::cout << "\n[Сервер]: " << reply << "\n> " << std::flush;
            }
        }
    });

    std::string line;
    while (running.load(std::memory_order_relaxed) && client.Connected())
    {
        {
            std::lock_guard<std::mutex> lock(ioMtx);
            std::cout << "> " << std::flush;
        }

        if (!std::getline(std::cin, line))
            break;

        if (!running.load(std::memory_order_relaxed) || !client.Connected())
            break;

        if (line == "/quit" || line == "/exit")
        {
            running.store(false, std::memory_order_relaxed);
            break;
        }

        if (line.empty())
            continue;

        ByteBuffer tx(line.size() + 8U, ByteOrder::BigEndian);
        tx << line;

        if (!client.SendPacket(tx))
        {
            const int err = GET_NET_ERROR();
            std::lock_guard<std::mutex> lock(ioMtx);
            std::cerr << "[CLIENT ERR] Не удалось отправить сообщение: " << err
                      << " (" << SocketErrorToString(err) << ")\n";
            running.store(false, std::memory_order_relaxed);
            break;
        }
    }

    running.store(false, std::memory_order_relaxed);
    client.Close();

    if (recvThread.joinable())
        recvThread.join();

    std::cout << "[CLIENT] Отключён. Выход.\n";
    return 0;
}
