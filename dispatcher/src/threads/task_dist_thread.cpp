#include "task_dist_thread.h"

#include <stop_token>
#include <iostream>
#include <cstdint>
#include <cstring>

#include <nlohmann/json.hpp>

#include "concurrency/concurrent_queue.h"
#include "connection/qubic_connection.h"
#include "hash_util/hash_util.h"
#include "hash_util/difficulty.h"
#include "structs.h"


void distributeTask(
    nlohmann::json task,
    ConcurrentHashMap<uint64_t, DispatcherMiningTask>& activeTasks,
    std::vector<QubicConnection>& connections,
    const DifficultyTarget& currentPoolDifficulty,
    const DifficultyTarget& dispatcherDifficulty,
    std::atomic<uint64_t>& dispatcherJobId,
    const std::vector<uint8_t>& extraNonce1,
    unsigned int extraNonce2NumBytes
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

    bool cleanJobQueue = params[8];
    if (cleanJobQueue)
        activeTasks.clear();

    // Build DispatcherMiningTask
    DispatcherMiningTask dispatcherTask;
    dispatcherTask.taskId = params[0];
    dispatcherTask.nTimeHex = params[7];

    std::vector<uint8_t> version = hexToBytes(params[5], ByteArrayFormat::LittleEndian);
    std::vector<uint8_t> prevHash = hexToBytes(params[1], ByteArrayFormat::LittleEndian);
    std::vector<uint8_t> ntime = hexToBytes(params[7], ByteArrayFormat::LittleEndian);
    std::vector<uint8_t> nbits = hexToBytes(params[6], ByteArrayFormat::LittleEndian);

    if (version.size() != 4 || prevHash.size() != 32 || ntime.size() != 4 || nbits.size() != 4)
    {
        std::cerr << "distributeTask: unexpected size encountered ("
            << "version " << version.size() << " vs 4, "
            << "nTime " << ntime.size() << " vs 4, "
            << "nBits " << nbits.size() << " vs 4, "
            << "prevHash " << prevHash.size() << " vs 32)."
            << std::endl;
        return;
    }

    memcpy(dispatcherTask.partialHeader1.data(), version.data(), 4);
    memcpy(dispatcherTask.partialHeader1.data() + 4, prevHash.data(), 32);
    memcpy(dispatcherTask.partialHeader2.data(), ntime.data(), 4);
    memcpy(dispatcherTask.partialHeader2.data() + 4, nbits.data(), 4);

    dispatcherTask.targetPool = currentPoolDifficulty.getFullRep();
    dispatcherTask.targetDispatcher = dispatcherDifficulty.getFullRep();

    dispatcherTask.extraNonce2NumBytes = extraNonce2NumBytes;
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
        std::cerr << "distributeTask: QubicDogeMiningTask including payload is larger than buffer." << std::endl;
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
    dogeTask->extraNonce2NumBytes = extraNonce2NumBytes;

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

    // TODO: calculate signature and write to buffer
    memset(buffer.data() + offset, 0, SIGNATURE_SIZE);
    offset += SIGNATURE_SIZE;

    if (offset != totalNumBytes)
    {
        std::cerr << "distributeTask: Something went wrong in building QubicDogeMiningTask or its payload" << std::endl;
        return;
    }

    // Only increase dispatcherJobId once we know that task building worked.
    dispatcherJobId++;
    qubicTask->jobId = dispatcherJobId;

    // Send task to the Qubic network.
    unsigned int numSends = 0;
    for (auto& connection : connections)
    {
        if (connection.sendMessage(buffer.data(), totalNumBytes))
            numSends++;
    }
    std::cout << "Task " << qubicTask->jobId << " sent to the qubic network (" << numSends << "/" << connections.size() << " connections successful)." << std::endl;

    activeTasks.insert(qubicTask->jobId, std::move(dispatcherTask));
}

void taskDistributionLoop(
    std::stop_token st,
    ConcurrentQueue<nlohmann::json>& queue,
    ConcurrentHashMap<uint64_t, DispatcherMiningTask>& activeTasks,
    std::vector<QubicConnection>& connections,
    const DifficultyTarget& basePoolDifficulty,
    DifficultyTarget& currentPoolDifficulty,
    const DifficultyTarget& dispatcherDifficulty,
    std::atomic<uint64_t>& dispatcherJobId,
    const std::vector<uint8_t>& extraNonce1,
    unsigned int extraNonce2NumBytes
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
            }
            else if (msg["method"] == "mining.notify")
            {
                distributeTask(std::move(msg), activeTasks, connections, currentPoolDifficulty, 
                    dispatcherDifficulty, dispatcherJobId, extraNonce1, extraNonce2NumBytes);
            }
        }
        else if (msg["id"].is_number_unsigned())
        {
            // This is the response to a share submission via 'mining.submit'.
            unsigned int shareId = msg["id"];
            // TODO: verify that id matches submitted share.
            if (msg["result"] == false)
            {
                // Share was rejected, the error contains the reason.
                // Example JSON: {"id": 4, "result": false, "error": [21, "Job not found", null]}
                std::cout << "Share with submission id " << shareId << " was rejected by pool.";
                if (msg["error"] != nullptr && msg["error"].size() > 1)
                {
                    std::cout << " Reason: " << msg["error"][1] << " (error code " << msg["error"][0] << ").";
                }
                std::cout << std::endl;
            }
            else
            {
                // Share was accepted.
                // Example JSON: {"id": 4, "result": true, "error": null}
                std::cout << "Share with submission id " << shareId << " was accepted by pool." << std::endl;
            }
        }
    }
}