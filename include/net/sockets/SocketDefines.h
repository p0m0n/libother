#ifndef LIBOTHER_NET_SOCK_DEFINES_H
#define LIBOTHER_NET_SOCK_DEFINES_H


// Подключаем общий базовый заголовок библиотеки.
#include "Define.h"


#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
#   include <ws2tcpip.h>
#   include <iphlpapi.h>
#   include <mswsock.h>

// Автоматическая линковка системных сетевых библиотек только для MSVC (без предупреждений в MinGW/GCC).
#   ifdef _MSC_VER
#       pragma comment(lib, "ws2_32.lib")
#       pragma comment(lib, "iphlpapi.lib")
#   endif
#else
#   include <sys/socket.h>
#   include <sys/ioctl.h>
#   include <sys/epoll.h>
#   include <netinet/in.h>
#   include <netinet/tcp.h>
#   include <arpa/inet.h>
#   include <netdb.h>
#   include <unistd.h>
#   include <fcntl.h>
#   include <errno.h>
#   include <string.h>
#   include <sys/poll.h>
#endif


namespace other::net::sockets
{

#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
    typedef SOCKET socket_t;
    typedef int    socklen_t;

#   define GET_NET_ERROR()        ::WSAGetLastError()
#   define SET_NET_ERROR(err)     ::WSASetLastError(err)
#   define CLOSE_SOCKET(s)        ::closesocket(s)
#   define POLL_FUNC              ::WSAPoll
#else
    typedef int socket_t;
    // На Linux тип socklen_t уже объявлен в <sys/socket.h>.

#   define GET_NET_ERROR()        errno
#   define SET_NET_ERROR(err)     (errno = (err))
#   define INVALID_SOCKET         static_cast<other::net::sockets::socket_t>(-1)
#   define SOCKET_ERROR           (-1)
#   define CLOSE_SOCKET(s)        ::close(s)
#   define POLL_FUNC              ::poll
#endif

    /**
     * @class WSAInitializer
     * @brief RAII-инициализатор сетевой подсистемы Winsock2 на Windows.
     *
     * Автоматически вызывает `WSAStartup(MAKEWORD(2, 2), ...)` при старте программы
     * и `WSACleanup()` при завершении. На Linux конструктор и деструктор ничего не делают.
     * Копирование и перемещение запрещены, чтобы исключить повторный вызов `WSACleanup()`.
     */
    class WSAInitializer
    {
    public:
        /**
         * @brief Инициализирует Winsock версии 2.2 на Windows (на Linux — пустая операция).
         * @note Гарантированно не выбрасывает исключений (`noexcept`). При сбое `WSAStartup`
         *       или несовместимой версии Winsock.dll выводит диагностику в `stderr` и завершает процесс.
         */
        WSAInitializer() noexcept
        {
#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
            // Запрашиваем инициализацию библиотеки Winsock версии 2.2.
            WSADATA wsaData = {};
            int err = ::WSAStartup(MAKEWORD(2, 2), &wsaData);
            if (err != 0)
            {
                std::fprintf(stderr, "WSAStartup failed with error: %d\n", err);
                std::exit(EXIT_FAILURE);
            }

            // Проверяем, что системная библиотека действительно предоставила версию 2.2.
            if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2)
            {
                std::fprintf(stderr, "Could not find a usable version of Winsock.dll\n");
                ::WSACleanup();
                std::exit(EXIT_FAILURE);
            }
#endif
        }

        /**
         * @brief Освобождает ресурсы Winsock через `WSACleanup()` на Windows (на Linux — пустая операция).
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        ~WSAInitializer() noexcept
        {
#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
            ::WSACleanup();
#endif
        }

        // Запрещаем копирование и перемещение: каждому WSAStartup соответствует ровно один WSACleanup.
        WSAInitializer(const WSAInitializer&) = delete;
        WSAInitializer& operator=(const WSAInitializer&) = delete;
        WSAInitializer(WSAInitializer&&) = delete;
        WSAInitializer& operator=(WSAInitializer&&) = delete;
    };

////////////////////////////////////////////////////////////////////////
///  УНИФИЦИРОВАННЫЙ СПИСОК СИСТЕМНЫХ, СЕТЕВЫХ И ИНТЕРНЕТ-ОШИБОК     ///
////////////////////////////////////////////////////////////////////////
#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS

    // --- Общие ошибки системных вызовов, ОС и аргументов ---
#   define SOCK_EINTR               WSAEINTR                   // Прерванный системный вызов (операция прервана сигналом)
#   define SOCK_EBADF               WSAEBADF                   // Неверный/недействительный дескриптор файла или сокета
#   define SOCK_EACCES              WSAEACCES                  // Отказано в доступе (нет прав на выполнение действия)
#   define SOCK_EFAULT              WSAEFAULT                  // Неверный адрес (ошибка доступа к памяти, невалидный указатель)
#   define SOCK_EINVAL              WSAEINVAL                  // Неверный аргумент (передано недопустимое значение в функцию)
#   define SOCK_EMFILE              WSAEMFILE                  // Слишком много открытых файлов или сокетов для текущего процесса
#   define SOCK_EPERM               ERROR_ACCESS_DENIED        // Операция не разрешена (недостаточно системных привилегий)
#   define SOCK_ENOENT              ERROR_FILE_NOT_FOUND       // Файл, ресурс или путь не найден в системе
#   define SOCK_EIO                 ERROR_IO_DEVICE            // Системная ошибка ввода-вывода (I/O error)
#   define SOCK_ENOMEM              WSA_NOT_ENOUGH_MEMORY      // Недостаточно оперативной памяти для выполнения операции
#   define SOCK_EBUSY               ERROR_BUSY                 // Системный ресурс или устройство временно заняты
#   define SOCK_ENODEV              ERROR_DEV_NOT_EXIST        // Сетевое устройство или интерфейс не существует
#   define SOCK_ENFILE              ERROR_TOO_MANY_OPEN_FILES  // Превышен общесистемный лимит открытых файлов/дескрипторов
#   define SOCK_ENOSPC              ERROR_DISK_FULL            // Нет свободного места на диске / в буфере устройства
#   define SOCK_EPIPE               ERROR_BROKEN_PIPE          // Разорванный канал (запись в закрытый удалённой стороной поток/сокет)
#   define SOCK_ENOSYS              ERROR_CALL_NOT_IMPLEMENTED // Функция или системный вызов не реализованы в ОС
#   define SOCK_EOVERFLOW           ERROR_ARITHMETIC_OVERFLOW  // Переполнение значения (не помещается в целевой тип данных)

    // --- Ошибки неблокирующих и асинхронных операций ---
#   define SOCK_EWOULDBLOCK         WSAEWOULDBLOCK             // Ресурс временно недоступен (операция заблокировала бы неблокирующий сокет)
#   define SOCK_EINPROGRESS         WSAEINPROGRESS             // Операция в процессе выполнения (например, асинхронный connect ещё идёт)
#   define SOCK_EALREADY            WSAEALREADY                // Операция уже выполняется на данном сокете
#   define SOCK_IO_PENDING          WSA_IO_PENDING             // Асинхронная операция ввода-вывода принята и завершится позже
#   define SOCK_IO_INCOMPLETE       WSA_IO_INCOMPLETE          // Асинхронная операция ввода-вывода ещё не завершена
#   define SOCK_OP_ABORTED          WSA_OPERATION_ABORTED      // Перекрытая/асинхронная операция прервана из-за закрытия сокета

    // --- Ошибки типов, опций и протоколов сокетов ---
#   define SOCK_ENOTSOCK            WSAENOTSOCK                // Операция применена не к сокету (передан дескриптор обычного файла)
#   define SOCK_EDESTADDRREQ        WSAEDESTADDRREQ            // Требуется адрес назначения (не указан получатель для отправки)
#   define SOCK_EMSGSIZE            WSAEMSGSIZE                // Сообщение слишком длинное для отправки в текущий буфер/протокол
#   define SOCK_EPROTOTYPE          WSAEPROTOTYPE              // Неправильный тип протокола для данного сокета
#   define SOCK_ENOPROTOOPT         WSAENOPROTOOPT             // Неверная или неподдерживаемая опция протокола (getsockopt/setsockopt)
#   define SOCK_EPROTONOSUPPORT     WSAEPROTONOSUPPORT         // Указанный протокол не поддерживается системой
#   define SOCK_ESOCKTNOSUPPORT     WSAESOCKTNOSUPPORT         // Тип сокета не поддерживается в данном семействе протоколов
#   define SOCK_EOPNOTSUPP          WSAEOPNOTSUPP              // Операция не поддерживается для данного типа сокета
#   define SOCK_EPFNOSUPPORT        WSAEPFNOSUPPORT            // Семейство протоколов не поддерживается системой
#   define SOCK_EAFNOSUPPORT        WSAEAFNOSUPPORT            // Семейство адресов не поддерживается протоколом (например, IPv6 отключён)
#   define SOCK_EPROTO              ERROR_PROTOCOL_UNREACHABLE // Ошибка сетевого протокола / протокол недостижим
#   define SOCK_EBADMSG             ERROR_INVALID_DATA         // Повреждённое или некорректное сообщение/пакет

    // --- Ошибки привязки (bind), сетевых интерфейсов и интернета ---
#   define SOCK_EADDRINUSE          WSAEADDRINUSE              // Адрес уже используется (порт уже занят другим приложением)
#   define SOCK_EADDRNOTAVAIL       WSAEADDRNOTAVAIL           // Запрошенный адрес не может быть назначен (например, несуществующий локальный IP)
#   define SOCK_ENETDOWN            WSAENETDOWN                // Сеть повреждена / отключена (сетевой интерфейс не работает)
#   define SOCK_ENETUNREACH         WSAENETUNREACH             // Сеть недоступна (нет физического пути или маршрута до сети/интернета)
#   define SOCK_ENETRESET           WSAENETRESET               // Сеть сбросила соединение из-за сбоя оборудования или перезапуска
#   define SOCK_ECONNABORTED        WSAECONNABORTED            // Программный отказ соединения (разорвано локально по таймауту или сбою)
#   define SOCK_ECONNRESET          WSAECONNRESET              // Соединение разорвано удалённой стороной (удалённый хост отправил RST)
#   define SOCK_ENOBUFS             WSAENOBUFS                 // Недостаточно свободной буферной памяти в системе для отправки/приёма
#   define SOCK_EISCONN             WSAEISCONN                 // Сокет уже подключён (повторный connect на установленном соединении)
#   define SOCK_ENOTCONN            WSAENOTCONN                // Сокет не подключён (попытка обмена данными без соединения)
#   define SOCK_ESHUTDOWN           WSAESHUTDOWN               // Нельзя отправить данные: сокет уже закрыт для отправки (вызван shutdown)
#   define SOCK_ETOOMANYREFS        WSAETOOMANYREFS            // Слишком много ссылок на объект ядра
#   define SOCK_ETIMEDOUT           WSAETIMEDOUT               // Истекло время ожидания (таймаут соединения или сетевой операции)
#   define SOCK_ECONNREFUSED        WSAECONNREFUSED            // Соединение отклонено (на удалённом хосте никто не слушает этот порт)
#   define SOCK_ENONET              ERROR_NO_NETWORK           // Компьютер не подключён к сети / отсутствует доступ к интернету
#   define SOCK_ENOLINK             ERROR_NETNAME_DELETED      // Сетевой канал (link) или виртуальное соединение разорваны
#   define SOCK_ECOMM               ERROR_UNEXP_NET_ERR        // Неожиданная ошибка связи при передаче данных по сети
#   define SOCK_EMULTIHOP           ERROR_BAD_NETPATH          // Ошибка маршрутизации / многозвенного сетевого пути (multihop)
#   define SOCK_EPORTUNREACH        ERROR_PORT_UNREACHABLE     // Удалённый порт недостижим (ICMP Port Unreachable)
#   define SOCK_ENETBUSY            ERROR_NETWORK_BUSY         // Сеть перегружена и временно не может обработать запрос
#   define SOCK_EBADNETRESP         ERROR_BAD_NET_RESP         // Удалённый сервер вернул некорректный или невыполнимый сетевой ответ

    // --- Ошибки путей, хостов и окружения ---
#   define SOCK_ELOOP               WSAELOOP                   // Слишком многоуровневая вложенность символьных ссылок
#   define SOCK_ENAMETOOLONG        WSAENAMETOOLONG            // Имя файла, хоста или компонента пути слишком длинное
#   define SOCK_EHOSTDOWN           WSAEHOSTDOWN               // Удалённый хост выключен (лежит)
#   define SOCK_EHOSTUNREACH        WSAEHOSTUNREACH            // Нет маршрута к удалённому хосту
#   define SOCK_ENOTEMPTY           WSAENOTEMPTY               // Директория не пуста
#   define SOCK_EPROCLIM            WSAEPROCLIM                // Превышен лимит процессов в системе
#   define SOCK_EUSERS              WSAEUSERS                  // Превышен лимит пользователей
#   define SOCK_EDQUOT              WSAEDQUOT                  // Превышена дисковая квота
#   define SOCK_ESTALE              WSAESTALE                  // Устаревшая ссылка на файл или ресурс
#   define SOCK_EREMOTE             WSAEREMOTE                 // Запрошенный элемент является удалённым

    // --- Ошибки подсистемы Winsock и DNS-разрешения имён ---
#   define SOCK_SYSNOTREADY         WSASYSNOTREADY             // Подсистема сетевого окружения не готова (драйвер TCP/IP не загружен)
#   define SOCK_VERNOTSUPPORTED     WSAVERNOTSUPPORTED         // Запрошенная версия Winsock.dll не поддерживается данной библиотекой
#   define SOCK_NOTINITIALISED      WSANOTINITIALISED          // Успешный вызов WSAStartup ещё не был выполнен
#   define SOCK_EDISCON             WSAEDISCON                 // Выполняется плавное (graceful) завершение соединения
#   define SOCK_ENOMORE             WSAENOMORE                 // Больше нет результатов для запроса (службы имён)
#   define SOCK_ECANCELLED          WSAECANCELLED              // Асинхронный вызов был отменён
#   define SOCK_EINVALIDPROCTABLE   WSAEINVALIDPROCTABLE       // Таблица процедур службы поставщика недействительна
#   define SOCK_EINVALIDPROVIDER    WSAEINVALIDPROVIDER        // Запрошенный поставщик услуг недействителен или повреждён
#   define SOCK_EPROVIDERFAILEDINIT WSAEPROVIDERFAILEDINIT     // Поставщик услуг не смог инициализироваться
#   define SOCK_ESYSCALLFAILURE     WSASYSCALLFAILURE          // Критический сбой системного вызова в недрах ОС
#   define SOCK_SERVICENOTFOUND     WSASERVICE_NOT_FOUND       // Сетевая служба не найдена
#   define SOCK_TYPENOTFOUND        WSATYPE_NOT_FOUND          // Запрошенный тип класса не найден
#   define SOCK_HOSTNOTFOUND        WSAHOST_NOT_FOUND          // Хост не найден (ошибка DNS-разрешения)
#   define SOCK_TRYAGAIN            WSATRY_AGAIN               // Временная ошибка разрешения имён DNS (попробуйте повторить позже)
#   define SOCK_NORECOVERY          WSANO_RECOVERY             // Неисправимая ошибка при поиске имени в DNS
#   define SOCK_NODATA              WSANO_DATA                 // Имя найдено, но у него нет DNS-записи запрашиваемого типа (A/AAAA)

    // --- Ошибки разрешения интернет-адресов (getaddrinfo / getnameinfo) ---
#   define NET_EAI_AGAIN            EAI_AGAIN                  // Временный сбой DNS-сервера; повторите запрос позже
#   define NET_EAI_BADFLAGS         EAI_BADFLAGS               // Переданы недопустимые флаги ai_flags в getaddrinfo
#   define NET_EAI_FAIL             EAI_FAIL                   // Неисправимая ошибка DNS-сервера при разрешении имени
#   define NET_EAI_FAMILY           EAI_FAMILY                 // Указанное семейство адресов (ai_family) не поддерживается
#   define NET_EAI_MEMORY           EAI_MEMORY                 // Недостаточно памяти при разрешении доменного имени
#   define NET_EAI_NONAME           EAI_NONAME                 // Доменное имя или сетевая служба не найдены
#   define NET_EAI_NODATA           EAI_NODATA                 // Доменное имя существует, но сетевые адреса (A/AAAA) отсутствуют
#   define NET_EAI_SERVICE          EAI_SERVICE                // Запрошенная служба/порт не поддерживается для данного типа сокета
#   define NET_EAI_SOCKTYPE         EAI_SOCKTYPE               // Указанный тип сокета (ai_socktype) не поддерживается
#   define NET_EAI_SYSTEM           (-2001)                    // Системная ошибка ОС при разрешении имени (специфично для Linux)
#   define NET_EAI_OVERFLOW         (-2002)                    // Буфер аргумента слишком мал для результата (специфично для Linux)
#   define NET_EAI_ADDRFAMILY       (-2003)                    // Семейство адресов для данного хоста не поддерживается (специфично для Linux)

#else

    // --- Linux эквиваленты системных, сетевых и интернет-ошибок ---
#   define SOCK_EINTR               EINTR
#   define SOCK_EBADF               EBADF
#   define SOCK_EACCES              EACCES
#   define SOCK_EFAULT              EFAULT
#   define SOCK_EINVAL              EINVAL
#   define SOCK_EMFILE              EMFILE
#   define SOCK_EPERM               EPERM
#   define SOCK_ENOENT              ENOENT
#   define SOCK_EIO                 EIO
#   define SOCK_ENOMEM              ENOMEM
#   define SOCK_EBUSY               EBUSY
#   define SOCK_ENODEV              ENODEV
#   define SOCK_ENFILE              ENFILE
#   define SOCK_ENOSPC              ENOSPC
#   define SOCK_EPIPE               EPIPE
#   define SOCK_ENOSYS              ENOSYS
#   define SOCK_EOVERFLOW           EOVERFLOW

    // --- Ошибки неблокирующих и асинхронных операций ---
#   define SOCK_EWOULDBLOCK         EWOULDBLOCK
#   define SOCK_EINPROGRESS         EINPROGRESS
#   define SOCK_EALREADY            EALREADY
#   define SOCK_IO_PENDING          (-1020)
#   define SOCK_IO_INCOMPLETE       (-1021)
#   define SOCK_OP_ABORTED          (-1022)

    // --- Ошибки типов, опций и протоколов сокетов ---
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
#   define SOCK_EPROTO              EPROTO
#   define SOCK_EBADMSG             EBADMSG

    // --- Ошибки привязки (bind), сетевых интерфейсов и интернета ---
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
#   define SOCK_ETOOMANYREFS        ETOOMANYREFS
#   define SOCK_ETIMEDOUT           ETIMEDOUT
#   define SOCK_ECONNREFUSED        ECONNREFUSED
#   define SOCK_ENONET              ENONET
#   define SOCK_ENOLINK             ENOLINK
#   define SOCK_ECOMM               ECOMM
#   define SOCK_EMULTIHOP           EMULTIHOP
#   define SOCK_EPORTUNREACH        (-1023)
#   define SOCK_ENETBUSY            (-1024)
#   define SOCK_EBADNETRESP         (-1025)

    // --- Ошибки путей, хостов и окружения ---
#   define SOCK_ELOOP               ELOOP
#   define SOCK_ENAMETOOLONG        ENAMETOOLONG
#   define SOCK_EHOSTDOWN           EHOSTDOWN
#   define SOCK_EHOSTUNREACH        EHOSTUNREACH
#   define SOCK_ENOTEMPTY           ENOTEMPTY
#   define SOCK_EPROCLIM            (-1001)
#   define SOCK_EUSERS              EUSERS
#   define SOCK_EDQUOT              EDQUOT
#   define SOCK_ESTALE              ESTALE
#   define SOCK_EREMOTE             EREMOTE

    // --- Уникальные заглушки для специфичных кодов Winsock и коды DNS на Linux ---
#   define SOCK_SYSNOTREADY         (-1002)
#   define SOCK_VERNOTSUPPORTED     (-1003)
#   define SOCK_NOTINITIALISED      (-1004)
#   define SOCK_EDISCON             (-1005)
#   define SOCK_ENOMORE             ENODATA
#   define SOCK_ECANCELLED          ECANCELED
#   define SOCK_EINVALIDPROCTABLE   (-1006)
#   define SOCK_EINVALIDPROVIDER    (-1007)
#   define SOCK_EPROVIDERFAILEDINIT (-1008)
#   define SOCK_ESYSCALLFAILURE     (-1009)
#   define SOCK_SERVICENOTFOUND     (-1010)
#   define SOCK_TYPENOTFOUND        (-1011)
#   define SOCK_HOSTNOTFOUND        EAI_NONAME
#   define SOCK_TRYAGAIN            EAI_AGAIN
#   define SOCK_NORECOVERY          EAI_FAIL
#   define SOCK_NODATA              EAI_NODATA

    // --- Ошибки разрешения интернет-адресов (getaddrinfo / getnameinfo) ---
#   define NET_EAI_AGAIN            EAI_AGAIN
#   define NET_EAI_BADFLAGS         EAI_BADFLAGS
#   define NET_EAI_FAIL             EAI_FAIL
#   define NET_EAI_FAMILY           EAI_FAMILY
#   define NET_EAI_MEMORY           EAI_MEMORY
#   define NET_EAI_NONAME           EAI_NONAME
#   define NET_EAI_NODATA           EAI_NODATA
#   define NET_EAI_SERVICE          EAI_SERVICE
#   define NET_EAI_SOCKTYPE         EAI_SOCKTYPE
#   define NET_EAI_SYSTEM           EAI_SYSTEM
#   define NET_EAI_OVERFLOW         EAI_OVERFLOW
#   define NET_EAI_ADDRFAMILY       EAI_ADDRFAMILY

#endif

    /**
     * @brief Преобразует код системной, сетевой или DNS-ошибки (`GET_NET_ERROR()`, `SO_ERROR`, `getaddrinfo()`, `getnameinfo()`) в читаемую строку.
     *
     * Основные системные, сокетные и общие DNS-ошибки обрабатываются в `switch`, а дополнительные коды `NET_EAI_*` —
     * в ветке `default:` через `if`, чтобы избежать дублирующихся `case` на Windows (где `EAI_*` совпадают с `WSA*`).
     * Код `0` трактуется как отсутствие ошибки (`"No error (Success)."`).
     * @param [in] nErrorId Код ошибки (`0`, `WSAE*` / `ERROR_*` / `EAI_*` на Windows или `errno` / `EAI_*` на Linux).
     * @return Текстовое описание ошибки либо `"Unknown Socket error: <код>"` для неизвестных кодов.
     * @throws std::bad_alloc При нехватке памяти для формирования `std::string`.
     */
    inline std::string SocketErrorToString(int nErrorId)
    {
        switch (nErrorId)
        {
        case 0:                        return "No error (Success).";

        // --- Системные ошибки ОС и аргументов ---
        case SOCK_EINTR:               return "Interrupted function call.";
        case SOCK_EBADF:               return "File handle is not valid.";
        case SOCK_EACCES:              return "Permission denied.";
        case SOCK_EFAULT:              return "Bad address.";
        case SOCK_EINVAL:              return "Invalid argument.";
        case SOCK_EMFILE:              return "Too many open files.";
        case SOCK_EPERM:               return "Operation not permitted.";
        case SOCK_ENOENT:              return "No such file, directory, or resource.";
        case SOCK_EIO:                 return "System input/output error.";
        case SOCK_ENOMEM:              return "Not enough memory available.";
        case SOCK_EBUSY:               return "Device or resource busy.";
        case SOCK_ENODEV:              return "No such device or network interface.";
        case SOCK_ENFILE:              return "Too many open files in system.";
        case SOCK_ENOSPC:              return "No space left on device.";
        case SOCK_EPIPE:               return "Broken pipe (remote peer closed connection).";
        case SOCK_ENOSYS:              return "Function not implemented by operating system.";
        case SOCK_EOVERFLOW:           return "Value too large for defined data type.";

        // --- Ошибки неблокирующих и асинхронных операций ---
        case SOCK_EWOULDBLOCK:         return "Resource temporarily unavailable (Would block).";
        case SOCK_EINPROGRESS:         return "Operation now in progress.";
        case SOCK_EALREADY:            return "Operation already in progress.";
        case SOCK_IO_PENDING:          return "Overlapped I/O operation is in progress.";
        case SOCK_IO_INCOMPLETE:       return "Overlapped I/O event is not in a signaled state.";
        case SOCK_OP_ABORTED:          return "Overlapped I/O operation has been aborted.";

        // --- Ошибки типов, опций и протоколов сокетов ---
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
        case SOCK_EPROTO:              return "Protocol error or protocol unreachable.";
        case SOCK_EBADMSG:             return "Bad or corrupted message.";

        // --- Ошибки привязки, сетевых интерфейсов и интернета ---
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
        case SOCK_ETOOMANYREFS:        return "Too many references.";
        case SOCK_ETIMEDOUT:           return "Connection timed out.";
        case SOCK_ECONNREFUSED:        return "Connection refused.";
        case SOCK_ENONET:              return "Machine is not connected to the network.";
        case SOCK_ENOLINK:             return "Network link has been severed.";
        case SOCK_ECOMM:               return "Communication error on send.";
        case SOCK_EMULTIHOP:           return "Multihop network path error.";
        case SOCK_EPORTUNREACH:        return "Remote port is unreachable.";
        case SOCK_ENETBUSY:            return "Network is busy.";
        case SOCK_EBADNETRESP:         return "Bad network response from remote server.";

        // --- Ошибки путей, хостов и окружения ---
        case SOCK_ELOOP:               return "Cannot translate name (too many symbolic links).";
        case SOCK_ENAMETOOLONG:        return "Name component or name was too long.";
        case SOCK_EHOSTDOWN:           return "Host is down.";
        case SOCK_EHOSTUNREACH:        return "No route to host.";
        case SOCK_ENOTEMPTY:           return "Directory not empty.";
        case SOCK_EPROCLIM:            return "Too many processes.";
        case SOCK_EUSERS:              return "Too many users.";
        case SOCK_EDQUOT:              return "Disk quota exceeded.";
        case SOCK_ESTALE:              return "Stale file handle reference.";
        case SOCK_EREMOTE:             return "Item is remote.";

        // --- Ошибки подсистемы Winsock и DNS ---
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
        case SOCK_TRYAGAIN:            return "Nonauthoritative host not found (temporary DNS failure).";
        case SOCK_NORECOVERY:          return "This is a nonrecoverable DNS error.";
        case SOCK_NODATA:              return "Valid name, no data record of requested type.";

        default:
            // Дополнительные коды getaddrinfo/getnameinfo (не перекрытые ветками switch выше).
            if (nErrorId == NET_EAI_BADFLAGS)   return "Invalid flags passed to getaddrinfo.";
            if (nErrorId == NET_EAI_FAMILY)     return "Address family not supported.";
            if (nErrorId == NET_EAI_MEMORY)     return "Memory allocation failure during DNS resolution.";
            if (nErrorId == NET_EAI_SERVICE)    return "Service not supported for socket type.";
            if (nErrorId == NET_EAI_SOCKTYPE)   return "Socket type not supported.";
            if (nErrorId == NET_EAI_SYSTEM)     return "System error during DNS resolution (see GET_NET_ERROR).";
            if (nErrorId == NET_EAI_OVERFLOW)   return "Argument buffer overflow during address resolution.";
            if (nErrorId == NET_EAI_ADDRFAMILY) return "Address family for hostname not supported.";
            return "Unknown Socket error: " + std::to_string(nErrorId);
        }
    }

    /**
     * @brief Проверяет результат системного сетевого вызова и выводит диагностику в `std::cerr` при ошибке.
     *
     * После вывода сообщения восстанавливает исходный код ошибки через `SET_NET_ERROR()`,
     * чтобы операции потокового вывода не перезаписывали системный код ошибки.
     * @param [in] result Возвращённое значение системного вызова (сравнивается с `SOCKET_ERROR`).
     * @param [in] msg    Контекстное описание операции для диагностики.
     * @return `0`, если `result != SOCKET_ERROR`; `-1`, если произошла ошибка.
     * @throws std::bad_alloc При нехватке памяти внутри `SocketErrorToString()` в случае ошибки.
     */
    inline int CheckSocketError(int result, std::string_view msg)
    {
        if (result == SOCKET_ERROR)
        {
            // Сохраняем системный код ошибки до любых операций с потоком вывода.
            const int errorId = GET_NET_ERROR();
            std::cerr << "Error: " << msg << " | " << SocketErrorToString(errorId) << '\n';
            // Восстанавливаем исходный код ошибки для вызывающего кода.
            SET_NET_ERROR(errorId);
            return -1;
        }
        return 0;
    }

}

#endif // LIBOTHER_NET_SOCK_DEFINES_H
