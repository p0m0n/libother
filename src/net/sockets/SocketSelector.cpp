#include "SocketSelector.h"



namespace other::net::sockets
{
    // Добавляет сокет в набор для отслеживания.
    void SocketSelector::Add(const Socket& socket, bool watchRead, bool watchWrite)
    {
        if (!socket.IsCreated()) return;

        // Если сокет уже отслеживается, просто обновляем его флаги событий
        for (auto& pfd : _pollFds)
        {
            if (pfd.fd == socket.SockHandle())
            {
                if (watchRead) 
                    pfd.events |= POLLIN;
                else           
                    pfd.events &= ~POLLIN;


                if (watchWrite) 
                    pfd.events |= POLLOUT;
                else            
                    pfd.events &= ~POLLOUT;
                return;
            }
        }

        // Иначе добавляем новый элемент в списки
        pollfd pfd = { 0 };
        pfd.fd = socket.SockHandle();

        if (watchRead)  
            pfd.events |= POLLIN;

        if (watchWrite) 
            pfd.events |= POLLOUT;

        pfd.revents = 0;

        _pollFds.push_back(pfd);
        _sockets.push_back(&socket);
    }

    // Удаляет сокет из набора отслеживания.
    void SocketSelector::Remove(const Socket& socket)
    {
        if (!socket.IsCreated()) return;

        socket_t handle = socket.SockHandle();
        for (size_t i = 0; i < _pollFds.size(); ++i)
        {
            if (_pollFds[i].fd == handle)
            {
                _pollFds.erase(_pollFds.begin() + i);
                _sockets.erase(_sockets.begin() + i);
                break;
            }
        }
    }

    // Полностью очищает весь набор отслеживаемых сокетов.
    void SocketSelector::Clear()
    {
        _pollFds.clear();
        _sockets.clear();
    }

    // Блокирующий опрос состояния всех добавленных сокетов (Event Loop).
    int SocketSelector::Poll(int timeoutMs)
    {
        if (_pollFds.empty())
        {
            return 0;
        }

        // Сбрасываем флаги результатов предыдущего опроса
        for (auto& pfd : _pollFds)
        {
            pfd.revents = 0;
        }

        // Вызываем кросс-платформенную функцию опроса (poll в Linux / WSAPoll в Windows)
        int result = POLL_FUNC(_pollFds.data(), static_cast<ULONG>(_pollFds.size()), timeoutMs);
        if (result == SOCKET_ERROR || result < 0)
        {
            return -1;
        }

        return result;
    }

    // Проверяет, готов ли сокет к чтению (после выполнения метода Poll).
    bool SocketSelector::IsReadable(const Socket& socket) const
    {
        socket_t handle = socket.SockHandle();
        for (size_t i = 0; i < _pollFds.size(); ++i)
        {
            if (_pollFds[i].fd == handle)
            {
                return (_pollFds[i].revents & (POLLIN | POLLPRI | POLLHUP | POLLERR)) != 0;
            }
        }
        return false;
    }

    // Проверяет, готов ли сокет к записи (после выполнения метода Poll).
    bool SocketSelector::IsWritable(const Socket& socket) const
    {
        socket_t handle = socket.SockHandle();
        for (size_t i = 0; i < _pollFds.size(); ++i)
        {
            if (_pollFds[i].fd == handle)
            {
                return (_pollFds[i].revents & (POLLOUT | POLLERR)) != 0;
            }
        }
        return false;
    }

    // Проверяет, произошла ли критическая ошибка на сокете (после выполнения Poll).
    bool SocketSelector::HasError(const Socket& socket) const
    {
        socket_t handle = socket.SockHandle();
        for (size_t i = 0; i < _pollFds.size(); ++i)
        {
            if (_pollFds[i].fd == handle)
            {
                return (_pollFds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0;
            }
        }
        return false;
    }

    // Возвращает текущее количество сокетов, находящихся под наблюдением селектора.
    size_t SocketSelector::Size() const
    {
        return _pollFds.size();
    }
}
