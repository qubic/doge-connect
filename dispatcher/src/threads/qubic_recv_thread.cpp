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

#include "concurrency/concurrent_queue.h"
#include "connection/qubic_connection.h"
#include "k12_and_key_utils.h"
#include "structs.h"

constexpr unsigned long pollTimeoutMilliSec = 200;

void processSolution(char* recvData, unsigned int recvBytes, ConcurrentQueue<DispatcherMiningSolution>& queue)
{
    unsigned int requiredMinSize = sizeof(RequestResponseHeader) + sizeof(CustomQubicMiningSolution) + sizeof(QubicDogeMiningSolution) + SIGNATURE_SIZE;
    if (recvBytes < requiredMinSize)
        return;

    RequestResponseHeader* header = reinterpret_cast<RequestResponseHeader*>(recvData);
    if (header->size() < requiredMinSize || header->type() != CustomQubicMiningSolution::type())
        return;

    // Read CustomQubicMiningSolution struct and check that type is DOGE.
    uint64_t offset = sizeof(RequestResponseHeader);
    CustomQubicMiningSolution* qubicSol = reinterpret_cast<CustomQubicMiningSolution*>(recvData + offset);
    if (qubicSol->customMiningType != CustomMiningType::DOGE)
        return;

    // Parse QubicDogeMiningSolution struct and create DispatcherMiningSolution from it.
    offset += sizeof(CustomQubicMiningSolution);
    QubicDogeMiningSolution* dogeSol = reinterpret_cast<QubicDogeMiningSolution*>(recvData + offset);
    if (requiredMinSize + dogeSol->extraNonce2NumBytes != recvBytes)
        return;

    // Verify signature: covers everything after the header, before the trailing 64-byte signature.
    const unsigned int messageSize = recvBytes - sizeof(RequestResponseHeader);
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


void qubicReceiveLoop(std::stop_token st, ConcurrentQueue<DispatcherMiningSolution>& queue, std::vector<QubicConnection>& connections)
{
    std::array<char, 4096> buffer;

    // Prepare the pollfd array.
    std::vector<pollfd_t> socketPollList;
    for (const auto& c : connections)
    {
        pollfd_t pollDescriptor;
        pollDescriptor.fd = c.getRawSocket();
        pollDescriptor.events = POLLIN; // Monitor for incoming data
        pollDescriptor.revents = 0;     // Output field reset
        socketPollList.push_back(std::move(pollDescriptor));
    }

    while (!st.stop_requested())
    {
        // Returns number of sockets with events, or 0 if timeout.
        int numSockWithData = poll(socketPollList.data(), static_cast<unsigned long>(socketPollList.size()), pollTimeoutMilliSec);

        if (numSockWithData > 0)
        {
            // Iterate to see which sockets are ready, i.e. they have data that can be read without blocking.
            for (const auto& pollDescriptor : socketPollList)
            {
                if (pollDescriptor.revents & POLLIN)
                {
                    int recvBytes = read(pollDescriptor.fd, buffer.data(), buffer.size());

                    if (recvBytes > 0)
                    {
                        processSolution(buffer.data(), static_cast<unsigned int>(recvBytes), queue);
                    }
                    // TODO: handle disconnection of a single Qubic connection
                }
                else if (pollDescriptor.revents & (POLLERR | POLLHUP))
                {
                    // TODO: handle error or hangup
                }
            }
        }
        else if (numSockWithData < 0)
        {
            std::cerr << "qubicReceiveLoop: Poll error occurred (" << GET_SOCKET_ERR << ")." << std::endl;
            break;
        }
    }
}