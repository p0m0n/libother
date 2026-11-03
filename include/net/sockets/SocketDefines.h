#ifndef _OTHER_NET_SOCKET_DEFINES_H_
#define _OTHER_NET_SOCKET_DEFINES_H_


#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <mswsock.h>

// Автоматическая линковка необходимых системных библиотек в MSVC
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <poll.h>
#endif

// Подключаем общий базовый заголовок библиотеки
#include "Define.h"

namespace other::net::sockets
{

#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
    typedef SOCKET socket_t;
    typedef int    socklen_t;

#define GET_NET_ERROR()        WSAGetLastError()
#define CLOSE_SOCKET(s)        closesocket(s)
#define POLL_FUNC              WSAPoll
#else
    typedef int socket_t;
    // В UNIX socklen_t уже объявлен в <sys/socket.h>

#define GET_NET_ERROR()        errno
#define INVALID_SOCKET         (socket_t)(~0)
#define SOCKET_ERROR           -1
#define CLOSE_SOCKET(s)        close(s)
#define POLL_FUNC              poll
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

    /**
     * @brief Преобразует численный код сетевой ошибки в понятную строку.
     * Использует thread_local буфер для потокобезопасности.
     */
    inline const char* SocketErrorToString(int nErrorId)
    {
        static thread_local char tmp[256];

#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
        switch (nErrorId)
        {
        case WSAEINTR:                 return "Interrupted function call.";
        case WSAEBADF:                 return "File handle is not valid.";
        case WSAEACCES:                return "Permission denied.";
        case WSAEFAULT:                return "Bad address.";
        case WSAEINVAL:                return "Invalid argument.";
        case WSAEMFILE:                return "Too many open files.";
        case WSAEWOULDBLOCK:           return "Resource temporarily unavailable (Would block).";
        case WSAEINPROGRESS:           return "Operation now in progress.";
        case WSAEALREADY:              return "Operation already in progress.";
        case WSAENOTSOCK:              return "Socket operation on nonsocket.";
        case WSAEDESTADDRREQ:          return "Destination address required.";
        case WSAEMSGSIZE:              return "Message too long.";
        case WSAEPROTOTYPE:            return "Protocol wrong type for socket.";
        case WSAENOPROTOOPT:           return "Bad protocol option.";
        case WSAEPROTONOSUPPORT:       return "Protocol not supported.";
        case WSAESOCKTNOSUPPORT:       return "Socket type not supported.";
        case WSAEOPNOTSUPP:            return "Operation not supported.";
        case WSAEPFNOSUPPORT:          return "Protocol family not supported.";
        case WSAEAFNOSUPPORT:          return "Address family not supported by protocol family.";
        case WSAEADDRINUSE:            return "Address already in use.";
        case WSAEADDRNOTAVAIL:         return "Cannot assign requested address.";
        case WSAENETDOWN:              return "Network is down.";
        case WSAENETUNREACH:           return "Network is unreachable.";
        case WSAENETRESET:             return "Network dropped connection on reset.";
        case WSAECONNABORTED:          return "Software caused connection abort.";
        case WSAECONNRESET:            return "Connection reset by peer.";
        case WSAENOBUFS:               return "No buffer space available.";
        case WSAEISCONN:               return "Socket is already connected.";
        case WSAENOTCONN:              return "Socket is not connected.";
        case WSAESHUTDOWN:             return "Cannot send after socket shutdown.";
        case WSAETOOMANYREFS:          return "Too many references.";
        case WSAETIMEDOUT:             return "Connection timed out.";
        case WSAECONNREFUSED:          return "Connection refused.";
        case WSAELOOP:                 return "Cannot translate name.";
        case WSAENAMETOOLONG:          return "Name component or name was too long.";
        case WSAEHOSTDOWN:             return "Host is down.";
        case WSAEHOSTUNREACH:          return "No route to host.";
        case WSAENOTEMPTY:             return "Directory not empty.";
        case WSAEPROCLIM:              return "Too many processes.";
        case WSAEUSERS:                return "Too many users.";
        case WSAEDQUOT:                return "Disk quota exceeded.";
        case WSAESTALE:                return "Stale file handle reference.";
        case WSAEREMOTE:               return "Item is remote.";
        case WSASYSNOTREADY:           return "Network subsystem is unavailable.";
        case WSAVERNOTSUPPORTED:       return "Winsock.dll version out of range.";
        case WSANOTINITIALISED:        return "Successful WSAStartup not yet performed.";
        case WSAEDISCON:               return "Graceful shutdown in progress.";
        case WSAENOMORE:               return "No more results.";
        case WSAECANCELLED:            return "Call has been cancelled.";
        case WSAEINVALIDPROCTABLE:     return "Procedure call table is invalid.";
        case WSAEINVALIDPROVIDER:      return "Service provider is invalid.";
        case WSAEPROVIDERFAILEDINIT:   return "Service provider failed to initialize.";
        case WSASYSCALLFAILURE:        return "System call failure.";
        case WSASERVICE_NOT_FOUND:     return "Service not found.";
        case WSATYPE_NOT_FOUND:        return "Class type not found.";
        case WSAHOST_NOT_FOUND:        return "Host not found.";
        case WSATRY_AGAIN:             return "Nonauthoritative host not found.";
        case WSANO_RECOVERY:           return "This is a nonrecoverable error.";
        case WSANO_DATA:               return "Valid name, no data record of requested type.";
        default: break;
        }
        std::snprintf(tmp, sizeof(tmp), "Unknown Socket error: %d", nErrorId);
        return tmp;
#else
        const char* s = std::strerror(nErrorId);
        return s ? s : "Unknown Socket error";
#endif
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

} // namespace other::net::sockets

#endif // _OTHER_NET_SOCKET_DEFINES_H_
