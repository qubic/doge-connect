#include "mining_thread.h"

#include <thread>
#include <stop_token>
#include <iostream>
#include <limits>
#include <cstdint>
#include <cstring>

#include "hash_util/hash_util.h"
#include "hash_util/scrypt.h"

#include "concurrency/concurrent_queue.h"
#include "connection/qubic_connection.h"
#include "structs.h"

#include "mining_structs.h"

constexpr unsigned int computorId = 337;


void miningLoop(
    std::stop_token st,
    ConcurrentQueue<InternalMiningTask>& activeTasks,
    QubicConnection& connection,
    std::atomic<bool>& startNewJob,
    std::atomic<uint64_t>& solutionsFound
)
{
    std::array<uint8_t, 80> header;
    std::array<uint8_t, 32> merkleRoot;
    std::array<uint8_t, 32> scryptHash;
    constexpr unsigned int minBufferSize = sizeof(RequestResponseHeader) + sizeof(CustomQubicMiningSolution) + sizeof(QubicDogeMiningSolution);
    std::vector<uint8_t> solutionBuffer(minBufferSize + 8);

    while (!st.stop_requested())
    {
    start_new_job:
        InternalMiningTask task = activeTasks.pop(); // blocks until data is available
        startNewJob = false;

        if (task.extraNonce2NumBytes > 8)
        {
            std::cerr << "miningLoop can currently only handle up to 8 bytes for extraNonce2." << std::endl;
            continue;
        }

        // Full header can be constructed via concatenating partialHeader1 + merkleRoot + partialHeader2 + nonce.
        memcpy(header.data(), task.partialHeader1.data(), task.partialHeader1.size());
        memcpy(header.data() + task.partialHeader1.size() + merkleRoot.size(), task.partialHeader2.data(), task.partialHeader2.size());

        unsigned int totalNumBytes = minBufferSize + task.extraNonce2NumBytes;
        if (solutionBuffer.size() < totalNumBytes)
            solutionBuffer.resize(totalNumBytes);

        // First 10 bits in extraNonce2 need to be set to indicate computor id.
        unsigned int extraNonce2EffectiveNumBits = task.extraNonce2NumBytes * 8 - 10;
        uint64_t extraNonce2CompMask = computorId;
        extraNonce2CompMask <<= extraNonce2EffectiveNumBits;
        uint64_t extraNonce2Max = (1ULL << extraNonce2EffectiveNumBits) - 1; // extraNonce2EffectiveNumBits trailing 1s

        std::vector<uint8_t> extraNonce2Vec(task.extraNonce2NumBytes);
        for (uint64_t extraNonce2Counter = 0; extraNonce2Counter <= extraNonce2Max; ++extraNonce2Counter)
        {
            uint64_t extraNonce2 = extraNonce2CompMask | extraNonce2Counter;
            for (unsigned int b = 0; b < extraNonce2Vec.size(); ++b)
                extraNonce2Vec[extraNonce2Vec.size() - 1 - b] = (extraNonce2 >> (b * 8)) & 0xFF;

            merkleRoot = calculateMerkleRoot(task.coinbase1, task.coinbase2, task.extraNonce1, extraNonce2Vec, task.merkleBranches);

            memcpy(header.data() + task.partialHeader1.size(), merkleRoot.data(), merkleRoot.size());

            for (uint32_t nonce = 0; nonce <= (std::numeric_limits<uint32_t>::max)(); ++nonce)
            {
                if (startNewJob)
                    goto start_new_job;
                if (st.stop_requested())
                    goto stop_mining;

                // Write nonce into header in little-endian format.
                header[76] = nonce & 0xFF;
                header[77] = (nonce >> 8) & 0xFF;
                header[78] = (nonce >> 16) & 0xFF;
                header[79] = (nonce >> 24) & 0xFF;

                scrypt_1024_1_1_256(reinterpret_cast<char*>(header.data()), reinterpret_cast<char*>(scryptHash.data()));
                bool passedTarget = verifyHashVsTarget(scryptHash, task.targetDispatcher);
                if (passedTarget || nonce % 10000 == 0) // send some wrong shares as well for testing
                {
                    if (passedTarget)
                    {
                        ++solutionsFound;
                        std::cout << "miningLoop: Found a share that passes dispatcher difficulty." << std::endl;
                    }
                    else
                        std::cout << "miningLoop: Sending a wrong share for testing." << std::endl;

                    RequestResponseHeader* packetHeader = reinterpret_cast<RequestResponseHeader*>(solutionBuffer.data());
                    packetHeader->zeroDejavu();
                    packetHeader->setSize(totalNumBytes);
                    packetHeader->setType(CustomQubicMiningSolution::type());

                    uint64_t offset = sizeof(RequestResponseHeader);
                    CustomQubicMiningSolution* qubicSol = reinterpret_cast<CustomQubicMiningSolution*>(solutionBuffer.data() + offset);
                    qubicSol->jobId = task.jobId;
                    qubicSol->customMiningType = CustomMiningType::DOGE;

                    offset += sizeof(CustomQubicMiningSolution);
                    QubicDogeMiningSolution* dogeSol = reinterpret_cast<QubicDogeMiningSolution*>(solutionBuffer.data() + offset);
                    memcpy(dogeSol->nonce.data(), header.data() + 76, 4);
                    memcpy(dogeSol->merkleRoot.data(), merkleRoot.data(), merkleRoot.size());
                    dogeSol->extraNonce2NumBytes = extraNonce2Vec.size();
                    offset += sizeof(QubicDogeMiningSolution);
                    memcpy(solutionBuffer.data() + offset, extraNonce2Vec.data(), extraNonce2Vec.size());

                    connection.sendMessage(reinterpret_cast<char*>(solutionBuffer.data()), totalNumBytes);
                }
            }
        }
    }

stop_mining:
    return;
}