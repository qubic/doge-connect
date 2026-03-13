#include "hash_util.h"

#include <vector>
#include <string>
#include <array>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <span>
#include <cstdint>

#include "scrypt.h"


std::vector<uint8_t> hexToBytes(const std::string& hex, ByteArrayFormat byteFormat)
{
    std::vector<uint8_t> bytes;
    std::size_t lengthFullBytes = 2 * (hex.length() / 2); // round down to even number if length is odd
    for (std::size_t i = 0; i < lengthFullBytes; i += 2)
    {
        bytes.push_back((uint8_t)strtol(hex.substr(i, 2).c_str(), NULL, 16));
    }
    if (byteFormat == ByteArrayFormat::LittleEndian)
        std::reverse(bytes.begin(), bytes.end()); // convert to little-endian

    return bytes;
}

std::string bytesToHex(std::span<const uint8_t> bytes, ByteArrayFormat byteFormat)
{
    if (bytes.empty()) return "";

    std::stringstream ss;
    ss << std::hex << std::setfill('0');

    if (byteFormat == ByteArrayFormat::BigEndian)
    {
        // bytes[0] is the start of the string
        for (const auto& byte : bytes)
            ss << std::setw(2) << static_cast<int>(byte);
    }
    else
    {
        // bytes[0] is the end of the string (least significant), so we have to iterate backwards.
        for (auto it = bytes.rbegin(); it != bytes.rend(); ++it)
            ss << std::setw(2) << static_cast<int>(*it);
    }

    return ss.str();
}

std::array<uint8_t, 32> doubleSHA256(const std::vector<uint8_t>& data)
{
    SHA256_CTX ctx;
    std::array<uint8_t, 32> firstHash;
    std::array<uint8_t, 32> secondHash;

    // --- Round 1: Hash the input data ---
    SHA256_Init(&ctx);
    SHA256_Update(&ctx, data.data(), data.size());
    SHA256_Final(firstHash.data(), &ctx);

    // --- Round 2: Hash the result of Round 1 ---
    SHA256_Init(&ctx);
    SHA256_Update(&ctx, firstHash.data(), firstHash.size());
    SHA256_Final(secondHash.data(), &ctx);

    return secondHash;
}

std::array<uint8_t, 32> calculateMerkleRoot(
    const std::vector<uint8_t>& coinbase1,
    const std::vector<uint8_t>& coinbase2,
    const std::vector<uint8_t>& extraNonce1,
    const std::vector<uint8_t>& extraNonce2,
    const std::vector<std::vector<uint8_t>>& merkleBranches
)
{
    // Build the coinbase transaction by concatenation.
    std::vector<uint8_t> coinbase;
    coinbase.reserve(coinbase1.size() + extraNonce1.size() + extraNonce2.size() + coinbase2.size());

    coinbase.insert(coinbase.end(), coinbase1.begin(), coinbase1.end());
    coinbase.insert(coinbase.end(), extraNonce1.begin(), extraNonce1.end());
    coinbase.insert(coinbase.end(), extraNonce2.begin(), extraNonce2.end());
    coinbase.insert(coinbase.end(), coinbase2.begin(), coinbase2.end());

    // Initial hash of the coinbase (the first leaf).
    std::array<uint8_t, 32> currentHash = doubleSHA256(coinbase);

    // Iteratively hash with merkle branches. The hash derived from the coinbase is always on the left.
    for (const auto& branch : merkleBranches)
    {
        std::vector<uint8_t> concat;
        concat.reserve(currentHash.size() + branch.size());

        concat.insert(concat.end(), currentHash.begin(), currentHash.end());
        concat.insert(concat.end(), branch.begin(), branch.end());

        currentHash = doubleSHA256(concat);
    }

    return currentHash;
}

bool verifyHashVsTarget(const std::array<uint8_t, 32>& hash, const std::array<uint8_t, 32>& target)
{
    // Start from the most significant byte (the end of the array).
    for (int i = 31; i >= 0; --i)
    {
        if (hash[i] < target[i]) return true;  // hash is smaller
        if (hash[i] > target[i]) return false; // hash is larger
    }
    return true; // exactly equal
}
