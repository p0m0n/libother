#ifndef LIBOTHER_PRIVATENET_HPP
#define LIBOTHER_PRIVATENET_HPP

#include "Define.h"

namespace other
{
    namespace net
    {
        namespace sockets
        {
            // На Linux попытка отправить данные в TCP-сокет, закрытый удалённой стороной, по умолчанию
            // генерирует сигнал SIGPIPE, аварийно завершающий процесс. Флаг MSG_NOSIGNAL отключает SIGPIPE:
            // вместо падения ::send() возвращает -1 с ошибкой EPIPE.
            inline int ApplyNoSignalFlag(int flags) noexcept
            {
#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
                return flags;
#else
                return flags | MSG_NOSIGNAL;
#endif
            }

            // Проверяет, означает ли код ошибки временную недоступность ресурса на неблокирующем сокете.
            inline bool IsWouldBlockError(int err) noexcept
            {
#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
                return (err == WSAEWOULDBLOCK);
#else
                return (err == EWOULDBLOCK || err == EAGAIN);
#endif
            }

            // Сохраняет код ошибки getaddrinfo() в системный код ошибки потока (GET_NET_ERROR()).
            // На Linux при EAI_SYSTEM реальный код системной ошибки уже находится в errno.
            inline void SaveAddrInfoError(int gaiErr) noexcept
            {
                if (gaiErr != 0 && gaiErr != NET_EAI_SYSTEM)
                    SET_NET_ERROR(gaiErr);
            }

            // Подготавливает бинарную структуру sockaddr для bind/connect/sendto.
            // Для Dual-Stack IPv6-сокета автоматически конвертирует IPv4-адрес в IPv4-mapped IPv6 (::ffff:x.x.x.x).
            inline bool PrepareTargetAddress(int sockFamily, bool dualMode, const sockaddr* srcAddr, socklen_t srcLen, sockaddr_storage& outAddr, socklen_t& outLen) noexcept
            {
                if (srcAddr == nullptr || srcLen <= 0)
                    return false;

                std::memset(&outAddr, 0, sizeof(outAddr));

                // Прямое совпадение семейства адресов (IPv4 -> IPv4 или IPv6 -> IPv6).
                if (srcAddr->sa_family == sockFamily)
                {
                    if (static_cast<size_t>(srcLen) > sizeof(outAddr))
                        return false;
                    std::memcpy(&outAddr, srcAddr, static_cast<size_t>(srcLen));
                    outLen = srcLen;
                    return true;
                }

                // Для Dual-Stack IPv6-сокета упаковываем IPv4 в формат ::ffff:a.b.c.d.
                if (sockFamily == AF_INET6 && dualMode && srcAddr->sa_family == AF_INET)
                {
                    const sockaddr_in* ipv4 = reinterpret_cast<const sockaddr_in*>(srcAddr);
                    sockaddr_in6* ipv6 = reinterpret_cast<sockaddr_in6*>(&outAddr);

                    ipv6->sin6_family = AF_INET6;
                    ipv6->sin6_port = ipv4->sin_port;
                    ipv6->sin6_addr.s6_addr[10] = 0xFF;
                    ipv6->sin6_addr.s6_addr[11] = 0xFF;
                    std::memcpy(&ipv6->sin6_addr.s6_addr[12], &ipv4->sin_addr, sizeof(ipv4->sin_addr));

                    outLen = static_cast<socklen_t>(sizeof(sockaddr_in6));
                    return true;
                }

                // Несовместимое семейство адресов.
                return false;
            }

            // Опрашивает один дескриптор через poll/WSAPoll с защитой от переполнения long -> int и автоматическим перезапуском при прерывании сигналом (EINTR) на Linux.
            inline int PollSingle(socket_t handle, short events, long timeoutMs, short& outRevents) noexcept
            {
                outRevents = 0;

                // Приводим таймаут к диапазону int (< 0 означает бесконечное ожидание).
                int remainingMs = -1;
                if (timeoutMs >= 0)
                {
                    constexpr int maxInt = std::numeric_limits<int>::max();
                    remainingMs = (timeoutMs > maxInt ? maxInt : timeoutMs);
                }

#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
                pollfd pfd = {};
                pfd.fd = handle;
                pfd.events = events;

                int res = POLL_FUNC(&pfd, 1, remainingMs);
                if (res > 0)
                    outRevents = pfd.revents;
                return res;
#else
                using Clock = std::chrono::steady_clock;
                const auto deadline = (remainingMs > 0) ? (Clock::now() + std::chrono::milliseconds(remainingMs)) : Clock::time_point{};

                while (true)
                {
                    pollfd pfd = {};
                    pfd.fd = handle;
                    pfd.events = events;

                    int res = POLL_FUNC(&pfd, 1, remainingMs);
                    if (res >= 0)
                    {
                        if (res > 0)
                            outRevents = pfd.revents;
                        return res;
                    }

                    // Прекращаем ожидание при любой ошибке, кроме прерывания сигналом ОС.
                    if (GET_NET_ERROR() != SOCK_EINTR)
                        return -1;

                    // Для конечного таймаута пересчитываем оставшееся время до дедлайна.
                    if (remainingMs > 0)
                    {
                        const auto now = Clock::now();
                        if (now >= deadline)
                            return 0;
                        remainingMs = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
                    }
                }
#endif
            }

        } // !End namespace sockets


    } // !End namespace net


} // !End namespace other

#endif // LIBOTHER_PRIVATENET_HPP
