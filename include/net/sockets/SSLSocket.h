#ifndef OTHER_NET_SSL_SOCKET_H
#define OTHER_NET_SSL_SOCKET_H


#include "Socket.h"


namespace other::net::sockets
{
	/**
     * @brief Зарезервированный класс.
     */
    class SSLSocket
    {
    private:
        Socket _socket;
        

    public:
        SSLSocket() noexcept;
        ~SSLSocket() noexcept;
		
		/**
         * @brief Конструктор перемещения: переносит владение дескриптором, метаданными и статистикой из `other`.
         * @param [in,out] other Исходный объект `SSLSocket`; после перемещения становится закрытым и обнулённым.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        SSLSocket(SSLSocket&& other) noexcept;
        /**
         * @brief Оператор перемещающего присваивания: закрывает текущий сокет (если открыт) и забирает ресурсы `other`.
         * @param [in,out] other Исходный объект `SSLSocket`; после перемещения становится закрытым и обнулённым.
         * @return Ссылка на текущий объект (`*this`).
         * @note Безопасен при самоприсваивании. Гарантированно не выбрасывает исключений (`noexcept`).
         */
        SSLSocket& operator=(SSLSocket&& other) noexcept;

        // Копирование запрещено: одним системным дескриптором может владеть только один объект SSLSocket.
        SSLSocket(const SSLSocket&) = delete;
        SSLSocket& operator=(const SSLSocket&) = delete;



        
    };
}

#endif // OTHER_NET_SSL_SOCKET_H
