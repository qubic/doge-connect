#include "task_dist_thread.h"

#include <stop_token>
#include <iostream>

#include "log.h"
#include <chrono>
#include <cstdint>
#include <cstring>
#include <atomic>

#include <nlohmann/json.hpp>

#include "concurrency/concurrent_queue.h"
#include "connection/qubic_connection.h"
#include "crypto/dispatcher_signing.h"
#include "hash_util/hash_util.h"
#include "hash_util/difficulty.h"
#include "structs.h"


void distributeTask(
    nlohmann::json task,
    ConcurrentHashMap<uint64_t, DispatcherMiningTask>& activeTasks,
    std::vector<QubicConnection>& connections,
    const DifficultyTarget& currentPoolDifficulty,
    const DifficultyTarget& dispatcherDifficulty,
    const std::vector<uint8_t>& extraNonce1,
    bool propagateCleanJobFlag,
    const DispatcherSigningContext& signingCtx,
    DispatcherStats& stats
)
{
    const nlohmann::json& params = task["params"];

    // --- Example params ---
    // taskId: "c7b0",
    // prevHash: "cbf0a10805b2fceb439afcfd59c606e1493eea5842fd466594310cbabbfc0eef",
    // coinbase1: "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff5f03d29c2e182f5669614254432f4d696e6564206279207162636467652f2cfabe6d6dc1b3587b34a659a2d2b22cd824879ca3ba5a9c16506f5867bd7ffaa8c4608a71200000000000000010b0c79e016d4450ef",
    // coinbase2: "ffffffff027aea4025000000001976a91493e278b485679cfd2d21441a94bb29dd7c99d72088ac0000000000000000266a24aa21a9ed22f87ab1993f0c5fbceb0366553569cf27b2aeb731270432718d16ddf1b501b400000000",
    // merkleBranches: ["e867437629d527ed4097ceb6e6d1819967afda952875735d28686d2afe17ec8f", "01e36e2cd95ecb859ec06702a1f70d4cb1c6a5ef177bc73678e560e20b268a21", "2e4cfa11da0392bdad7675de0a80064d72f918780766d4122b5f7692b27969b6"],
    // version: "20000000",
    // nbits: "192a848a",
    // ntime: "698da47c",
    // cleanJobQueue: true
    // --------------------

    bool cleanJobQueue = params[8].get<bool>() || propagateCleanJobFlag;
    if (cleanJobQueue)
        activeTasks.clear();

    // Build DispatcherMiningTask
    DispatcherMiningTask dispatcherTask;
    dispatcherTask.taskId = params[0];

    std::vector<uint8_t> version = hexToBytes(params[5], ByteArrayFormat::LittleEndian);
    std::vector<uint8_t> prevHash = hexToBytes(params[1], ByteArrayFormat::LittleEndian);
    std::vector<uint8_t> ntime = hexToBytes(params[7], ByteArrayFormat::LittleEndian);
    std::vector<uint8_t> nbits = hexToBytes(params[6], ByteArrayFormat::LittleEndian);

    if (version.size() != 4 || prevHash.size() != 32 || ntime.size() != 4 || nbits.size() != 4)
    {
        ERR() << "distributeTask: unexpected size encountered ("
            << "version " << version.size() << " vs 4, "
            << "nTime " << ntime.size() << " vs 4, "
            << "nBits " << nbits.size() << " vs 4, "
            << "prevHash " << prevHash.size() << " vs 32)."
            << std::endl;
        return;
    }

    memcpy(dispatcherTask.partialHeader.data(), version.data(), 4);
    memcpy(dispatcherTask.partialHeader.data() + 4, prevHash.data(), 32);
    memcpy(dispatcherTask.nBits.data(), nbits.data(), 4);

    dispatcherTask.targetPool = currentPoolDifficulty.getFullRep();
    dispatcherTask.targetDispatcher = dispatcherDifficulty.getFullRep();

    dispatcherTask.coinbase1 = hexToBytes(params[2], ByteArrayFormat::BigEndian);
    dispatcherTask.coinbase2 = hexToBytes(params[3], ByteArrayFormat::BigEndian);
    dispatcherTask.extraNonce1 = extraNonce1;
    for (const nlohmann::json& merkleBranch : params[4])
    {
        dispatcherTask.merkleBranches.push_back(hexToBytes(merkleBranch, ByteArrayFormat::BigEndian));
    }

    // Build QubicDogeMiningTask with its payload

    static std::array<char, 4096> buffer;

    uint64_t totalNumBytes = sizeof(RequestResponseHeader) + sizeof(CustomQubicMiningTask) + sizeof(QubicDogeMiningTask)
        + dispatcherTask.extraNonce1.size()
        + dispatcherTask.coinbase1.size()
        + dispatcherTask.coinbase2.size()
        + dispatcherTask.merkleBranches.size() * sizeof(unsigned int);
    for (const auto& branch : dispatcherTask.merkleBranches)
    {
        totalNumBytes += branch.size();
    }
    totalNumBytes += SIGNATURE_SIZE;
    if (totalNumBytes > buffer.size())
    {
        ERR() << "distributeTask: QubicDogeMiningTask including payload is larger than buffer." << std::endl;
        return;
    }

    RequestResponseHeader* header = reinterpret_cast<RequestResponseHeader*>(buffer.data());
    header->zeroDejavu(); // distribute message to peers
    header->setSize(totalNumBytes);
    header->setType(CustomQubicMiningTask::type());

    uint64_t offset = sizeof(RequestResponseHeader);

    CustomQubicMiningTask* qubicTask = reinterpret_cast<CustomQubicMiningTask*>(buffer.data() + offset);
    qubicTask->customMiningType = CustomMiningType::DOGE;
    
    offset += sizeof(CustomQubicMiningTask);
    QubicDogeMiningTask* dogeTask = reinterpret_cast<QubicDogeMiningTask*>(buffer.data() + offset);
    dogeTask->cleanJobQueue = cleanJobQueue;
    dogeTask->dispatcherDifficulty = dispatcherDifficulty.getCompactRep();

    memcpy(dogeTask->version.data(), version.data(), 4);
    memcpy(dogeTask->nTime.data(), ntime.data(), 4);
    memcpy(dogeTask->nBits.data(), nbits.data(), 4);
    memcpy(dogeTask->prevHash.data(), prevHash.data(), 32);

    dogeTask->extraNonce1NumBytes = extraNonce1.size();
    dogeTask->coinbase1NumBytes = dispatcherTask.coinbase1.size();
    dogeTask->coinbase2NumBytes = dispatcherTask.coinbase2.size();
    dogeTask->numMerkleBranches = dispatcherTask.merkleBranches.size();

    // Copy payload to buffer behind the QubicDogeMiningTask.
    offset += sizeof(QubicDogeMiningTask);
    memcpy(buffer.data() + offset, extraNonce1.data(), extraNonce1.size());
    offset += extraNonce1.size();
    memcpy(buffer.data() + offset, dispatcherTask.coinbase1.data(), dispatcherTask.coinbase1.size());
    offset += dispatcherTask.coinbase1.size();
    memcpy(buffer.data() + offset, dispatcherTask.coinbase2.data(), dispatcherTask.coinbase2.size());
    offset += dispatcherTask.coinbase2.size();
    unsigned int* branchesNumBytes = reinterpret_cast<unsigned int*>(buffer.data() + offset);
    for (int i = 0; i < dispatcherTask.merkleBranches.size(); ++i)
    {
        branchesNumBytes[i] = dispatcherTask.merkleBranches[i].size();
    }
    offset += dispatcherTask.merkleBranches.size() * sizeof(unsigned int);
    for (const auto& branch : dispatcherTask.merkleBranches)
    {
        memcpy(buffer.data() + offset, branch.data(), branch.size());
        offset += branch.size();
    }

    // Use millisecond timestamp as job ID (unique, meaningful, survives restarts).
    // Must be set before signing so the signature covers the final payload.
    qubicTask->jobId = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    // Sign the task data (everything after the header, before the signature).
    const uint8_t* signDataStart = reinterpret_cast<const uint8_t*>(buffer.data()) + sizeof(RequestResponseHeader);
    unsigned int signDataSize = offset - sizeof(RequestResponseHeader);
    signTaskPacket(signingCtx, signDataStart, signDataSize, reinterpret_cast<uint8_t*>(buffer.data() + offset));
    offset += SIGNATURE_SIZE;

    if (offset != totalNumBytes)
    {
        ERR() << "distributeTask: Something went wrong in building QubicDogeMiningTask or its payload" << std::endl;
        return;
    }

    // Send task to the Qubic network.
    unsigned int numSends = 0;
    for (auto& connection : connections)
    {
        if (connection.sendMessage(buffer.data(), totalNumBytes))
            numSends++;
    }
    LOG() << "Task " << qubicTask->jobId << " sent (" << numSends << "/" << connections.size() << " conns)"
        << " | pool job: " << dispatcherTask.taskId
        << " | prevHash: " << std::string(params[1]).substr(0, 16) << "..."
        << " | nTime: " << std::string(params[7])
        << " | nBits: " << std::string(params[6])
        << " | merkle branches: " << dispatcherTask.merkleBranches.size()
        << " | clean: " << (cleanJobQueue ? "yes" : "no")
        << " | size: " << totalNumBytes << "B"
        << std::endl;

    activeTasks.insert(qubicTask->jobId, std::move(dispatcherTask));
    stats.tasksDistributed++;
}

void checkShareResponse(const nlohmann::json& msg)
{
    // This is the response to a share submission via 'mining.submit'.
    unsigned int shareId = msg["id"];
    // TODO: verify that id matches submitted share.
    if (msg["result"] == false)
    {
        // Share was rejected, the error contains the reason.
        // Example JSON: {"id": 4, "result": false, "error": [21, "Job not found", null]}
        if (msg["error"] != nullptr && msg["error"].size() > 1)
            LOG() << "Share with submission id " << shareId << " was rejected by pool. Reason: " << msg["error"][1] << " (error code " << msg["error"][0] << ")." << std::endl;
        else
            LOG() << "Share with submission id " << shareId << " was rejected by pool." << std::endl;
    }
    else
    {
        // Share was accepted.
        // Example JSON: {"id": 4, "result": true, "error": null}
        LOG() << "Share with submission id " << shareId << " was accepted by pool." << std::endl;
    }
}

void taskDistributionLoop(
    std::stop_token st,
    ConcurrentQueue<nlohmann::json>& queue,
    ConcurrentHashMap<uint64_t, DispatcherMiningTask>& activeTasks,
    std::vector<QubicConnection>& connections,
    const DifficultyTarget& basePoolDifficulty,
    DifficultyTarget& currentPoolDifficulty,
    const DifficultyTarget& dispatcherDifficulty,
    const std::vector<uint8_t>& extraNonce1,
    const DispatcherSigningContext& signingCtx,
    DispatcherStats& stats
)
{
    uint64_t poolBaseDiffDivisor = 1; // TODO: confirm that this is always an integer, not a floating point number.

    while (!st.stop_requested())
    {
        nlohmann::json msg = queue.pop(); // pop() blocks until data is available
        if (!msg.contains("id"))
            continue;
        if (msg["id"] == nullptr)
        {
            if (msg["method"] == "mining.set_difficulty")
            {
                // Example JSON: {"id": null, "method": "mining.set_difficulty", "params": [65536]}
                poolBaseDiffDivisor = msg["params"][0];
                currentPoolDifficulty = DifficultyTarget(divideTarget(basePoolDifficulty.getFullRep(), poolBaseDiffDivisor));
                stats.poolDifficulty.store(poolBaseDiffDivisor);
            }
            else if (msg["method"] == "mining.notify")
            {
                // Drain any additional queued messages so we only distribute the latest job.
                // This avoids sending a burst of stale jobs during startup or after reconnect.
                // Check for cleanJobQueue flag and propagate it to the latest job.
                nlohmann::json latestNotify = std::move(msg);
                unsigned int skipped = 0;
                bool cleanJobQueue = latestNotify["params"][8].get<bool>();
                while (auto next = queue.try_pop())
                {
                    nlohmann::json& nextMsg = *next;
                    if (!nextMsg.contains("id"))
                        continue;
                    if (nextMsg["id"] == nullptr)
                    {
                        if (nextMsg["method"] == "mining.set_difficulty")
                        {
                            poolBaseDiffDivisor = nextMsg["params"][0];
                            currentPoolDifficulty = DifficultyTarget(divideTarget(basePoolDifficulty.getFullRep(), poolBaseDiffDivisor));
                            stats.poolDifficulty.store(poolBaseDiffDivisor);
                        }
                        else if (nextMsg["method"] == "mining.notify")
                        {
                            skipped++;
                            cleanJobQueue |= nextMsg["params"][8].get<bool>();
                            latestNotify = std::move(nextMsg);
                        }
                    }
                    else if (nextMsg["id"].is_number_unsigned())
                    {
                        checkShareResponse(nextMsg);
                    }
                }
                if (skipped > 0)
                    LOG() << "Skipped " << skipped << " queued job(s), distributing latest only." << std::endl;

                distributeTask(std::move(latestNotify), activeTasks, connections, currentPoolDifficulty,
                    dispatcherDifficulty, extraNonce1, cleanJobQueue, signingCtx, stats);
            }
        }
        else if (msg["id"].is_number_unsigned())
        {
            checkShareResponse(msg);
        }
    }
}