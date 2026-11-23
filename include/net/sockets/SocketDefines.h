#ifndef _OTHER_NET_SOCK_DEFINES_H_
#define _OTHER_NET_SOCK_DEFINES_H_

// Подключаем общий базовый заголовок библиотеки.
#include "Define.h"


#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
#   include <iphlpapi.h>
#   include <mswsock.h>

    // Автоматическая линковка необходимых системных библиотек в MSVC.
#   pragma comment(lib, "ws2_32.lib")
#   pragma comment(lib, "iphlpapi.lib")
#else
#   include <sys/socket.h>
#   include <sys/ioctl.h>
#   include <netinet/in.h>
#   include <netinet/tcp.h>
#   include <arpa/inet.h>
#   include <netdb.h>
#   include <unistd.h>
#   include <fcntl.h>
#   include <errno.h>
#   include <string.h>
#   include <poll.h>
#endif

namespace other::net::sockets
{

#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
    typedef SOCKET socket_t;
    typedef int    socklen_t;

#   define GET_NET_ERROR()        WSAGetLastError()
#   define SET_NET_ERROR(err)     WSASetLastError(err)
#   define CLOSE_SOCKET(s)        closesocket(s)
#   define POLL_FUNC              WSAPoll
#else
    typedef int socket_t;
    // В UNIX socklen_t уже объявлен в <sys/socket.h>

#   define GET_NET_ERROR()        errno
#   define SET_NET_ERROR(err)     (errno = (err))
#   define INVALID_SOCKET         (socket_t)(~0)
#   define SOCKET_ERROR           -1
#   define CLOSE_SOCKET(s)        close(s)
#   define POLL_FUNC              poll
#endif

    /**
     * @brief Автоматически вызывает WSAStartup при старте программы и WSACleanup при выходе.
     */
    class WSAInitializer
    {
    public:
        WSAInitializer()
        {
#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
            WSADATA wsaData;
            int err = WSAStartup(MAKEWORD(2, 2), &wsaData);
            if (err != 0)
            {
                std::fprintf(stderr, "WSAStartup failed with error: %d\n", err);
                std::exit(EXIT_FAILURE);
            }

            if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2)
            {
                std::fprintf(stderr, "Could not find a usable version of Winsock.dll\n");
                WSACleanup();
                std::exit(EXIT_FAILURE);
            }
#endif
        }

        ~WSAInitializer()
        {
#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
            WSACleanup();
#endif
        }
    };

////////////////////////////////////////////////////////////////////////
///     УНИФИЦИРОВАННЫЙ КРОССПЛАТФОРМЕННЫЙ СПИСОК СЕТЕВЫХ ОШИБОК     ///
////////////////////////////////////////////////////////////////////////
#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS

    // --- Общие ошибки системных вызовов и аргументов ---
#   define SOCK_EINTR               WSAEINTR               // Прерванный системный вызов (операция прервана сигналом)
#   define SOCK_EBADF               WSAEBADF               // Неверный/недействительный дескриптор файла или сокета
#   define SOCK_EACCES              WSAEACCES              // Отказано в доступе (нет прав на выполнение действия)
#   define SOCK_EFAULT              WSAEFAULT              // Неверный адрес (ошибка доступа к памяти, например, передан невалидный указатель)
#   define SOCK_EINVAL              WSAEINVAL              // Неверный аргумент (передано недопустимое значение в функцию)
#   define SOCK_EMFILE              WSAEMFILE              // Слишком много открытых файлов или сокетов для текущего процесса

// --- Ошибки неблокирующих операций и состояний ---
#   define SOCK_EWOULDBLOCK         WSAEWOULDBLOCK         // Ресурс временно недоступен (операция заблокировала бы неблокирующий сокет)
#   define SOCK_EINPROGRESS         WSAEINPROGRESS         // Операция в процессе выполнения (например, асинхронный connect еще идет)
#   define SOCK_EALREADY            WSAEALREADY            // Операция уже выполняется на данном сокете

// --- Ошибки работы с типами и протоколами сокетов ---
#   define SOCK_ENOTSOCK            WSAENOTSOCK            // Операция применена не к сокету (передан дескриптор обычного файла)
#   define SOCK_EDESTADDRREQ        WSAEDESTADDRREQ        // Требуется адрес назначения (не указан получатель для отправки)
#   define SOCK_EMSGSIZE            WSAEMSGSIZE            // Сообщение слишком длинное для отправки в текущий буфер/протокол
#   define SOCK_EPROTOTYPE          WSAEPROTOTYPE          // Неправильный тип протокола для данного сокета
#   define SOCK_ENOPROTOOPT         WSAENOPROTOOPT         // Неверная или неподдерживаемая опция протокола (ошибка в getsockopt/setsockopt)
#   define SOCK_EPROTONOSUPPORT     WSAEPROTONOSUPPORT     // Указанный протокол не поддерживается системой
#   define SOCK_ESOCKTNOSUPPORT     WSAESOCKTNOSUPPORT     // Тип сокета не поддерживается в данном семействе протоколов
#   define SOCK_EOPNOTSUPP          WSAEOPNOTSUPP          // Операция не поддерживается для данного типа сокета
#   define SOCK_EPFNOSUPPORT        WSAEPFNOSUPPORT        // Семейство протоколов не поддерживается системой
#   define SOCK_EAFNOSUPPORT        WSAEAFNOSUPPORT        // Семейство адресов не поддерживается протоколом (например, IPv6 на машине без поддержки)

// --- Ошибки привязки (bind) и сетевых интерфейсов ---
#   define SOCK_EADDRINUSE          WSAEADDRINUSE          // Адрес уже используется (порт уже занят другим приложением)
#   define SOCK_EADDRNOTAVAIL       WSAEADDRNOTAVAIL       // Запрошенный адрес не может быть назначен (например, несуществующий IP-адрес)
#   define SOCK_ENETDOWN            WSAENETDOWN            // Сеть повреждена / отключена (сетевой интерфейс не работает)
#   define SOCK_ENETUNREACH         WSAENETUNREACH         // Сеть недоступна (нет физического пути или маршрута до сети)
#   define SOCK_ENETRESET           WSAENETRESET           // Сеть сбросила соединение из-за сбоя оборудования или сброса
#   define SOCK_ECONNABORTED        WSAECONNABORTED        // Программный отказ соединения (соединение разорвано локально по тайм-ауту или из-за сбоя)
#   define SOCK_ECONNRESET          WSAECONNRESET          // Соединение разорвано удаленной стороной (удаленный хост резко сбросил сессию)
#   define SOCK_ENOBUFS             WSAENOBUFS             // Недостаточно свободной буферной памяти в системе для отправки/приема
#   define SOCK_EISCONN             WSAEISCONN             // Сокет уже подключен (попытка вызвать connect на уже соединенном сокете)
#   define SOCK_ENOTCONN            WSAENOTCONN            // Сокет не подключен (попытка отправки данных через сокет, который еще не соединен)
#   define SOCK_ESHUTDOWN           WSAESHUTDOWN           // Нельзя отправить данные: сокет уже закрыт для отправки (вызван shutdown)
#   define SOCK_ETOOМANYREFS        WSAETOOMANYREFS        // Слишком много ссылок на объект ядра
#   define SOCK_ETIMEDOUT           WSAETIMEDOUT           // Истекло время ожидания (тайм-аут соединения или сетевой операции)
#   define SOCK_ECONNREFUSED        WSAECONNREFUSED        // Соединение отклонено (на удаленном хосте никто не слушает этот порт)

// --- Ошибки путей, хостов и окружения ---
#   define SOCK_ELOOP               WSAELOOP               // Слишком многоуровневая вложенность символьных ссылок
#   define SOCK_ENAMETOOLONG        WSAENAMETOOLONG        // Имя файла или компонента пути слишком длинное
#   define SOCK_EHOSTDOWN           WSAEHOSTDOWN           // Удаленный хост выключен (лежит)
#   define SOCK_EHOSTUNREACH        WSAEHOSTUNREACH        // Нет маршрута к удаленному хосту
#   define SOCK_ENOTEMPTY           WSAENOTEMPTY           // Директория не пуста
#   define SOCK_EPROCLIM            WSAEPROCLIM            // Превышен лимит процессов в системе
#   define SOCK_EUSERS              WSAEUSERS              // Превышен лимит пользователей
#   define SOCK_EDQUOT              WSAEDQUOT              // Превышена дисковая квота
#   define SOCK_ESTALE              WSAESTALE              // Устаревшая ссылка на файл или ресурс
#   define SOCK_EREMOTE             WSAEREMOTE             // Запрошенный элемент является удаленным

// --- Специфичные ошибки инициализации и работы Winsock в Windows ---
#   define SOCK_SYSNOTREADY         WSASYSNOTREADY         // Подсистема сетевого окружения не готова (драйвер стека TCP/IP не загружен)
#   define SOCK_VERNOTSUPPORTED     WSAVERNOTSUPPORTED     // Запрошенная версия Winsock.dll не поддерживается данной библиотекой
#   define SOCK_NOTINITIALISED      WSANOTINITIALISED      // Успешный вызов WSAStartup еще не был выполнен (библиотека сокетов не инициализирована)
#   define SOCK_EDISCON             WSAEDISCON             // Выполняется плавное (graceful) завершение соединения
#   define SOCK_ENOMORE             WSAENOMORE             // Больше нет результатов для запроса (службы имен)
#   define SOCK_ECANCELLED          WSAECANCELLED          // Асинхронный вызов был отменен
#   define SOCK_EINVALIDPROCTABLE   WSAEINVALIDPROCTABLE   // Таблица процедур службы поставщика недействительна
#   define SOCK_EINVALIDPROVIDER    WSAEINVALIDPROVIDER    // Запрошенный поставщик услуг недействителен или поврежден
#   define SOCK_EPROVIDERFAILEDINIT WSAEPROVIDERFAILEDINIT // Поставщик услуг не смог инициализироваться
#   define SOCK_ESYSCALLFAILURE     WSASYSCALLFAILURE      // Критический сбой системного вызова в недрах ОС
#   define SOCK_SERVICENOTFOUND     WSASERVICE_NOT_FOUND   // Сетевая служба не найдена
#   define SOCK_TYPENOTFOUND        WSATYPE_NOT_FOUND      // Запрошенный тип класса не найден
#   define SOCK_HOSTNOTFOUND        WSAHOST_NOT_FOUND      // Хост не найден (ошибка DNS-разрешения)
#   define SOCK_TRYAGAIN            WSATRY_AGAIN           // Временная ошибка разрешения имен (попробуйте повторить позже)
#   define SOCK_NORECOVERY          WSANO_RECOVERY         // Неисправимая ошибка при поиске имени в DNS
#   define SOCK_NODATA              WSANO_DATA            // Имя найдено, но у него нет записи запрашиваемого типа (например, нет A-записи)

#else

    // --- POSIX (Linux / macOS) эквиваленты ошибок ---
#   define SOCK_EINTR               EINTR
#   define SOCK_EBADF               EBADF
#   define SOCK_EACCES              EACCES
#   define SOCK_EFAULT              EFAULT
#   define SOCK_EINVAL              EINVAL
#   define SOCK_EMFILE              EMFILE
#   define SOCK_EWOULDBLOCK         EWOULDBLOCK
#   define SOCK_EINPROGRESS         EINPROGRESS
#   define SOCK_EALREADY            EALREADY
#   define SOCK_ENOTSOCK            ENOTSOCK
#   define SOCK_EDESTADDRREQ        EDESTADDRREQ
#   define SOCK_EMSGSIZE            EMSGSIZE
#   define SOCK_EPROTOTYPE          EPROTOTYPE
#   define SOCK_ENOPROTOOPT         ENOPROTOOPT
#   define SOCK_EPROTONOSUPPORT     EPROTONOSUPPORT
#   define SOCK_ESOCKTNOSUPPORT     ESOCKTNOSUPPORT
#   define SOCK_EOPNOTSUPP          EOPNOTSUPP
#   define SOCK_EPFNOSUPPORT        EPFNOSUPPORT
#   define SOCK_EAFNOSUPPORT        EAFNOSUPPORT
#   define SOCK_EADDRINUSE          EADDRINUSE
#   define SOCK_EADDRNOTAVAIL       EADDRNOTAVAIL
#   define SOCK_ENETDOWN            ENETDOWN
#   define SOCK_ENETUNREACH         ENETUNREACH
#   define SOCK_ENETRESET           ENETRESET
#   define SOCK_ECONNABORTED        ECONNABORTED
#   define SOCK_ECONNRESET          ECONNRESET
#   define SOCK_ENOBUFS             ENOBUFS
#   define SOCK_EISCONN             EISCONN
#   define SOCK_ENOTCONN            ENOTCONN
#   define SOCK_ESHUTDOWN           ESHUTDOWN
#   define SOCK_ETOOМANYREFS        ELOOP
#   define SOCK_ETIMEDOUT           ETIMEDOUT
#   define SOCK_ECONNREFUSED        ECONNREFUSED
#   define SOCK_ELOOP               ELOOP
#   define SOCK_ENAMETOOLONG        ENAMETOOLONG
#   define SOCK_EHOSTDOWN           EHOSTDOWN
#   define SOCK_EHOSTUNREACH        EHOSTUNREACH
#   define SOCK_ENOTEMPTY           ENOTEMPTY
#   define SOCK_EPROCLIM            EAGAIN
#   define SOCK_EUSERS              EUSERS
#   define SOCK_EDQUOT              EDQUOT
#   define SOCK_ESTALE              ESTALE
#   define SOCK_EREMOTE             EREMOTE

// Безопасные заглушки для чисто виндовых специфичных макросов под Linux
#   define SOCK_SYSNOTREADY         ENOSYS
#   define SOCK_VERNOTSUPPORTED     ENOSYS
#   define SOCK_NOTINITIALISED      ENOSYS
#   define SOCK_EDISCON             ESHUTDOWN
#   define SOCK_ENOMORE             ENODATA
#   define SOCK_ECANCELLED          ECANCELED
#   define SOCK_EINVALIDPROCTABLE   EINVAL
#   define SOCK_EINVALIDPROVIDER    EINVAL
#   define SOCK_EPROVIDERFAILEDINIT EIO
#   define SOCK_ESYSCALLFAILURE     EIO
#   define SOCK_SERVICENOTFOUND     ENOENT
#   define SOCK_TYPENOTFOUND        ENOENT
#   define SOCK_HOSTNOTFOUND        HOST_NOT_FOUND
#   define SOCK_TRYAGAIN            TRY_AGAIN
#   define SOCK_NORECOVERY          NO_RECOVERY
#   define SOCK_NODATA              NO_DATA

#endif

    inline std::string SocketErrorToString(int nErrorId)
    {
        switch (nErrorId)
        {
        case SOCK_EINTR:               return "Interrupted function call.";
        case SOCK_EBADF:               return "File handle is not valid.";
        case SOCK_EACCES:              return "Permission denied.";
        case SOCK_EFAULT:              return "Bad address.";
        case SOCK_EINVAL:              return "Invalid argument.";
        case SOCK_EMFILE:              return "Too many open files.";
        case SOCK_EWOULDBLOCK:         return "Resource temporarily unavailable (Would block).";
        case SOCK_EINPROGRESS:         return "Operation now in progress.";
        case SOCK_EALREADY:            return "Operation already in progress.";
        case SOCK_ENOTSOCK:            return "Socket operation on nonsocket.";
        case SOCK_EDESTADDRREQ:        return "Destination address required.";
        case SOCK_EMSGSIZE:            return "Message too long.";
        case SOCK_EPROTOTYPE:          return "Protocol wrong type for socket.";
        case SOCK_ENOPROTOOPT:         return "Bad protocol option.";
        case SOCK_EPROTONOSUPPORT:     return "Protocol not supported.";
        case SOCK_ESOCKTNOSUPPORT:     return "Socket type not supported.";
        case SOCK_EOPNOTSUPP:          return "Operation not supported.";
        case SOCK_EPFNOSUPPORT:        return "Protocol family not supported.";
        case SOCK_EAFNOSUPPORT:        return "Address family not supported by protocol family.";
        case SOCK_EADDRINUSE:          return "Address already in use.";
        case SOCK_EADDRNOTAVAIL:       return "Cannot assign requested address.";
        case SOCK_ENETDOWN:            return "Network is down.";
        case SOCK_ENETUNREACH:         return "Network is unreachable.";
        case SOCK_ENETRESET:           return "Network dropped connection on reset.";
        case SOCK_ECONNABORTED:        return "Software caused connection abort.";
        case SOCK_ECONNRESET:          return "Connection reset by peer.";
        case SOCK_ENOBUFS:             return "No buffer space available.";
        case SOCK_EISCONN:             return "Socket is already connected.";
        case SOCK_ENOTCONN:            return "Socket is not connected.";
        case SOCK_ESHUTDOWN:           return "Cannot send after socket shutdown.";
        case SOCK_ETOOМANYREFS:        return "Too many references.";
        case SOCK_ETIMEDOUT:           return "Connection timed out.";
        case SOCK_ECONNREFUSED:        return "Connection refused.";
        case SOCK_ELOOP:               return "Cannot translate name.";
        case SOCK_ENAMETOOLONG:        return "Name component or name was too long.";
        case SOCK_EHOSTDOWN:           return "Host is down.";
        case SOCK_EHOSTUNREACH:        return "No route to host.";
        case SOCK_ENOTEMPTY:           return "Directory not empty.";
        case SOCK_EPROCLIM:            return "Too many processes.";
        case SOCK_EUSERS:              return "Too many users.";
        case SOCK_EDQUOT:              return "Disk quota exceeded.";
        case SOCK_ESTALE:              return "Stale file handle reference.";
        case SOCK_EREMOTE:             return "Item is remote.";
        case SOCK_SYSNOTREADY:         return "Network subsystem is unavailable.";
        case SOCK_VERNOTSUPPORTED:     return "Winsock.dll version out of range.";
        case SOCK_NOTINITIALISED:      return "Successful WSAStartup not yet performed.";
        case SOCK_EDISCON:             return "Graceful shutdown in progress.";
        case SOCK_ENOMORE:             return "No more results.";
        case SOCK_ECANCELLED:          return "Call has been cancelled.";
        case SOCK_EINVALIDPROCTABLE:   return "Procedure call table is invalid.";
        case SOCK_EINVALIDPROVIDER:    return "Service provider is invalid.";
        case SOCK_EPROVIDERFAILEDINIT: return "Service provider failed to initialize.";
        case SOCK_ESYSCALLFAILURE:     return "System call failure.";
        case SOCK_SERVICENOTFOUND:     return "Service not found.";
        case SOCK_TYPENOTFOUND:        return "Class type not found.";
        case SOCK_HOSTNOTFOUND:        return "Host not found.";
        case SOCK_TRYAGAIN:            return "Nonauthoritative host not found.";
        case SOCK_NORECOVERY:          return "This is a nonrecoverable error.";
        case SOCK_NODATA:              return "Valid name, no data record of requested type.";
        default:                       return "Unknown Socket error: " + std::to_string(nErrorId);
        }
    }

    /**
     * @brief Проверяет результат системного вызова и логирует ошибку при её наличии.
     */
    inline int CheckSocketError(int result, const std::string& msg)
    {
        if (result == SOCKET_ERROR)
        {
            int errorId = GET_NET_ERROR();
            std::cout << "Error: " << msg << " | " << SocketErrorToString(errorId) << std::endl;
            return -1;
        }
        return 0;
    }

}

#endif // _OTHER_NET_SOCK_DEFINES_H_
