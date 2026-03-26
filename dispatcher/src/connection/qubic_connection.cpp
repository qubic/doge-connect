#include "qubic_connection.h"

#include <iostream>
#include <cstdint>
#include <cstring>

#include "log.h"

#ifdef _MSC_VER
#pragma comment(lib, "Ws2_32.lib")
#include <Winsock2.h>
#include <Ws2tcpip.h>
#define read(x, y, z) recv(x, y, z, 0) // make sure this goes after <iostream>
#define poll WSAPoll
#define INVALID_SKT INVALID_SOCKET
#define GET_SOCKET_ERR WSAGetLastError()
#define CONNECT_IN_PROGRESS (WSAGetLastError() == WSAEWOULDBLOCK)
typedef WSAPOLLFD pollfd_t;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>
#include <fcntl.h>
#define closesocket close
#define INVALID_SKT -1
#define GET_SOCKET_ERR errno
#define CONNECT_IN_PROGRESS (errno == EINPROGRESS)
typedef struct pollfd pollfd_t;
#endif

constexpr int connectTimeoutMs = 2000;

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

// Receive the next qubic packet with a RequestResponseHeader that matches T.
// Used during handshake where timeouts are expected, so errors are silent.
template <typename T>
void QubicConnection::receivePacketWithHeaderAs(T& result)
{
    memset(&result, 0, sizeof(T));

    // First receive the header. Skip at most a few non-matching packets to bound handshake time.
    constexpr int maxSkip = 3;
    int skipped = 0;
    RequestResponseHeader header;
    int recvByte = -1, packetSize = -1, remainingSize = -1;
    while (true)
    {
        recvByte = receiveResponse(reinterpret_cast<char*>(&header), sizeof(RequestResponseHeader));
        if (recvByte != sizeof(RequestResponseHeader))
            return;
        if (header.type() == END_RESPONSE_TYPE)
            return;
        if (header.type() != T::type())
        {
            if (++skipped > maxSkip)
                return; // Too many non-matching packets, give up.
            // Skip this packet by draining its bytes in chunks.
            // Some packets (e.g. computor list) are larger than m_buffer, so we must not
            // read the entire packet at once to avoid overflowing m_buffer.
            packetSize = header.size();
            remainingSize = packetSize - sizeof(RequestResponseHeader);
            while (remainingSize > 0)
            {
                int chunkSize = (remainingSize < static_cast<int>(m_buffer.size())) ? remainingSize : static_cast<int>(m_buffer.size());
                if (!receiveAllData(m_buffer.data(), chunkSize))
                    return;
                remainingSize -= chunkSize;
            }
            continue;
        }
        break;
    }

    packetSize = header.size();
    remainingSize = packetSize - sizeof(RequestResponseHeader);

    if (remainingSize)
    {
        if (remainingSize > static_cast<int>(m_buffer.size()))
            return; // Packet too large for expected type T.
        memset(m_buffer.data(), 0, sizeof(T));
        if (!receiveAllData(m_buffer.data(), remainingSize))
            return;
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
            ERR() << "QubicConnection::receivePacketWithHeaderAs: No connection." << std::endl;
            return 0;
        }
        if (recvByte != sizeof(RequestResponseHeader))
        {
            ERR() << "QubicConnection::receivePacketWithHeaderAs: No header received." << std::endl;
            return -1;
        }
        if (header.type() == END_RESPONSE_TYPE)
        {
            ERR() << "QubicConnection::receivePacketWithHeaderAs: Unexpected EndResponse received." << std::endl;
            return -1;
        }
        if (header.type() != T::type())
        {
            // Skip this packet and keep receiving.
            packetSize = header.size();
            remainingSize = packetSize - sizeof(RequestResponseHeader);
            if (!receiveAllData(m_buffer.data(), remainingSize))
            {
                ERR() << "QubicConnection::receivePacketWithHeaderAs: Failed to receive all data." << std::endl;
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
            ERR() << "QubicConnection::receivePacketWithHeaderAs: Failed to receive all data." << std::endl;
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
        ERR() << "Error translating IP address to usable one." << std::endl;
        m_socket.reset();
        return false;
    }

    // Set socket to non-blocking for connect with timeout.
#ifdef _MSC_VER
    unsigned long nonBlocking = 1;
    ioctlsocket(m_socket.rawSocket, FIONBIO, &nonBlocking);
#else
    int flags = fcntl(m_socket.rawSocket, F_GETFL, 0);
    fcntl(m_socket.rawSocket, F_SETFL, flags | O_NONBLOCK);
#endif

    int connectResult = connect(m_socket.rawSocket, (const sockaddr*)&addr, sizeof(addr));

    if (connectResult < 0 && !CONNECT_IN_PROGRESS)
    {
        m_socket.reset();
        return false;
    }

    if (connectResult != 0)
    {
        // Wait for connect to complete with timeout.
        pollfd_t pfd;
        pfd.fd = m_socket.rawSocket;
        pfd.events = POLLOUT;
        pfd.revents = 0;
        int pollResult = poll(&pfd, 1, connectTimeoutMs);

        if (pollResult <= 0 || !(pfd.revents & POLLOUT))
        {
            m_socket.reset();
            return false;
        }

        // Check for connect error via SO_ERROR.
        int sockErr = 0;
        socklen_t errLen = sizeof(sockErr);
        getsockopt(m_socket.rawSocket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&sockErr), &errLen);
        if (sockErr != 0)
        {
            m_socket.reset();
            return false;
        }
    }

    // Set socket back to blocking.
#ifdef _MSC_VER
    nonBlocking = 0;
    ioctlsocket(m_socket.rawSocket, FIONBIO, &nonBlocking);
#else
    fcntl(m_socket.rawSocket, F_SETFL, flags);
#endif

    m_socket.isConnected = true;

    // Set a timeout for the handshake so we don't block indefinitely if the node doesn't respond.
    setTimeout(SO_RCVTIMEO, /*milliseconds=*/1000);

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