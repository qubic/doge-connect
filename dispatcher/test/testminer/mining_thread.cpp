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
#include "crypto/dispatcher_signing.h"
#include "structs.h"

#include "mining_structs.h"

constexpr uint32_t computorId = 337;


void miningLoop(
    std::stop_token st,
    ConcurrentQueue<InternalMiningTask>& activeTasks,
    QubicConnection& connection,
    std::atomic<bool>& startNewJob,
    std::atomic<uint64_t>& solutionsFound,
    const DispatcherSigningContext& signingCtx
)
{
    std::array<uint8_t, 80> header;
    std::array<uint8_t, 32> merkleRoot;
    std::array<uint8_t, 32> scryptHash;
    constexpr unsigned int totalNumBytes = sizeof(RequestResponseHeader) + sizeof(CustomQubicMiningSolution) + sizeof(QubicDogeMiningSolution) + SIGNATURE_SIZE;
    std::vector<uint8_t> solutionBuffer(totalNumBytes);

    while (!st.stop_requested())
    {
    start_new_job:
        InternalMiningTask task = activeTasks.pop(); // blocks until data is available
        startNewJob = false;

        // Full header can be constructed via concatenating partialHeader1 + merkleRoot + partialHeader2 + nonce.
        memcpy(header.data(), task.partialHeader1.data(), task.partialHeader1.size());
        memcpy(header.data() + task.partialHeader1.size() + merkleRoot.size(), task.partialHeader2.data(), task.partialHeader2.size());

        // First 4 bytes in extraNonce2 need to be set to indicate computor id.
        uint32_t extraNonce2High = computorId;
        std::array<uint8_t, 8> extraNonce2;
        // TODO: verify format for copying extraNonce2, currently using big endian
        for (unsigned int b = 0; b < 4; ++b)
            extraNonce2[b] = (extraNonce2High >> ((3-b) * 8)) & 0xFF;

        uint32_t* extraNonce2LowPtr = reinterpret_cast<uint32_t*>(extraNonce2.data() + 4);

        for (*extraNonce2LowPtr = 0; *extraNonce2LowPtr <= (std::numeric_limits<uint32_t>::max)(); ++(*extraNonce2LowPtr))
        {
            merkleRoot = calculateMerkleRoot(task.coinbase1, task.coinbase2, task.extraNonce1, extraNonce2, task.merkleBranches);

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
                        std::cout << "miningLoop: Found a share that PASSES dispatcher difficulty." << std::endl;
                    }
                    else
                        std::cout << "miningLoop: Sending a wrong share for testing." << std::endl;

                    RequestResponseHeader* packetHeader = reinterpret_cast<RequestResponseHeader*>(solutionBuffer.data());
                    packetHeader->zeroDejavu();
                    packetHeader->setSize(totalNumBytes);
                    packetHeader->setType(CustomQubicMiningSolution::type());

                    uint64_t offset = sizeof(RequestResponseHeader);
                    CustomQubicMiningSolution* qubicSol = reinterpret_cast<CustomQubicMiningSolution*>(solutionBuffer.data() + offset);
                    memcpy(qubicSol->sourcePublicKey.data(), signingCtx.publicKey.data(), 32);
                    qubicSol->jobId = task.jobId;
                    qubicSol->customMiningType = CustomMiningType::DOGE;
                    offset += sizeof(CustomQubicMiningSolution);

                    QubicDogeMiningSolution* dogeSol = reinterpret_cast<QubicDogeMiningSolution*>(solutionBuffer.data() + offset);
                    memcpy(dogeSol->nTime.data(), header.data() + 68, 4);
                    memcpy(dogeSol->nonce.data(), header.data() + 76, 4);
                    memcpy(dogeSol->merkleRoot.data(), merkleRoot.data(), merkleRoot.size());
                    memcpy(dogeSol->extraNonce2.data(), extraNonce2.data(), extraNonce2.size());
                    offset += sizeof(QubicDogeMiningSolution);

                    // Sign everything after the header (before the signature).
                    const uint8_t* signDataStart = solutionBuffer.data() + sizeof(RequestResponseHeader);
                    unsigned int signDataSize = offset - sizeof(RequestResponseHeader);
                    signTaskPacket(signingCtx, signDataStart, signDataSize, solutionBuffer.data() + offset);

                    connection.sendMessage(reinterpret_cast<char*>(solutionBuffer.data()), totalNumBytes);
                }
            }
        }
    }

stop_mining:
    return;
}