#pragma once

#include <string>

#include "connection.h"

/**
 * @brief A specialized class for connection to the Qubic network.
 */
class QubicConnection : public Connection
{
public:
    QubicConnection() {}
    QubicConnection(Socket&& socket) : Connection(std::move(socket)) {}

    // Move constructor/assignment: transfer peer info along with the socket.
    QubicConnection(QubicConnection&& other) noexcept
        : Connection(std::move(other))
        , m_peerIp(std::move(other.m_peerIp))
        , m_peerPort(other.m_peerPort)
    {
        other.m_peerPort = 0;
    }

    QubicConnection& operator=(QubicConnection&& other) noexcept
    {
        if (this != &other)
        {
            Connection::operator=(std::move(other));
            m_peerIp = std::move(other.m_peerIp);
            m_peerPort = other.m_peerPort;
            other.m_peerPort = 0;
        }
        return *this;
    }
    /**
     * @brief Open a connection to a Qubic node using the Qubic handshake (ExchangePublicPeers).
     * @param ip The node IP address.
     * @param port The node port to connect to.
     * @return True if connection was established, false otherwise.
     */
    bool openQubicConnection(const std::string& ip, int port);

    /**
     * @brief Attempt to reconnect using the stored IP and port from the last successful openQubicConnection call.
     * @return True if reconnection was successful, false otherwise.
     */
    bool reconnect();

    void setPeer(const std::string& ip, int port) { m_peerIp = ip; m_peerPort = port; }
    const std::string& getPeerIp() const { return m_peerIp; }
    int getPeerPort() const { return m_peerPort; }
    
    /**
     * @brief Receive data of type T that is preceeded by a header. Skip data that does not match T.
     * @tparam T The type of data to receive.
     * @return The received object of type T. On error, this will be all 0.
     */
    template <typename T> T receivePacketWithHeaderAs();

    /**
     * @brief Same as receivePacketWithHeaderAs() but with pre-allocated T. Use this for large T to prevent stack overflow.
     * @tparam T The type of data to receive.
     * @param result An output parameter to write the received object to. On error, this will be all 0.
     */
    template <typename T> void receivePacketWithHeaderAs(T& result);

    /**
     * @brief Similar to receivePacketWithHeaderAs but can be used to receive packets with a variable size extra data
              (as specified in the header as size). Note that the RequestResponseHeader is not written to the buffer.
     * @tparam T The type of data to receive.
     * @param buffer A pointer to the buffer to write the data to.
     * @return Returns the total number of bytes written to the buffer, or 0 if there is no connection, or -1 if an error occured.
     */
    template <typename T> int receivePacketAndExtraDataWithHeaderAs(char* buffer);

private:
    std::string m_peerIp;
    int m_peerPort = 0;
};

