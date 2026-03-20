#pragma once

#include <cstdint>
#include <array>
#include <vector>

/**
 * @brief A struct used by the test miner internally to save active tasks. 
 */
struct InternalMiningTask
{
    uint64_t jobId; // millisecond timestamp as dispatcher job id

    std::array<uint8_t, 32> targetDispatcher; // dispatcher difficulty target converted to a 256-bit number (little endian)

    // Full header can be constructed via concatenating partialHeader1 + merkleRoot + partialHeader2 + nonce.
    std::array<uint8_t, 36> partialHeader1; // 4 bytes version, 32 bytes prevBlockHash
    std::array<uint8_t, 8> partialHeader2; // 4 bytes timestamp (nTime), 4 bytes network difficulty (nBits)

    // Note: extraNonce1, coinbase1/2, and merkle branches have the same byte order as sent via stratum,
    // which should be correct for constructing the merkle root.
    std::vector<uint8_t> extraNonce1;
    std::vector<uint8_t> coinbase1;
    std::vector<uint8_t> coinbase2;
    std::vector<std::vector<uint8_t>> merkleBranches;

    // The Dispatcher always expects a size of 8 bytes for the extraNonce2, 4 bytes for comp id, 4 bytes for miner to iterate.
    static constexpr unsigned int extraNonce2NumBytes = 8;
};