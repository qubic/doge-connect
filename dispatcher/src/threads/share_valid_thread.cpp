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
#include "crypto/key_utils.h"
#include "hash_util/scrypt.h"
#include "hash_util/hash_util.h"
#include "hash_util/difficulty.h"
#include "structs.h"

// Helper: get short identity string (first 8 chars) from public key.
static std::string shortIdentity(const std::array<uint8_t, 32>& pubkey)
{
    char identity[61] = {0};
    getIdentityFromPublicKey(pubkey.data(), identity, false);
    return std::string(identity).substr(0, 8) + "..";
}

// Helper: compute difficulty of a hash relative to the scrypt base target (0x1f00ffff).
// Returns 0 if the hash is all zeros.
static uint64_t hashDifficulty(const std::array<uint8_t, 32>& hash)
{
    // Base target for scrypt difficulty 1: 0x1f00ffff compact.
    static const std::array<uint8_t, 32> baseTarget = calculateFullRepFromCompactRep({0xff, 0xff, 0x00, 0x1f});

    // Find the most significant non-zero byte of the hash.
    int msb = -1;
    for (int i = 31; i >= 0; --i)
    {
        if (hash[i] != 0) { msb = i; break; }
    }
    if (msb < 0) return 0;

    // Approximate difficulty = baseTarget / hash using the top bytes.
    // Use 8 bytes from the MSB of each for a 64-bit division.
    uint64_t hashTop = 0, targetTop = 0;
    for (int i = 0; i < 8; ++i)
    {
        int hi = msb - i;
        hashTop = (hashTop << 8) | (hi >= 0 ? hash[hi] : 0);
    }
    // Align target to same byte position.
    for (int i = 0; i < 8; ++i)
    {
        int ti = msb - i;
        targetTop = (targetTop << 8) | (ti >= 0 && ti < 32 ? baseTarget[ti] : 0);
    }
    if (hashTop == 0) return 0;
    return targetTop / hashTop;
}


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
            stats.solutionsStale++;
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

        // Debug: log header immediately before scrypt to confirm input bytes.
        LOG() << "shareValidationLoop: scrypt input=" << bytesToHex(std::span<const uint8_t>(fullHeader.data(), 80), ByteArrayFormat::BigEndian) << std::endl;
        scrypt_1024_1_1_256(reinterpret_cast<char*>(fullHeader.data()), reinterpret_cast<char*>(scryptHash.data()));
        LOG() << "shareValidationLoop: scrypt output=" << bytesToHex(scryptHash, ByteArrayFormat::BigEndian) << std::endl;

        uint64_t solDiff = hashDifficulty(scryptHash);
        std::string minerId = shortIdentity(sol.sourcePublicKey);
        // First 4 bytes of extraNonce2 encode the computor ID as U32 % 676 (compatible with qxmr).
        uint32_t extraNonce2High = (sol.extraNonce2[0] << 24) | (sol.extraNonce2[1] << 16) | (sol.extraNonce2[2] << 8) | sol.extraNonce2[3];
        uint32_t computorIdx = extraNonce2High % 676;

        if (!verifyHashVsTarget(scryptHash, task.targetPool))
        {
            LOG() << "shareValidationLoop: Solution from " << minerId << " comp " << computorIdx << " job " << sol.jobId
                << " FAILED pool diff (hash diff " << solDiff << ", required " << stats.poolDifficulty.load() << ")."
                << " hash=" << bytesToHex(scryptHash, ByteArrayFormat::LittleEndian)
                << " header=" << bytesToHex(std::span<const uint8_t>(fullHeader.data(), 80), ByteArrayFormat::BigEndian)
                << std::endl;
            stats.solutionsRejected++;
            continue;
        }

        LOG() << "shareValidationLoop: Solution from " << minerId << " comp " << computorIdx
            << " PASSED pool diff (" << solDiff << ")." << std::endl;
        stats.solutionsAccepted++;
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
