#include "share_valid_thread.h"

#include <stop_token>
#include <iostream>

#include "log.h"
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
    const std::string& workerName,
    DispatcherStats& stats
)
{
    std::array<uint8_t, 80> fullHeader;
    std::array<uint8_t, 32> scryptHash;

    while (!st.stop_requested())
    {
        DispatcherMiningSolution sol = queue.pop(); // pop() blocks until data is available

        stats.solutionsReceived++;

        // Check that solution matches an active task.
        std::optional<DispatcherMiningTask> taskOptional = activeTasks.get(sol.jobId);
        if (!taskOptional.has_value())
        {
            LOG() << "shareValidationLoop: Ignoring stale submitted solution (dispatcher jobId " << sol.jobId << ")." << std::endl;
            stats.solutionsRejected++;
            continue;
        }

        const DispatcherMiningTask& task = taskOptional.value();

        // Check that extraNonce2 has expected size.
        if (task.extraNonce2NumBytes != sol.extraNonce2.size())
        {
            LOG() << "shareValidationLoop: Ignoring submitted solution with wrong size of extraNonce2 ("
                << sol.extraNonce2.size() << " vs. " << task.extraNonce2NumBytes << ")." << std::endl;
            stats.solutionsRejected++;
            continue;
        }

        // Build complete header from task and solution info.
        memcpy(fullHeader.data(), task.partialHeader.data(), task.partialHeader.size());
        unsigned int offset = task.partialHeader.size();
        memcpy(fullHeader.data() + offset, sol.merkleRoot.data(), sol.merkleRoot.size());
        offset += sol.merkleRoot.size();
        memcpy(fullHeader.data() + offset, sol.nTime.data(), sol.nTime.size());
        offset += sol.nTime.size();
        memcpy(fullHeader.data() + offset, task.nBits.data(), task.nBits.size());
        offset += task.nBits.size();
        memcpy(fullHeader.data() + offset, sol.nonce.data(), sol.nonce.size());
        offset += sol.nonce.size();

        if (offset != 80)
        {
            ERR() << "shareValidationLoop: Something is wrong with the header size (should be 80 bytes)." << std::endl;
            stats.solutionsRejected++;
            continue;
        }

        scrypt_1024_1_1_256(reinterpret_cast<char*>(fullHeader.data()), reinterpret_cast<char*>(scryptHash.data()));

        if (!verifyHashVsTarget(scryptHash, task.targetDispatcher))
        {
            LOG() << "shareValidationLoop: Submitted solution FAILED Dispatcher target difficulty." << std::endl;
            stats.solutionsRejected++;
            continue;
        }

        LOG() << "shareValidationLoop: Submitted solution PASSED Dispatcher target difficulty." << std::endl;
        stats.solutionsAccepted++;

        if (verifyHashVsTarget(scryptHash, task.targetPool))
        {
            stats.solutionsPassedPoolDiff++;

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
                    bytesToHex(sol.nTime, ByteArrayFormat::LittleEndian),
                    bytesToHex(sol.nonce, ByteArrayFormat::LittleEndian)
                };

                // Stratum messages need to end with newline.
                connection.sendMessage(message.dump() + "\n");
            }
        }
    }
}
