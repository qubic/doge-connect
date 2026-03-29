#include "qubic_recv_thread.h"

#include <thread>
#include <stop_token>
#include <iostream>

#include "log.h"
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

using SteadyClock = std::chrono::steady_clock;

// Process a single, already-framed packet (packetSize bytes starting at recvData).
void processSolution(char* recvData, unsigned int packetSize, ConcurrentQueue<DispatcherMiningSolution>& queue)
{
    unsigned int requiredSize = sizeof(RequestResponseHeader) + sizeof(CustomQubicMiningSolution) + sizeof(QubicDogeMiningSolution) + SIGNATURE_SIZE;
    if (packetSize != requiredSize)
        return;

    RequestResponseHeader* header = reinterpret_cast<RequestResponseHeader*>(recvData);
    if (header->type() != CustomQubicMiningSolution::type())
        return;

    // Read CustomQubicMiningSolution struct and check that type is DOGE.
    uint64_t offset = sizeof(RequestResponseHeader);
    CustomQubicMiningSolution* qubicSol = reinterpret_cast<CustomQubicMiningSolution*>(recvData + offset);
    if (qubicSol->customMiningType != CustomMiningType::DOGE)
        return;

    // Verify signature: covers everything after the header, before the trailing 64-byte signature.
    const unsigned int messageSize = packetSize - sizeof(RequestResponseHeader);
    const uint8_t* payload = reinterpret_cast<const uint8_t*>(recvData + sizeof(RequestResponseHeader));
    unsigned char digest[32];
    KangarooTwelve(payload, messageSize - SIGNATURE_SIZE, digest, 32);
    if (!verify(qubicSol->sourcePublicKey.data(), digest, payload + (messageSize - SIGNATURE_SIZE)))
    {
        ERR() << "processSolution: Invalid signature, dropping solution." << std::endl;
        return;
    }

    // Parse QubicDogeMiningSolution struct and create DispatcherMiningSolution from it.
    offset += sizeof(CustomQubicMiningSolution);
    QubicDogeMiningSolution* dogeSol = reinterpret_cast<QubicDogeMiningSolution*>(recvData + offset);

    DispatcherMiningSolution dispSol =
    {
        .jobId = qubicSol->jobId,
        .nTime = dogeSol->nTime,
        .nonce = dogeSol->nonce,
        .merkleRoot = dogeSol->merkleRoot,
        .extraNonce2 = dogeSol->extraNonce2,
        .sourcePublicKey = qubicSol->sourcePublicKey,
    };

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

// Per-connection reconnect state for non-blocking exponential backoff.
struct ReconnectState
{
    unsigned int delaySec = reconnectBaseDelaySec;
    SteadyClock::time_point nextRetryTime = (SteadyClock::time_point::min)(); // min = no pending reconnect

    void scheduleRetry()
    {
        nextRetryTime = SteadyClock::now() + std::chrono::seconds(delaySec);
    }

    void backoff()
    {
        delaySec = (delaySec < reconnectBaseDelaySec) ? reconnectBaseDelaySec : (std::min)(delaySec * 2, reconnectMaxDelaySec);
    }

    void reset()
    {
        delaySec = reconnectBaseDelaySec;
        nextRetryTime = (SteadyClock::time_point::min)();
    }

    bool isPending() const { return nextRetryTime != (SteadyClock::time_point::min)(); }
    bool isReady() const { return isPending() && SteadyClock::now() >= nextRetryTime; }
};

// Mark a connection as disconnected and schedule a reconnect attempt.
static void markDisconnected(QubicConnection& conn, std::vector<char>& recvBuf, ReconnectState& rs, const char* reason)
{
    ERR() << "qubicReceiveLoop: Connection to " << conn.getPeerIp() << " lost (" << reason << ")." << std::endl;
    conn.closeConnection();
    recvBuf.clear();
    rs.scheduleRetry();
    LOG() << "qubicReceiveLoop: Will reconnect to " << conn.getPeerIp() << " in " << rs.delaySec << "s." << std::endl;
}

// Try ONE pending reconnect per call (to avoid blocking the recv loop for too long).
// Returns true if a connection was (re)established.
static bool tryOneReconnect(std::vector<QubicConnection>& connections, std::vector<ReconnectState>& reconnectStates, size_t& nextReconnIdx)
{
    // Scan from where we left off to find the next ready connection.
    for (size_t scanned = 0; scanned < connections.size(); ++scanned)
    {
        size_t i = nextReconnIdx % connections.size();
        nextReconnIdx = (nextReconnIdx + 1) % connections.size();

        ReconnectState& rs = reconnectStates[i];
        if (!rs.isReady())
            continue;

        if (connections[i].reconnect())
        {
            LOG() << "qubicReceiveLoop: Reconnected to " << connections[i].getPeerIp() << ":" << connections[i].getPeerPort() << "." << std::endl;
            rs.reset();
            return true;
        }
        else
        {
            rs.backoff();
            rs.scheduleRetry();
            LOG() << "qubicReceiveLoop: Connect to " << connections[i].getPeerIp() << ":" << connections[i].getPeerPort()
                << " failed, retry in " << rs.delaySec << "s." << std::endl;
            return false; // Attempted one, don't block further.
        }
    }
    return false;
}

void qubicReceiveLoop(std::stop_token st, ConcurrentQueue<DispatcherMiningSolution>& queue, std::vector<QubicConnection>& connections)
{
    std::array<char, 4096> readBuf;

    // Per-connection stream buffer for reassembling packets from the TCP byte stream.
    std::vector<std::vector<char>> recvBuffers(connections.size());
    std::vector<ReconnectState> reconnectStates(connections.size());

    // Schedule immediate connect for any connections that aren't already connected.
    for (size_t i = 0; i < connections.size(); ++i)
    {
        if (!connections[i].isConnected())
        {
            reconnectStates[i].delaySec = 0;
            reconnectStates[i].scheduleRetry();
        }
    }

    size_t nextReconnIdx = 0;

    std::vector<pollfd_t> socketPollList;
    std::vector<size_t> pollToConnIdx; // maps poll list index -> connections index
    buildPollList(connections, socketPollList, pollToConnIdx);

    while (!st.stop_requested())
    {
        // Try one pending reconnect per cycle to avoid blocking the recv loop.
        if (tryOneReconnect(connections, reconnectStates, nextReconnIdx))
            buildPollList(connections, socketPollList, pollToConnIdx);

        if (socketPollList.empty())
        {
            // All connections are down. Sleep briefly to avoid busy-waiting.
            std::this_thread::sleep_for(std::chrono::milliseconds(pollTimeoutMilliSec));
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
                        markDisconnected(connections[connIdx], recvBuffers[connIdx], reconnectStates[connIdx],
                            (std::string("recv returned ") + std::to_string(recvBytes)).c_str());
                        needRebuild = true;
                    }
                }
                else if (pd.revents & (POLLERR | POLLHUP))
                {
                    markDisconnected(connections[connIdx], recvBuffers[connIdx], reconnectStates[connIdx], "POLLERR|POLLHUP");
                    needRebuild = true;
                }
            }

            if (needRebuild)
                buildPollList(connections, socketPollList, pollToConnIdx);
        }
        else if (numSockWithData < 0)
        {
            ERR() << "qubicReceiveLoop: Poll error (" << GET_SOCKET_ERR << "), rebuilding poll list." << std::endl;
            buildPollList(connections, socketPollList, pollToConnIdx);
        }
    }
}