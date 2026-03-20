#pragma once

#include <vector>
#include <string>
#include <span>
#include <cstdint>

/**
 * @brief An enum describing different byte array formats.
 */
enum class ByteArrayFormat : uint8_t
{
    LittleEndian, // byteArray[0] contains the least significant byte
    BigEndian, // byteArray[0] contains the most significant byte
};

/**
 * @brief Convert a hex string to a byte array.
 * @param hex Input hex string, without leading "0x". Odd length strings are truncated to have even length.
 * @param byteFormat Byte format of the returned array. If format is BigEndian/LittleEndian, result[0]
                     corresponds to the leftmost/rightmost two characters in the input string.
 * @return The byte array represented by the input string.
 */
std::vector<uint8_t> hexToBytes(const std::string& hex, ByteArrayFormat byteFormat);

/**
 * @brief Convert a byte array to a hex string.
 * @param bytes Input byte array.
 * @param byteFormat Byte format of the input array. If format is BigEndian/LittleEndian, result[0]
                     corresponds to the leftmost/rightmost two characters in the output string.
 * @return Hex string representing the input byte array, without leading "0x".
 */
std::string bytesToHex(std::span<const uint8_t> bytes, ByteArrayFormat byteFormat);

/**
 * @brief Calculate a double SHA-256 hash of the data.
 * @param data The input data for the first hash.
 * @return The result of applying the hash function to the data twice, i.e. hash(hash(data)).
 */
std::array<uint8_t, 32> doubleSHA256(const std::span<uint8_t>& data);

/**
 * @brief Calculate the merkle root by constructing the coinbase transaction and iteratively hashing it with the merkle branches.
 * @param coinbase1 First part of the coinbase transaction.
 * @param coinbase2 Second part of the coinbase transaction.
 * @param extraNonce1 extraNonce1 identifying the miner of the block.
 * @param extraNonce2 extraNonce2 found during mining (to increase nonce search space).
 * @param merkleBranches Branches of the merkle tree of transactions.
 * @return The root of the full merkle tree.
 */
std::array<uint8_t, 32> calculateMerkleRoot(
    const std::span<uint8_t>& coinbase1,
    const std::span<uint8_t>& coinbase2,
    const std::span<uint8_t>& extraNonce1,
    const std::span<uint8_t>& extraNonce2,
    const std::span<std::vector<uint8_t>>& merkleBranches
);

/**
* @brief Verifies the given hash against a target. Both byte arrays are treated as little endian,
         i.e. hash[0] / target[0] contains the least significant byte.
* @param hash The input hash.
* @param target The target to compare against.
* @return True if the hash is smaller or equal than the target, false otherwise.
*/
bool verifyHashVsTarget(const std::array<uint8_t, 32>& hash, const std::array<uint8_t, 32>& target);