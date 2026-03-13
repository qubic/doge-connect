#include "task_recv_thread.h"

#include <thread>
#include <stop_token>
#include <iostream>
#include <vector>
#include <numeric>
#include <cstdint>
#include <cstring>

#include "hash_util/difficulty.h"

#include "concurrency/concurrent_queue.h"
#include "connection/qubic_connection.h"
#include "structs.h"

#include "mining_structs.h"


void taskReceiveLoop(std::stop_token st, ConcurrentQueue<InternalMiningTask>& activeTasks, QubicConnection& connection, std::atomic<bool>& startNewJob)
{
    std::array<char, 4096> buffer;

    while (!st.stop_requested())
    {
        const int recvBytes = connection.receivePacketAndExtraDataWithHeaderAs<CustomQubicMiningTask>(buffer.data());
        if (recvBytes == 0)
            break; // stop receiving if no connection

        int recvBytesMin = sizeof(CustomQubicMiningTask) + sizeof(QubicDogeMiningTask) + SIGNATURE_SIZE;
        if (recvBytes < recvBytesMin)
        {
            std::cerr << "Did not receive all data for QubicDogeMiningTask." << std::endl;
            continue;
        }

        // TODO: verify dispatcher signature (always last SIGNATURE_SIZE bytes of the full packet).

        CustomQubicMiningTask* qubicTask = reinterpret_cast<CustomQubicMiningTask*>(buffer.data());
        if (qubicTask->customMiningType != CustomMiningType::DOGE)
            continue;

        QubicDogeMiningTask* dogeTask = reinterpret_cast<QubicDogeMiningTask*>(buffer.data() + sizeof(CustomQubicMiningTask));

        InternalMiningTask task;
        task.jobId = qubicTask->jobId;
        task.targetDispatcher = calculateFullRepFromCompactRep(dogeTask->dispatcherDifficulty);

        task.extraNonce2NumBytes = dogeTask->extraNonce2NumBytes;
        memcpy(task.partialHeader1.data(), dogeTask->version.data(), 4);
        memcpy(task.partialHeader1.data() + 4, dogeTask->prevHash.data(), 32);
        memcpy(task.partialHeader2.data(), dogeTask->nTime.data(), 4);
        memcpy(task.partialHeader2.data() + 4, dogeTask->nBits.data(), 4);

        // The QubicDogeMiningTask struct is followed by the payload in the order:
        // - extraNonce1
        // - coinbase1
        // - coinbase2
        // - merkleBranch1NumBytes (unsigned int), ... , merkleBranchNNumBytes (unsigned int)
        // - merkleBranch1, ... , merkleBranchN

        // Verify recvBytes is large enough before reading the payload.
        recvBytesMin += dogeTask->extraNonce1NumBytes + dogeTask->coinbase1NumBytes
            + dogeTask->coinbase2NumBytes + dogeTask->numMerkleBranches * sizeof(unsigned int);
        if (recvBytes < recvBytesMin)
        {
            std::cerr << "Did not receive all data for QubicDogeMiningTask." << std::endl;
            continue;
        }

        uint8_t* payload = reinterpret_cast<uint8_t*>(buffer.data() + sizeof(CustomQubicMiningTask) + sizeof(QubicDogeMiningTask));

        task.extraNonce1 = std::vector<uint8_t>(payload, payload + dogeTask->extraNonce1NumBytes);
        payload += dogeTask->extraNonce1NumBytes;
        task.coinbase1 = std::vector<uint8_t>(payload, payload + dogeTask->coinbase1NumBytes);
        payload += dogeTask->coinbase1NumBytes;
        task.coinbase2 = std::vector<uint8_t>(payload, payload + dogeTask->coinbase2NumBytes);
        payload += dogeTask->coinbase2NumBytes;

        const unsigned int* branchesNumBytes = reinterpret_cast<unsigned int*>(payload);

        // Verify recvBytes is large enough before reading the merkle branches data.
        recvBytesMin += std::accumulate(branchesNumBytes, branchesNumBytes + dogeTask->numMerkleBranches, 0);
        if (recvBytes < recvBytesMin)
        {
            std::cerr << "Did not receive all data for QubicDogeMiningTask." << std::endl;
            continue;
        }

        payload += dogeTask->numMerkleBranches * sizeof(unsigned int); // payload now points behind the branch sizes to the actual branch data

        for (unsigned int b = 0; b < dogeTask->numMerkleBranches; ++b)
        {
            task.merkleBranches.push_back(std::vector<uint8_t>(payload, payload + branchesNumBytes[b]));
            payload += branchesNumBytes[b];
        }

        int numBytesRead = payload - reinterpret_cast<uint8_t*>(buffer.data()) + SIGNATURE_SIZE;
        if (numBytesRead != recvBytes)
        {
            std::cout << "Received more bytes than expected for QubicDogeMiningTask including payload. Truncating unnecessary data." << std::endl;
        }

        if (dogeTask->cleanJobQueue)
        {
            activeTasks.clear();
            std::cout << "Mining task queue cleared." << std::endl;
        }

        std::cout << "New mining task added to the queue (jobId " << task.jobId << ")." << std::endl;
        activeTasks.push(std::move(task));

        if (dogeTask->cleanJobQueue)
            startNewJob = true; // only signal once the task in the queue is ready
    }
}