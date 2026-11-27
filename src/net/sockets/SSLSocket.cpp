#include "SSLSocket.h"


namespace other::net::sockets
{
    SSLSocket::SSLSocket() noexcept : _socket(Socket())
    {
    }
	
	SSLSocket::~SSLSocket() noexcept
	{
	}
    
    SSLSocket::SSLSocket(SSLSocket&& other) noexcept : _socket(std::move(other._socket))
    {
    }

    SSLSocket& SSLSocket::operator=(SSLSocket&& other) noexcept
    {
        if (this != &other)
        {
            _socket = std::move(other._socket);
        }
        return *this;
    }
}
