#include "qubic_connection.h"

#include <iostream>
#include <cstdint>
#include <cstring>

#ifdef _MSC_VER
#pragma comment(lib, "Ws2_32.lib")
#include <Winsock2.h>
#include <Ws2tcpip.h>
#define read(x, y, z) recv(x, y, z, 0) // make sure this goes after <iostream>
#define INVALID_SKT INVALID_SOCKET
#define GET_SOCKET_ERR WSAGetLastError()
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#define closesocket close
#define INVALID_SKT -1
#define GET_SOCKET_ERR errno
#endif

#include "structs.h"

constexpr uint8_t END_RESPONSE_TYPE = 35;

struct RequestComputors
{
    static constexpr unsigned char type()
    {
        return 11;
    }
};


// Receive the next qubic packet with a RequestResponseHeader that matches T
template <typename T>
T QubicConnection::receivePacketWithHeaderAs()
{
    T result;
    receivePacketWithHeaderAs(result);
    return result;
}

// Receive the next qubic packet with a RequestResponseHeader that matches T
template <typename T>
void QubicConnection::receivePacketWithHeaderAs(T& result)
{
    memset(&result, 0, sizeof(T));

    // First receive the header.
    RequestResponseHeader header;
    int recvByte = -1, packetSize = -1, remainingSize = -1;
    while (true)
    {
        recvByte = receiveResponse(reinterpret_cast<char*>(&header), sizeof(RequestResponseHeader));
        if (recvByte != sizeof(RequestResponseHeader))
        {
            std::cerr << "QubicConnection::receivePacketWithHeaderAs: No header received." << std::endl;
            return;
        }
        if (header.type() == END_RESPONSE_TYPE)
        {
            std::cerr << "QubicConnection::receivePacketWithHeaderAs: Unexpected EndResponse received." << std::endl;
            return;
        }
        if (header.type() != T::type())
        {
            // Skip this packet and keep receiving.
            packetSize = header.size();
            remainingSize = packetSize - sizeof(RequestResponseHeader);
            if (!receiveAllData(m_buffer.data(), remainingSize))
            {
                std::cerr << "QubicConnection::receivePacketWithHeaderAs: Failed to receive all data." << std::endl;
                return;
            }
            else
                continue;
        }
        break;
    }

    packetSize = header.size();
    remainingSize = packetSize - sizeof(RequestResponseHeader);

    if (remainingSize)
    {
        memset(m_buffer.data(), 0, sizeof(T));
        if (!receiveAllData(m_buffer.data(), remainingSize))
        {
            std::cerr << "QubicConnection::receivePacketWithHeaderAs: Failed to receive all data." << std::endl;
            return;
        }
        result = *(reinterpret_cast<T*>(m_buffer.data()));
    }

    return;
}

template <typename T> int QubicConnection::receivePacketAndExtraDataWithHeaderAs(char* buffer)
{
    // First receive the header.
    RequestResponseHeader header;
    int recvByte = -1, packetSize = -1, remainingSize = -1;
    while (true)
    {
        recvByte = receiveResponse(reinterpret_cast<char*>(&header), sizeof(RequestResponseHeader));
        if (recvByte == 0 || recvByte == -1)
        {
            std::cerr << "QubicConnection::receivePacketWithHeaderAs: No connection." << std::endl;
            return 0;
        }
        if (recvByte != sizeof(RequestResponseHeader))
        {
            std::cerr << "QubicConnection::receivePacketWithHeaderAs: No header received." << std::endl;
            return -1;
        }
        if (header.type() == END_RESPONSE_TYPE)
        {
            std::cerr << "QubicConnection::receivePacketWithHeaderAs: Unexpected EndResponse received." << std::endl;
            return -1;
        }
        if (header.type() != T::type())
        {
            // Skip this packet and keep receiving.
            packetSize = header.size();
            remainingSize = packetSize - sizeof(RequestResponseHeader);
            if (!receiveAllData(m_buffer.data(), remainingSize))
            {
                std::cerr << "QubicConnection::receivePacketWithHeaderAs: Failed to receive all data." << std::endl;
                return -1;
            }
            else
                continue;
        }
        break;
    }

    packetSize = header.size();
    remainingSize = packetSize - sizeof(RequestResponseHeader);

    if (remainingSize)
    {
        if (!receiveAllData(buffer, remainingSize))
        {
            std::cerr << "QubicConnection::receivePacketWithHeaderAs: Failed to receive all data." << std::endl;
            return -1;
        }
    }

    return remainingSize;
}


bool QubicConnection::reconnect()
{
    if (m_peerIp.empty() || m_peerPort == 0)
        return false;
    return openQubicConnection(m_peerIp, m_peerPort);
}

bool QubicConnection::openQubicConnection(const std::string& ip, int port)
{
    closeConnection();

    m_peerIp = ip;
    m_peerPort = port;

    m_socket.rawSocket = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr;
    memset((char*)&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) <= 0)
    {
        std::cerr << "Error translating IP address to usable one." << std::endl;
        m_socket.reset();
        return false;
    }

#ifdef _MSC_VER
    if (connect(m_socket.rawSocket, (const sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
#else
    if (connect(m_socket.rawSocket, (const sockaddr*)&addr, sizeof(addr)) < 0)
#endif
    {
        std::cerr << "Connection failed for IP " << ip << " on port " << port << ": " << GET_SOCKET_ERR << std::endl;
        m_socket.reset();
        return false;
    }

    m_socket.isConnected = true;

    // Set a timeout for the handshake so we don't block indefinitely if the node doesn't respond.
    setTimeout(SO_RCVTIMEO, /*milliseconds=*/2000);

    // Receive handshake - exchange peer packets
    receivePacketWithHeaderAs<ExchangePublicPeers>();

    // Send our own ExchangePublicPeers so the node marks us as having completed the handshake.
    // Without this, the node's exchangedPublicPeers flag stays FALSE and it won't relay packets to us.
    {
        struct { RequestResponseHeader header; ExchangePublicPeers payload; } packet;
        memset(&packet, 0, sizeof(packet));
        packet.header.setSize(sizeof(packet));
        packet.header.setType(ExchangePublicPeers::type());
        packet.header.zeroDejavu();
        sendMessage(reinterpret_cast<char*>(&packet), sizeof(packet));
    }

    // If Qubic node has no ComputorList or a self-generated ComputorList it will requestComputor upon TCP initialization.
    // Ignore this message if it is sent. Temporarily set low timeout to not block if RequestComputors is not sent.
    setTimeout(SO_RCVTIMEO, /*milliseconds=*/200);
    receivePacketWithHeaderAs<RequestComputors>();

    // Reset to no timeout.
    setTimeout(SO_RCVTIMEO, 0);
    setTimeout(SO_SNDTIMEO, 0);

    return true;
}


// ---------------------------------------------------------------------------------------------------
// Instantiate templated functions with the types used in the code.
// This avoids having the definition of the function in the header.

template int QubicConnection::receivePacketAndExtraDataWithHeaderAs<CustomQubicMiningTask>(char* buffer);