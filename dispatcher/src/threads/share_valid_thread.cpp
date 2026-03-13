#include "share_valid_thread.h"

#include <stop_token>
#include <iostream>
#include <optional>
#include <cstdint>
#include <cstring>

#include <nlohmann/json.hpp>

#include "concurrency/concurrent_queue.h"
#include "concurrency/concurrent_hashmap.h"
#include "connection/connection.h"
#include "hash_util/scrypt.h"
#include "hash_util/hash_util.h"
#include "structs.h"


void shareValidationLoop(
    std::stop_token st,
    ConcurrentQueue<DispatcherMiningSolution>& queue,
    ConcurrentHashMap<uint64_t, DispatcherMiningTask>& activeTasks,
    std::atomic<uint64_t>& nextStratumSendId,
    Connection& connection,
    const std::string& workerName
)
{
    std::array<uint8_t, 80> fullHeader;
    std::array<uint8_t, 32> scryptHash;

    uint64_t numSubmitted = 0; // total number of submitted solutions
    uint64_t numRejected = 0; // number of rejected solutions (stale task, wrong format, low difficulty)
    uint64_t numAccepted = 0; // number of solutions accepted based on dispatcher difficulty
    // numSubmitted = numRejected + numAccepted
    uint64_t numPassedPoolDiff = 0; // number of solutions that also passed the pool difficulty

    while (!st.stop_requested())
    {
        if (numSubmitted % 1 == 0)
        {
            std::cout << "shareValidation: Number of solutions submitted to Dispatcher " << numSubmitted << " (accepted: " << numAccepted
                << " / rejected: " << numRejected << "). Solutions that passed pool difficulty: " << numPassedPoolDiff << std::endl;
        }

        DispatcherMiningSolution sol = queue.pop(); // pop() blocks until data is available

        numSubmitted++;

        // Check that solution matches an active task.
        std::optional<DispatcherMiningTask> taskOptional = activeTasks.get(sol.jobId);
        if (!taskOptional.has_value())
        {
            std::cout << "shareValidationLoop: Ignoring stale submitted solution (dispatcher jobId " << sol.jobId << ")." << std::endl;
            numRejected++;
            continue;
        }

        const DispatcherMiningTask& task = taskOptional.value();

        // Check that extraNonce2 has expected size.
        if (task.extraNonce2NumBytes != sol.extraNonce2.size())
        {
            std::cout << "shareValidationLoop: Ignoring submitted solution with wrong size of extraNonce2 ("
                << sol.extraNonce2.size() << " vs. " << task.extraNonce2NumBytes << ")." << std::endl;
            numRejected++;
            continue;
        }

        // Build complete header from task and solution info.
        memcpy(fullHeader.data(), task.partialHeader1.data(), task.partialHeader1.size());
        unsigned int offset = task.partialHeader1.size();
        memcpy(fullHeader.data() + offset, sol.merkleRoot.data(), sol.merkleRoot.size());
        offset += sol.merkleRoot.size();
        memcpy(fullHeader.data() + offset, task.partialHeader2.data(), task.partialHeader2.size());
        offset += task.partialHeader2.size();
        memcpy(fullHeader.data() + offset, sol.nonce.data(), sol.nonce.size());
        offset += sol.nonce.size();

        if (offset != 80)
        {
            std::cerr << "shareValidationLoop: Something is wrong with the header size (should be 80 bytes)." << std::endl;
            numRejected++;
            continue;
        }

        scrypt_1024_1_1_256(reinterpret_cast<char*>(fullHeader.data()), reinterpret_cast<char*>(scryptHash.data()));

        if (!verifyHashVsTarget(scryptHash, task.targetDispatcher))
        {
            std::cout << "shareValidationLoop: Submitted solution FAILED Dispatcher target difficulty." << std::endl;
            numRejected++;
            continue;
        }

        std::cout << "shareValidationLoop: Submitted solution PASSED Dispatcher target difficulty." << std::endl;
        numAccepted++;

        if (verifyHashVsTarget(scryptHash, task.targetPool))
        {
            numPassedPoolDiff++;

            if (connection.isConnected())
            {
                // Submit to pool via stratum connection.
                nlohmann::json message;
                message["id"] = nextStratumSendId++;
                message["method"] = "mining.submit";
                message["params"] =
                {
                    workerName,
                    task.taskId,
                    bytesToHex(sol.extraNonce2, ByteArrayFormat::BigEndian),
                    task.nTimeHex,
                    bytesToHex(sol.nonce, ByteArrayFormat::LittleEndian)
                };

                // Stratum messages need to end with newline.
                connection.sendMessage(message.dump() + "\n");
            }
        }
    }
}