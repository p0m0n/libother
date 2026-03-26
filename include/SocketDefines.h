#ifndef _OTHER_NET_SOCKET_DEFINES_H_
#define _OTHER_NET_SOCKET_DEFINES_H_

#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
#   ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#   endif
#
#   include <winsock2.h>
#   include <ws2tcpip.h>
#   include <iphlpapi.h>
#   include "Define.h"
#
#   pragma comment(lib, "ws2_32.lib")
#   pragma comment(lib, "iphlpapi.lib")
#else
#   include <sys/socket.h>
#   include <arpa/inet.h>
#   include <netdb.h>
#   include <unistd.h>
#   include <errno.h>
#   include <string.h>
#endif

namespace other::net::sockets
{
#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
    typedef SOCKET socket_t;
#   define GET_NET_ERROR()         WSAGetLastError()
#   define CLOSE_SOCKET(s)         closesocket(s)
#else
    typedef int socket_t;
#   define GET_NET_ERROR()         errno
#   define INVALID_SOCKET          (socket_t)(~0)
#   define SOCKET_ERROR            -1
#   define CLOSE_SOCKET(s)         close(s)
#endif

    // Класс для автоматической инициализации Winsock
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
                /* Tell the user that we could not find a usable */
                /* Winsock DLL.                                  */
                printf("WSAStartup failed with error: %d\n", err);
                exit(1);
            }

            if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
                /* Tell the user that we could not find a usable */
                /* WinSock DLL.                                  */
                printf("Could not find a usable version of Winsock.dll\n");
                WSACleanup();
                exit(1);
            }
            /*else
                printf("The Winsock 2.2 dll was found okay\n");*/
#endif
        }

        ~WSAInitializer()
        {
#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
            WSACleanup();
#endif
        }
    };

    inline const char* SocketErrorToString(int nErrorId)
    {
        static thread_local char tmp[256];

#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
        switch (nErrorId)
        {
        case WSAEINTR:                  return "Interrupted function call.";
        case WSAEBADF:                  return "File handle is not valid.";
        case WSAEACCES:                 return "Permission denied.";
        case WSAEFAULT:                 return "Bad address.";
        case WSAEINVAL:                 return "Invalid argument.";
        case WSAEMFILE:                 return "Too many open files.";
        case WSAEWOULDBLOCK:            return "Resource temporarily unavailable (Would block).";
        case WSAEINPROGRESS:            return "Operation now in progress.";
        case WSAEALREADY:               return "Operation already in progress.";
        case WSAENOTSOCK:               return "Socket operation on nonsocket.";
        case WSAEDESTADDRREQ:           return "Destination address required.";
        case WSAEMSGSIZE:               return "Message too long.";
        case WSAEPROTOTYPE:             return "Protocol wrong type for socket.";
        case WSAENOPROTOOPT:            return "Bad protocol option.";
        case WSAEPROTONOSUPPORT:        return "Protocol not supported.";
        case WSAESOCKTNOSUPPORT:        return "Socket type not supported.";
        case WSAEOPNOTSUPP:             return "Operation not supported.";
        case WSAEPFNOSUPPORT:           return "Protocol family not supported.";
        case WSAEAFNOSUPPORT:           return "Address family not supported by protocol family.";
        case WSAEADDRINUSE:             return "Address already in use.";
        case WSAEADDRNOTAVAIL:          return "Cannot assign requested address.";
        case WSAENETDOWN:               return "Network is down.";
        case WSAENETUNREACH:            return "Network is unreachable.";
        case WSAENETRESET:              return "Network dropped connection on reset.";
        case WSAECONNABORTED:           return "Software caused connection abort.";
        case WSAECONNRESET:             return "Connection reset by peer.";
        case WSAENOBUFS:                return "No buffer space available.";
        case WSAEISCONN:                return "Socket is already connected.";
        case WSAENOTCONN:               return "Socket is not connected.";
        case WSAESHUTDOWN:              return "Cannot send after socket shutdown.";
        case WSAETOOMANYREFS:           return "Too many references.";
        case WSAETIMEDOUT:              return "Connection timed out.";
        case WSAECONNREFUSED:           return "Connection refused.";
        case WSAELOOP:                  return "Cannot translate name.";
        case WSAENAMETOOLONG:           return "Name component or name was too long.";
        case WSAEHOSTDOWN:              return "Host is down.";
        case WSAEHOSTUNREACH:           return "No route to host.";
        case WSAENOTEMPTY:              return "Directory not empty.";
        case WSAEPROCLIM:               return "Too many processes.";
        case WSAEUSERS:                 return "Too many users.";
        case WSAEDQUOT:                 return "Disk quota exceeded.";
        case WSAESTALE:                 return "Stale file handle reference.";
        case WSAEREMOTE:                return "Item is remote.";
        case WSASYSNOTREADY:            return "Network subsystem is unavailable.";
        case WSAVERNOTSUPPORTED:        return "Winsock.dll version out of range.";
        case WSANOTINITIALISED:         return "Successful WSAStartup not yet performed.";
        case WSAEDISCON:                return "Graceful shutdown in progress.";
        case WSAENOMORE:                return "No more results.";
        case WSAECANCELLED:             return "Call has been cancelled.";
        case WSAEINVALIDPROCTABLE:      return "Procedure call table is invalid.";
        case WSAEINVALIDPROVIDER:       return "Service provider is invalid.";
        case WSAEPROVIDERFAILEDINIT:    return "Service provider failed to initialize.";
        case WSASYSCALLFAILURE:         return "System call failure.";
        case WSASERVICE_NOT_FOUND:      return "Service not found.";
        case WSATYPE_NOT_FOUND:         return "Class type not found.";
        case WSA_E_NO_MORE:             return "No more results (WSA_E_NO_MORE).";
        case WSA_E_CANCELLED:           return "Call was cancelled (WSA_E_CANCELLED).";
        case WSAEREFUSED:               return "Database query was refused.";
        case WSAHOST_NOT_FOUND:         return "Host not found.";
        case WSATRY_AGAIN:              return "Nonauthoritative host not found.";
        case WSANO_RECOVERY:            return "This is a nonrecoverable error.";
        case WSANO_DATA:                return "Valid name, no data record of requested type.";
        case WSA_QOS_RECEIVERS:         return "At least one QoS reserve has arrived.";
        case WSA_QOS_SENDERS:           return "At least one QoS send path has arrived.";
        case WSA_QOS_NO_SENDERS:        return "There are no QoS senders.";
        case WSA_QOS_NO_RECEIVERS:      return "There are no QoS receivers.";
        case WSA_QOS_REQUEST_CONFIRMED: return "QoS reserve request has been confirmed.";
        case WSA_QOS_ADMISSION_FAILURE: return "QoS admission error.";
        case WSA_QOS_POLICY_FAILURE:    return "QoS policy failure.";
        case WSA_QOS_BAD_STYLE:         return "QoS unknown or conflicting style.";
        case WSA_QOS_BAD_OBJECT:        return "QoS bad filterspec or provider specific object.";
        case WSA_QOS_TRAFFIC_CTRL_ERROR: return "QoS traffic control error.";
        case WSA_QOS_GENERIC_ERROR:     return "QoS generic error.";
        case WSA_QOS_ESERVICETYPE:      return "QoS service type error.";
        case WSA_QOS_EFLOWSPEC:         return "QoS flowspec error.";
        case WSA_QOS_EPROVSPECBUF:      return "Invalid QoS provider-specific buffer.";
        case WSA_QOS_EFILTERSTYLE:      return "Invalid QoS filter style.";
        case WSA_QOS_EFILTERTYPE:       return "Invalid QoS filter type.";
        case WSA_QOS_EFILTERCOUNT:      return "Incorrect QoS filter count.";
        case WSA_QOS_EOBJLENGTH:        return "Invalid QoS object length.";
        case WSA_QOS_EFLOWCOUNT:        return "Incorrect QoS flow count.";
        case WSA_QOS_EPOLICYOBJ:        return "Invalid QoS policy object.";
        case WSA_QOS_EFLOWDESC:         return "Invalid QoS flow descriptor.";
        case WSA_QOS_EPSFLOWSPEC:       return "Invalid QoS provider-specific flowspec.";
        case WSA_QOS_EPSFILTERSPEC:     return "Invalid QoS provider-specific filterspec.";
        case WSA_QOS_ESDMODEOBJ:        return "Invalid QoS shape discard mode object.";
        case WSA_QOS_RESERVED_PETYPE:   return "Reserved policy element type for QoS.";
        case WSA_IPSEC_NAME_POLICY_ERROR: return "IPsec name-based policy error.";
        default: break;
        }
        snprintf(tmp, sizeof(tmp), "Unknown Socket error: %d", nErrorId);
        return tmp;
#else
        const char* s = strerror(nErrorId);
        return s ? s : "Unknown Socket error";
#endif
    }
	
	inline void CheckSocketError(int result, const std::string &msg) 
	{
        if (result == SOCKET_ERROR) 
		{
            int errorId = GET_NET_ERROR();
            throw std::runtime_error("Error: " + msg + " " + SocketErrorToString(errorId));
        }
    }

}

#endif
