#include "qubic_recv_thread.h"

#include <thread>
#include <stop_token>
#include <iostream>
#include <vector>
#include <cstdint>

#ifdef _MSC_VER
#pragma comment(lib, "Ws2_32.lib")
#include <Winsock2.h>
#define read(x, y, z) recv(x, y, z, 0) // make sure this goes after <iostream>
#define poll WSAPoll
typedef WSAPOLLFD pollfd_t;
#define GET_SOCKET_ERR WSAGetLastError()
#else
#include <poll.h>
#include <unistd.h>
typedef struct pollfd pollfd_t;
#define GET_SOCKET_ERR errno
#endif

#include <chrono>

#include "concurrency/concurrent_queue.h"
#include "connection/qubic_connection.h"
#include "k12_and_key_utils.h"
#include "structs.h"

constexpr unsigned long pollTimeoutMilliSec = 200;
constexpr unsigned int reconnectBaseDelaySec = 2;
constexpr unsigned int reconnectMaxDelaySec = 60;

// Process a single, already-framed packet (packetSize bytes starting at recvData).
void processSolution(char* recvData, unsigned int packetSize, ConcurrentQueue<DispatcherMiningSolution>& queue)
{
    unsigned int requiredMinSize = sizeof(RequestResponseHeader) + sizeof(CustomQubicMiningSolution) + sizeof(QubicDogeMiningSolution) + SIGNATURE_SIZE;
    if (packetSize < requiredMinSize)
        return;

    RequestResponseHeader* header = reinterpret_cast<RequestResponseHeader*>(recvData);
    if (header->type() != CustomQubicMiningSolution::type())
        return;

    // Read CustomQubicMiningSolution struct and check that type is DOGE.
    uint64_t offset = sizeof(RequestResponseHeader);
    CustomQubicMiningSolution* qubicSol = reinterpret_cast<CustomQubicMiningSolution*>(recvData + offset);
    if (qubicSol->customMiningType != CustomMiningType::DOGE)
        return;

    // Parse QubicDogeMiningSolution struct and create DispatcherMiningSolution from it.
    offset += sizeof(CustomQubicMiningSolution);
    QubicDogeMiningSolution* dogeSol = reinterpret_cast<QubicDogeMiningSolution*>(recvData + offset);
    if (requiredMinSize + dogeSol->extraNonce2NumBytes != packetSize)
        return;

    // Verify signature: covers everything after the header, before the trailing 64-byte signature.
    const unsigned int messageSize = packetSize - sizeof(RequestResponseHeader);
    const uint8_t* payload = reinterpret_cast<const uint8_t*>(recvData + sizeof(RequestResponseHeader));
    unsigned char digest[32];
    KangarooTwelve(payload, messageSize - SIGNATURE_SIZE, digest, 32);
    if (!verify(qubicSol->sourcePublicKey.data(), digest, payload + (messageSize - SIGNATURE_SIZE)))
    {
        std::cerr << "processSolution: Invalid signature, dropping solution." << std::endl;
        return;
    }

    DispatcherMiningSolution dispSol =
    {
        .jobId = qubicSol->jobId,
        .nonce = dogeSol->nonce,
        .merkleRoot = dogeSol->merkleRoot
    };
    offset += sizeof(QubicDogeMiningSolution);
    dispSol.extraNonce2 = std::vector<uint8_t>(recvData + offset, recvData + offset + dogeSol->extraNonce2NumBytes);

    queue.push(std::move(dispSol));
}

// Parse complete packets from a stream buffer using RequestResponseHeader framing.
// Calls processSolution for each complete packet, then removes consumed bytes from the buffer.
static void processRecvBuffer(std::vector<char>& buf, ConcurrentQueue<DispatcherMiningSolution>& queue)
{
    unsigned int pos = 0;
    while (pos + sizeof(RequestResponseHeader) <= buf.size())
    {
        RequestResponseHeader* header = reinterpret_cast<RequestResponseHeader*>(buf.data() + pos);
        unsigned int packetSize = header->size();

        // Sanity: broken header (size 0 or impossibly large).
        if (packetSize < sizeof(RequestResponseHeader) || packetSize > 10 * 1024 * 1024)
        {
            buf.clear();
            return;
        }

        // Not enough data yet for the full packet — wait for more.
        if (pos + packetSize > buf.size())
            break;

        processSolution(buf.data() + pos, packetSize, queue);
        pos += packetSize;
    }

    // Remove consumed bytes.
    if (pos > 0)
        buf.erase(buf.begin(), buf.begin() + pos);
}


// Rebuild the pollfd list from current connections. Only includes connected sockets.
static void buildPollList(std::vector<QubicConnection>& connections, std::vector<pollfd_t>& socketPollList, std::vector<size_t>& pollToConnIdx)
{
    socketPollList.clear();
    pollToConnIdx.clear();
    for (size_t i = 0; i < connections.size(); ++i)
    {
        if (connections[i].isConnected())
        {
            pollfd_t pd;
            pd.fd = connections[i].getRawSocket();
            pd.events = POLLIN;
            pd.revents = 0;
            socketPollList.push_back(pd);
            pollToConnIdx.push_back(i);
        }
    }
}

// Try to reconnect a single connection with exponential backoff.
// Returns true if reconnected, false if stop was requested before success.
static bool tryReconnect(std::stop_token& st, QubicConnection& conn)
{
    unsigned int delaySec = reconnectBaseDelaySec;
    while (!st.stop_requested())
    {
        std::cout << "qubicReceiveLoop: Reconnecting to " << conn.getPeerIp() << ":" << conn.getPeerPort()
            << " in " << delaySec << "s..." << std::endl;

        // Sleep in small increments so we can respond to stop requests.
        for (unsigned int elapsed = 0; elapsed < delaySec && !st.stop_requested(); ++elapsed)
            std::this_thread::sleep_for(std::chrono::seconds(1));

        if (st.stop_requested())
            return false;

        if (conn.reconnect())
        {
            std::cout << "qubicReceiveLoop: Reconnected to " << conn.getPeerIp() << ":" << conn.getPeerPort() << "." << std::endl;
            return true;
        }

        std::cerr << "qubicReceiveLoop: Reconnect failed for " << conn.getPeerIp() << ":" << conn.getPeerPort() << "." << std::endl;
        delaySec = (std::min)(delaySec * 2, reconnectMaxDelaySec);
    }
    return false;
}

void qubicReceiveLoop(std::stop_token st, ConcurrentQueue<DispatcherMiningSolution>& queue, std::vector<QubicConnection>& connections)
{
    std::array<char, 4096> readBuf;

    // Per-connection stream buffer for reassembling packets from the TCP byte stream.
    std::vector<std::vector<char>> recvBuffers(connections.size());

    std::vector<pollfd_t> socketPollList;
    std::vector<size_t> pollToConnIdx; // maps poll list index -> connections index
    buildPollList(connections, socketPollList, pollToConnIdx);

    while (!st.stop_requested())
    {
        if (socketPollList.empty())
        {
            // All connections are down. Try to reconnect each one.
            for (auto& conn : connections)
            {
                if (!conn.isConnected())
                    tryReconnect(st, conn);
                if (st.stop_requested())
                    return;
            }
            buildPollList(connections, socketPollList, pollToConnIdx);
            continue;
        }

        int numSockWithData = poll(socketPollList.data(), static_cast<unsigned long>(socketPollList.size()), pollTimeoutMilliSec);

        if (numSockWithData > 0)
        {
            bool needRebuild = false;

            for (size_t p = 0; p < socketPollList.size(); ++p)
            {
                const auto& pd = socketPollList[p];
                size_t connIdx = pollToConnIdx[p];

                if (pd.revents & POLLIN)
                {
                    int recvBytes = read(pd.fd, readBuf.data(), readBuf.size());

                    if (recvBytes > 0)
                    {
                        recvBuffers[connIdx].insert(recvBuffers[connIdx].end(), readBuf.data(), readBuf.data() + recvBytes);
                        processRecvBuffer(recvBuffers[connIdx], queue);
                    }
                    else
                    {
                        std::cerr << "qubicReceiveLoop: Connection to " << connections[connIdx].getPeerIp() << " lost (recv returned " << recvBytes << ")." << std::endl;
                        connections[connIdx].closeConnection();
                        recvBuffers[connIdx].clear();
                        tryReconnect(st, connections[connIdx]);
                        needRebuild = true;
                    }
                }
                else if (pd.revents & (POLLERR | POLLHUP))
                {
                    std::cerr << "qubicReceiveLoop: Connection to " << connections[connIdx].getPeerIp() << " lost (POLLERR|POLLHUP)." << std::endl;
                    connections[connIdx].closeConnection();
                    recvBuffers[connIdx].clear();
                    tryReconnect(st, connections[connIdx]);
                    needRebuild = true;
                }
            }

            if (needRebuild)
                buildPollList(connections, socketPollList, pollToConnIdx);
        }
        else if (numSockWithData < 0)
        {
            std::cerr << "qubicReceiveLoop: Poll error (" << GET_SOCKET_ERR << "), rebuilding poll list." << std::endl;
            buildPollList(connections, socketPollList, pollToConnIdx);
        }
    }
}