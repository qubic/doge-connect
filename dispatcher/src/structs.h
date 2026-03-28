#pragma once

#include <array>
#include <atomic>
#include <vector>
#include <cstdint>

constexpr uint8_t SIGNATURE_SIZE = 64;

/**
 * @brief Shared statistics counters for monitoring the dispatcher at runtime.
 */
struct DispatcherStats
{
    std::atomic<uint64_t> tasksDistributed{0};
    std::atomic<uint64_t> solutionsReceived{0};
    std::atomic<uint64_t> solutionsAccepted{0};
    std::atomic<uint64_t> solutionsRejected{0};
    std::atomic<uint64_t> solutionsPassedPoolDiff{0};
    std::atomic<uint64_t> poolDifficulty{1};
};

enum CustomMiningType : uint8_t
{
    DOGE,
};

#pragma pack(push, 1) // pack all structs defined in this header tightly

/**
 * @brief A generic custom mining struct that can contain mining task descriptions for different types.
 */
struct CustomQubicMiningTask
{
    uint64_t jobId; // millisecond timestamp as dispatcher job id
    uint8_t customMiningType;

    // Followed by the specific task struct, e.g. QubicDogeMiningTask for CustomMiningType::DOGE.

    // Followed by the dispatcher signature (SIGNATURE_SIZE bytes).

    static constexpr unsigned char type()
    {
        return 68;
    }
};

/**
 * @brief A struct for sending a mining task to the Qubic network.
 */
struct QubicDogeMiningTask
{
    uint8_t cleanJobQueue; // flag indicating whether previous jobs should be dropped
    std::array<uint8_t, 4> dispatcherDifficulty; // dispatcher difficulty, usually lower than pool and network difficulty, same compact format
    
    // The Dispatcher always expects a size of 8 bytes for the extraNonce2, 4 bytes for comp id, 4 bytes for miner to iterate.
    static constexpr unsigned int extraNonce2NumBytes = 8;

    // Data for building the block header, the byte arrays are in the
    // correct order for copying into the header directly.
    std::array<uint8_t, 4> version; // version, little endian
    std::array<uint8_t, 4> nTime; // timestamp, little endian
    std::array<uint8_t, 4> nBits; // network difficulty, little endian
    std::array<uint8_t, 32> prevHash; // previous hash, little endian
    unsigned int extraNonce1NumBytes;
    unsigned int coinbase1NumBytes;
    unsigned int coinbase2NumBytes;
    unsigned int numMerkleBranches;
    // Followed by the payload in the order
    // - extraNonce1
    // - coinbase1
    // - coinbase2
    // - merkleBranch1NumBytes (unsigned int), ... , merkleBranchNNumBytes (unsigned int)
    // - merkleBranch1, ... , merkleBranchN
    // Note: extraNonce1, coinbase1/2, and merkle branches have the same byte order as sent via stratum,
    // which should be correct for constructing the merkle root.
};

/**
 * @brief A generic custom mining struct that can contain mining solutions for different types.
 */
struct CustomQubicMiningSolution
{
    std::array<uint8_t, 32> sourcePublicKey; // public key of the sender (miner), used for signature verification
    uint64_t jobId; // millisecond timestamp as dispatcher job id
    uint8_t customMiningType;

    // Followed by the specific solution struct, e.g. QubicDogeMiningSolution for CustomMiningType::DOGE.

    // Followed by the sender's signature (SIGNATURE_SIZE bytes).

    static constexpr unsigned char type()
    {
        return 69;
    }
};

/**
 * @brief A struct for receiving mining solutions from the Qubic network.
 */
struct QubicDogeMiningSolution
{
    std::array<uint8_t, 4> nTime; // the miner's rolling timestamp, little endian (same byte order as used in the block header)
    std::array<uint8_t, 4> nonce; // little endian (same byte order as used in the block header)
    std::array<uint8_t, 32> merkleRoot; // to avoid dispatcher having to calculate the root again, same byte order as used in the header
    std::array<uint8_t, 8> extraNonce2; // same byte order as it was used to create the merkle root
};

/**
 * @brief A struct used by the Dispatcher internally to save currently active mining tasks.
 */
struct DispatcherMiningTask
{
    std::string taskId; // the pool's taskId

    std::array<uint8_t, 32> targetPool; // current (i.e. when receiving the task) pool difficulty target converted to a 256-bit number (little endian)
    std::array<uint8_t, 32> targetDispatcher; // dispatcher difficulty target converted to a 256-bit number (little endian)

    // Full header can be constructed via concatenating partialHeader1 + merkleRoot + miner's nTime + nBits + miner's nonce.
    std::array<uint8_t, 36> partialHeader; // 4 bytes version, 32 bytes prevBlockHash
    std::array<uint8_t, 4> nBits; // 4 bytes network difficulty (nBits)

    // Note: extraNonce1, coinbase1/2, and merkle branches have the same byte order as sent via stratum,
    // which should be correct for constructing the merkle root.
    std::vector<uint8_t> extraNonce1;
    std::vector<uint8_t> coinbase1;
    std::vector<uint8_t> coinbase2;
    std::vector<std::vector<uint8_t>> merkleBranches;

    // The Dispatcher always expects a size of 8 bytes for the extraNonce2, 4 bytes for comp id, 4 bytes for miner to iterate.
    static constexpr unsigned int extraNonce2NumBytes = 8;
};

/**
 * @brief A struct used by the Dispatcher internally to save received solutions for validation.
 */
struct DispatcherMiningSolution
{
    uint64_t jobId; // millisecond timestamp as dispatcher job id
    std::array<uint8_t, 4> nTime; // the miner's rolling timestamp, little endian
    std::array<uint8_t, 4> nonce; // little endian
    std::array<uint8_t, 32> merkleRoot; // to avoid dispatcher having to calculate the root again, same byte order as it appears in the header
    std::array<uint8_t, 8> extraNonce2; // same byte order as it was used to create the merkle root
};

// ----- Structs for the Qubic connection -----

struct RequestResponseHeader
{
private:
    uint8_t _size[3];
    uint8_t _type;
    unsigned int _dejavu;

public:
    inline unsigned int size() const
    {
        if (((*((unsigned int*)_size)) & 0xFFFFFF) == 0) return INT32_MAX; // size is never zero, zero means broken packets
        return (*((unsigned int*)_size)) & 0xFFFFFF;
    }

    inline void setSize(unsigned int size)
    {
        _size[0] = (uint8_t)size;
        _size[1] = (uint8_t)(size >> 8);
        _size[2] = (uint8_t)(size >> 16);
    }

    inline unsigned int dejavu() const
    {
        return _dejavu;
    }

    inline void zeroDejavu()
    {
        _dejavu = 0;
    }

    inline uint8_t type() const
    {
        return _type;
    }

    inline void setType(const uint8_t type)
    {
        _type = type;
    }
};

struct ExchangePublicPeers
{
    unsigned char peers[4][4];

    static constexpr unsigned char type()
    {
        return 0;
    }
};

// --------------------------------------------

#pragma pack(pop) // restore original alignment
