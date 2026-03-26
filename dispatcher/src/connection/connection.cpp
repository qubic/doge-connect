#include "connection.h"

#include <iostream>
#include <string>
#include <memory>
#include <cstring>

#include "log.h"

#ifdef _MSC_VER
#pragma comment(lib, "Ws2_32.lib")
#include <Winsock2.h>
#include <Ws2tcpip.h>
#define read(x, y, z) recv(x, y, z, 0) // make sure this goes after <iostream>
#define INVALID_SKT INVALID_SOCKET
#define GET_SOCKET_ERR WSAGetLastError()
#define SEND_FLAGS 0
#else
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#define closesocket close
#define INVALID_SKT -1
#define GET_SOCKET_ERR errno
#define SOCKET_ERROR -1
#define SEND_FLAGS MSG_NOSIGNAL
#endif


std::unique_ptr<ConnectionContext> ConnectionContext::makeConnectionContext()
{
#ifdef _MSC_VER
    // Try to initialize winsock on Windows
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        ERR() << "WSAStartup failed." << std::endl;
        return nullptr;
    }
#endif
    return std::unique_ptr<ConnectionContext>(new ConnectionContext());
}

ConnectionContext::~ConnectionContext()
{
#ifdef _MSC_VER
    WSACleanup();
#endif
}

Connection::Socket::Socket()
{
    isConnected = false;
    rawSocket = INVALID_SKT;
}

Connection::Socket::~Socket()
{
    if (rawSocket != INVALID_SKT)
        closesocket(rawSocket);
}

void Connection::Socket::reset()
{
    if (rawSocket != INVALID_SKT)
    {
        closesocket(rawSocket);
        rawSocket = INVALID_SKT;
    }
    isConnected = false;
}



bool Connection::setTimeout(int optName, unsigned long milliseconds)
{
#ifdef _MSC_VER
    DWORD tv = milliseconds;
#else
    struct timeval tv;
    tv.tv_sec = milliseconds / 1000;
    tv.tv_usec = (milliseconds % 1000) * 1000;
#endif
    if (setsockopt(m_socket.rawSocket, SOL_SOCKET, optName, (const char*)&tv, sizeof tv) != 0)
    {
        ERR() << "setsockopt failed with error: " << GET_SOCKET_ERR << std::endl;
        return false;
    }
    return true;
}

bool Connection::sendMessage(const std::string& msg)
{
    if (!m_socket.isConnected)
    {
        ERR() << "Socket is not connected, failed to send" << std::endl;
        return false;
    }

    size_t totalSent = 0;
    while (totalSent < msg.size())
    {
        int sent = send(m_socket.rawSocket, msg.c_str() + totalSent, msg.size() - totalSent, SEND_FLAGS);
        if (sent == -1)
        {
            m_socket.isConnected = false;
            return false;
        }
        totalSent += sent;
    }

    return true;
}

bool Connection::sendMessage(char* buffer, unsigned int size)
{
    if (!m_socket.isConnected)
    {
        return false;
    }

    size_t totalSent = 0;
    while (totalSent < size)
    {
        int sent = send(m_socket.rawSocket, buffer + totalSent, size - totalSent, SEND_FLAGS);
        if (sent == -1)
        {
            m_socket.isConnected = false;
            return false;
        }
        totalSent += sent;
    }
    return true;
}
    
std::string Connection::receiveResponse()
{
    std::string res = "";

    if (!m_socket.isConnected)
        return res;

    int recvBytes = read(m_socket.rawSocket, m_buffer.data(), m_buffer.size());
    if (recvBytes > 0)
        res = std::string(m_buffer.data(), recvBytes);
    return res;
}

int Connection::receiveResponse(char* buffer, unsigned int size)
{
    if (!m_socket.isConnected)
        return -1;

    return read(m_socket.rawSocket, buffer, size);
}

bool Connection::receiveAllData(char* buffer, unsigned int size)
{
    if (!m_socket.isConnected)
        return false;

    int recvBytesTotal = 0;
    int recvBytes = 0;
    while (recvBytesTotal < size)
    {
        recvBytes = read(m_socket.rawSocket, buffer + recvBytesTotal, size - recvBytesTotal);
        if (recvBytes <= 0)
            return false;
        recvBytesTotal += recvBytes;
    }

    return true;
}

bool Connection::openConnection(const std::string& host, const std::string& port)
{
    closeConnection();

    // Resolve host name to address
    struct addrinfo hints, * result = nullptr;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &result) != 0)
    {
        ERR() << "DNS lookup failed." << std::endl;
        return false;
    }

    // Connect socket
    m_socket.rawSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (connect(m_socket.rawSocket, result->ai_addr, result->ai_addrlen) == SOCKET_ERROR)
    {
        ERR() << "Connection failed: " << GET_SOCKET_ERR << std::endl;
        freeaddrinfo(result);
        m_socket.reset();
        return false;
    }
    freeaddrinfo(result);

    m_socket.isConnected = true;
    return true;
}

void Connection::closeConnection()
{
    m_socket.reset();
}

