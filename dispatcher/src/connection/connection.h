#pragma once

#include <string>
#include <memory>
#include <array>

#ifdef _MSC_VER
#include <Winsock2.h>
typedef SOCKET SocketType;
#define INVALID_SKT INVALID_SOCKET
#else
typedef int SocketType;
#define INVALID_SKT -1
#endif

/**
 * @brief This is a dummy class to handle WSAStartup/WSACleanup on Windows automatically via object lifespan.
          On Linux, this does not do anything.
 */
class ConnectionContext
{
public:
    ~ConnectionContext();

    static std::unique_ptr<ConnectionContext> makeConnectionContext();

protected:
    ConnectionContext() {}
};

/**
 * @brief A class representing a network connection.
 */
class Connection
{
public:
    /**
     * @brief A wrapper struct for the raw socket.
     */
    struct Socket
    {
        bool isConnected; // true if rawSocket is valid and connected
        SocketType rawSocket;

        Socket();
        Socket(SocketType socket)
        {
            rawSocket = socket;
            isConnected = rawSocket != INVALID_SKT;
        }

        ~Socket();

        // Delete copy constructor and assignment to avoid
        // copies accidentally closing the socket.
        Socket(const Socket&) = delete;
        Socket& operator=(const Socket&) = delete;

        // Moving is allowed.
        Socket(Socket&& other) noexcept : rawSocket(other.rawSocket)
        {
            // "Steal" the resource and reset the original so its destructor is not closing the socket.
            other.rawSocket = INVALID_SKT;
            other.isConnected = false;
            isConnected = rawSocket != INVALID_SKT;
        }

        Socket& operator=(Socket&& other) noexcept
        {
            if (this != &other) {
                reset(); // Close existing socket in 'this'.
                rawSocket = other.rawSocket;
                other.rawSocket = INVALID_SKT;
                other.isConnected = false;
                isConnected = rawSocket != INVALID_SKT;
            }
            return *this;
        }

        void reset();
    };

    Connection() {}
    Connection(Socket&& socket) : m_socket(std::move(socket)) { }

    Connection(Connection&& other) noexcept = default;
    Connection& operator=(Connection&& other) noexcept = default;

    // No copy (Socket is non-copyable).
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    /**
     * @brief Send a message via the connection.
     * @param msg The message to send.
     * @return True if the whole message was sent, false otherwise.
     */
    bool sendMessage(const std::string& msg);

    /**
     * @brief Send data via the connection.
     * @param buffer A pointer to the data to send.
     * @param size The size of the data (number of bytes).
     * @return True if the whole message was sent, false otherwise.
     */
    bool sendMessage(char* buffer, unsigned int size);

    /**
     * @brief Receive a response via the connection.
     * @return The received message, may be empty if not connected.
     */
    std::string receiveResponse();

    /**
     * @brief Receive a response via the connection.
     * @param buffer A pointer to the buffer to write to.
     * @param size The maximum number of bytes to read.
     * @return Actual number of bytes read or -1 if not connected.
     */
    int receiveResponse(char* buffer, unsigned int size);

    /**
     * @brief Receive exactly the specified amount of data.
     * @param buffer A pointer to the buffer to write to.
     * @param size The number of bytes to receive.
     * @return True if exactly `size` bytes were received, false otherwise.
     */
    bool receiveAllData(char* buffer, unsigned int size);

    /**
     * @brief Open a connection.
     * @param host The host address.
     * @param port The host port to connect to.
     * @return True if the connection was established, false otherwise.
     */
    bool openConnection(const std::string& host, const std::string& port);

    /**
     * @brief Close the connection.
     */
    void closeConnection();

    /**
     * @brief Set the timeout for the specified operation. This adjusts the time that an operation
              (e.g. receive/send) may block for. The connection is not closed after the timeout.
     * @param optName The operation to set the timeout for, e.g. receive/send.
     * @param milliseconds The new timeout in milliseconds or 0 for no timeout.
     * @return True if the new timeout was set, false otherwise.
     */
    bool setTimeout(int optName, unsigned long milliseconds);

    SocketType getRawSocket() const { return m_socket.rawSocket; }

    bool isConnected() const { return m_socket.isConnected; }

protected:
    Socket m_socket;
    std::array<char, 4096> m_buffer;
};

