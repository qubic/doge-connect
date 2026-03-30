#include "share_valid_thread.h"

#include <stop_token>
#include <iostream>
#include <unordered_set>

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

// Helper: compute approximate difficulty of a hash relative to the scrypt base target (0x1f00ffff).
// difficulty = baseTarget / hash. Returns 0 if hash is all zeros or higher than base target.
static uint64_t hashDifficulty(const std::array<uint8_t, 32>& hash)
{
    // Base target for scrypt difficulty 1: 0x1f00ffff compact.
    // Non-zero bytes at positions 28 (0xff) and 29 (0xff), i.e. 0x0000ffff << 224.
    // As a 64-bit value shifted to top: 0xffff followed by zeros.
    static constexpr int baseMsb = 29; // MSB position of base target

    // Find MSB of hash.
    int hashMsb = -1;
    for (int i = 31; i >= 0; --i)
    {
        if (hash[i] != 0) { hashMsb = i; break; }
    }
    if (hashMsb < 0) return 0;
    if (hashMsb > baseMsb) return 0; // hash > base target, difficulty < 1

    // Read top 8 bytes of hash from its MSB.
    uint64_t hashTop = 0;
    for (int i = 0; i < 8; ++i)
    {
        int idx = hashMsb - i;
        hashTop = (hashTop << 8) | (idx >= 0 ? hash[idx] : 0);
    }
    if (hashTop == 0) return 0;

    // Shift base target value to align with hashTop's byte position.
    // baseTarget effectively = 0xffff at byte 28-29.
    // If hashMsb == 29: baseTop = 0xffff000000000000 → diff = baseTop / hashTop
    // If hashMsb == 27: hash is 2 bytes lower → diff is 2^16 times higher.
    int byteShift = baseMsb - hashMsb;
    // Base target top 8 bytes from MSB = 0x00ffff0000000000 (byte 29=0xff, 28=0xff, 30=0x00)
    uint64_t baseTop = 0x00FFFF0000000000ULL;

    if (byteShift > 0)
    {
        // Hash MSB is lower than base MSB → difficulty is higher.
        // Each byte shift = 256x more difficulty. Use multiplication to avoid overflow.
        uint64_t diff = baseTop / hashTop;
        for (int i = 0; i < byteShift; ++i)
        {
            if (diff > UINT64_MAX / 256) return UINT64_MAX; // overflow guard
            diff *= 256;
        }
        return diff;
    }
    else
    {
        return baseTop / hashTop;
    }
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

    // Deduplication: track recently submitted (jobId, nonce) pairs to avoid duplicate pool submissions.
    // Solutions may arrive multiple times via qubic gossip from different peers.
    std::unordered_set<std::string> recentSubmits;
    uint64_t lastCleanJobId = 0;

    while (!st.stop_requested())
    {
        DispatcherMiningSolution sol = queue.pop(); // pop() blocks until data is available

        // Deduplicate: key = jobId + nonce + extraNonce2 (unique per share).
        // Skip before counting — duplicates from qubic gossip are not interesting.
        std::string dedupeKey = std::to_string(sol.jobId) + "_"
            + bytesToHex(sol.nonce, ByteArrayFormat::BigEndian)
            + bytesToHex(sol.extraNonce2, ByteArrayFormat::BigEndian);
        if (recentSubmits.count(dedupeKey))
            continue; // silently skip duplicate

        stats.solutionsReceived++;

        // Clear dedup set when a new clean job arrives (old shares are irrelevant).
        if (sol.jobId != lastCleanJobId)
        {
            recentSubmits.clear();
            lastCleanJobId = sol.jobId;
        }

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
            << " job " << sol.jobId << " (pool " << task.taskId << ")"
            << " PASSED pool diff (" << solDiff << ")." << std::endl;
        stats.solutionsAccepted++;
        stats.solutionsPassedPoolDiff++;
        recentSubmits.insert(dedupeKey);

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

            LOG() << "shareValidationLoop: Submitting to pool: " << message.dump() << std::endl;

            // Stratum messages need to end with newline.
            connection.sendMessage(message.dump() + "\n");
        }
    }
}
