/**
 * @file PrivateNet.hpp
 * @brief Внутренние платформозависимые вспомогательные функции сетевого модуля.
 *
 * @details
 * Этот заголовок предназначен исключительно для внутреннего использования внутри реализации
 * сетевой библиотеки и не является частью публичного API. Его задача — изолировать низкоуровневые
 * различия между сетевыми стеками Windows (Winsock2) и Linux (POSIX), чтобы основной код классов
 * сокетов оставался чистым, читаемым и переносимым.
 *
 * В файле сосредоточена служебная логика, обеспечивающая единообразное поведение на обеих платформах:
 * защита от аварийного завершения процесса при записи в разорванное соединение, распознавание
 * кодов временной неготовности неблокирующих операций, сохранение ошибок разрешения доменных имён,
 * преобразование сетевых адресов для режима Dual-Stack (IPv4 поверх IPv6-сокета), устойчивый
 * к прерыванию сигналами опрос одного дескриптора и перевод временных интервалов Keep-Alive
 * между секундами и платформенными единицами.
 *
 * Все функции объявлены как `inline` и `noexcept` — они не выделяют динамическую память
 * и не выбрасывают исключений.
 */

#ifndef LIBOTHER_PRIVATENET_HPP
#define LIBOTHER_PRIVATENET_HPP

#include "Define.h"


namespace other
{
    namespace net
    {
        namespace sockets
        {
            /**
             * @brief Дополняет флаги отправки защитой от сигнала `SIGPIPE` на POSIX-платформах.
             *
             * На Linux попытка отправить данные в TCP-сокет, закрытый удалённой стороной, по умолчанию
             * генерирует сигнал `SIGPIPE`, аварийно завершающий процесс. Добавление флага `MSG_NOSIGNAL`
             * подавляет сигнал, заставляя системный вызов просто вернуть `-1` с ошибкой `EPIPE`.
             * @param [in] flags Исходная битовая маска флагов для системных вызовов отправки (`::send` / `::sendto`).
             * @return Модифицированная маска флагов: на Linux с установленным `MSG_NOSIGNAL`; на Windows возвращается без изменений.
             * @note Гарантированно не выбрасывает исключений (`noexcept`).
             */
            inline int ApplyNoSignalFlag(int flags) noexcept
            {
#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
                return flags;
#else
                return flags | MSG_NOSIGNAL;
#endif
            }

            /**
             * @brief Проверяет, обозначает ли системный код ошибки временную неготовность неблокирующего сокета.
             *
             * Используется для отделения штатной ситуации «данных пока нет / буфер отправки заполнен»
             * от реального сетевого сбоя при работе в неблокирующем режиме.
             * @param [in] err Системный код ошибки, полученный после неудачной операции ввода-вывода.
             * @return `true`, если код соответствует временной блокировке (`WSAEWOULDBLOCK` на Windows;
             *         `EWOULDBLOCK` или `EAGAIN` на Linux); `false` для всех остальных кодов ошибок.
             * @note Гарантированно не выбрасывает исключений (`noexcept`).
             */
            inline bool IsWouldBlockError(int err) noexcept
            {
#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
                return (err == WSAEWOULDBLOCK);
#else
                return (err == EWOULDBLOCK || err == EAGAIN);
#endif
            }

            /**
             * @brief Сохраняет код ошибки разрешения адреса (`::getaddrinfo()`) в состояние ошибок текущего потока.
             *
             * При успешном завершении (`gaiErr == 0`) состояние ошибок не изменяется. На Linux при коде
             * `NET_EAI_SYSTEM` реальная причина системного сбоя уже записана в `errno`, поэтому перезапись
             * также не выполняется.
             * @param [in] gaiErr Код возврата системной функции `::getaddrinfo()`.
             * @note Гарантированно не выбрасывает исключений (`noexcept`).
             */
            inline void SaveAddrInfoError(int gaiErr) noexcept
            {
                if (gaiErr != 0 && gaiErr != NET_EAI_SYSTEM)
                    SET_NET_ERROR(gaiErr);
            }

            /**
             * @brief Подготавливает бинарную структуру сетевого адреса для системных вызовов `bind`, `connect` или `sendto`.
             *
             * Если семейство исходного адреса совпадает с семейством сокета, структура копируется напрямую.
             * Если сокет открыт как IPv6 с включённым режимом Dual-Stack, а исходный адрес является IPv4,
             * функция автоматически упаковывает его в формат IPv4-mapped IPv6 (`::ffff:a.b.c.d`).
             * @param [in]  sockFamily Семейство адресов целевого сокета (`AF_INET` или `AF_INET6`).
             * @param [in]  dualMode   Флаг активности Dual-Stack на IPv6-сокете (`true` разрешает преобразование IPv4 -> IPv6).
             * @param [in]  srcAddr    Указатель на исходную структуру адреса (не может быть `nullptr`).
             * @param [in]  srcLen     Длина исходной структуры `srcAddr` в байтах (должна быть `> 0` и не превышать `sizeof(sockaddr_storage)`).
             * @param [out] outAddr    Выходной буфер, в который записывается подготовленный адрес (предварительно обнуляется).
             * @param [out] outLen     Выходная фактическая длина подготовленной структуры в `outAddr` в байтах.
             * @return `true`, если адрес успешно подготовлен и совместим с сокетом;
             *         `false` при неверных аргументах, переполнении буфера или несовместимых семействах адресов.
             * @note Гарантированно не выбрасывает исключений (`noexcept`).
             */
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

            /**
             * @brief Опрашивает готовность одного сокета через `poll`/`WSAPoll` с защитой от переполнения таймаута и `EINTR`.
             *
             * На Linux автоматически перезапускает ожидание при прерывании системного вызова сигналом (`SOCK_EINTR`),
             * точно пересчитывая остаток времени до исходного дедлайна по монотонным часам.
             * @param [in]  handle     Системный дескриптор опрашиваемого сокета.
             * @param [in]  events     Битовая маска интересующих событий (например `POLLIN`, `POLLOUT` или `0` для проверки только ошибок).
             * @param [in]  timeoutMs  Максимальное время ожидания в миллисекундах:
             *                         `< 0` — бесконечное ожидание; `0` — мгновенная проверка без блокировки;
             *                         `> 0` — ограничение времени (значения больше `INT_MAX` ограничиваются `INT_MAX`).
             * @param [out] outRevents Битовая маска фактически сработавших событий (обнуляется в начале вызова и заполняется при возврате `> 0`).
             * @return `> 0`, если на сокете зафиксировано событие; `0`, если истёк таймаут ожидания;
             *         `-1` при системной ошибке опроса (код доступен через `GET_NET_ERROR()`).
             * @note Гарантированно не выбрасывает исключений (`noexcept`).
             */
            inline int PollSingle(socket_t handle, short events, long timeoutMs, short& outRevents) noexcept
            {
                outRevents = 0;

                // Приводим таймаут к диапазону int (< 0 означает бесконечное ожидание).
                int remainingMs = -1;
                if (timeoutMs >= 0)
                {
                    constexpr int maxInt = std::numeric_limits<int>::max();
                    remainingMs = (timeoutMs > maxInt) ? maxInt : static_cast<int>(timeoutMs);
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

            /**
             * @brief Переводит интервал TCP Keep-Alive из секунд в единицы измерения текущей ОС для `setsockopt`.
             *
             * На Windows переводит секунды в миллисекунды с проверкой на переполнение `int`;
             * на Linux/POSIX возвращает значение в секундах без изменений.
             * @param [in] seconds Желаемый интервал в секундах (должен быть `> 0`).
             * @return Интервал в платформенных единицах (`> 0` при корректном входе) либо `-1` на Windows,
             *         если умножение на `1000` приводит к переполнению типа `int`.
             * @note Гарантированно не выбрасывает исключений (`noexcept`).
             */
            inline int ToPlatformKeepAliveInterval(int seconds) noexcept
            {
#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
                constexpr int maxSec = std::numeric_limits<int>::max() / 1000;
                return (seconds > maxSec) ? -1 : (seconds * 1000);
#else
                return seconds;
#endif
            }

            /**
             * @brief Переводит интервал TCP Keep-Alive из платформенных единиц `getsockopt` обратно в секунды.
             *
             * На Windows выполняет целочисленное деление миллисекунд на `1000`;
             * на Linux/POSIX возвращает значение в секундах без изменений.
             * @param [in] rawValue Сырое значение интервала, полученное от операционной системы.
             * @return Значение интервала в целых секундах.
             * @note Гарантированно не выбрасывает исключений (`noexcept`).
             */
            inline int FromPlatformKeepAliveInterval(int rawValue) noexcept
            {
#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
                return rawValue / 1000;
#else
                return rawValue;
#endif
            }

        } // !End namespace sockets


    } // !End namespace net


} // !End namespace other

#endif // LIBOTHER_PRIVATENET_HPP
