/**
 * Replay DOGE oracle validation with exact data from the dispatcher log.
 * Uses the same scrypt implementation as the oracle (qubic-om).
 *
 * Build:
 *   g++ -O2 -o test_oracle test_oracle.cpp \
 *       ../../qubic-om/oracles/doge_share_validation/src/scrypt.c \
 *       -I../../qubic-om/oracles/doge_share_validation/src
 *
 * Run:
 *   ./test_oracle
 */

#include "scrypt.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <array>

static void hexToBytes(const char* hex, uint8_t* out, size_t outLen)
{
    for (size_t i = 0; i < outLen; i++)
    {
        unsigned int byte;
        sscanf(hex + 2 * i, "%02x", &byte);
        out[i] = (uint8_t)byte;
    }
}

static void printHex(const char* label, const uint8_t* data, size_t len)
{
    printf("%s: ", label);
    for (size_t i = 0; i < len; i++)
        printf("%02x", data[i]);
    printf("\n");
}

static bool verifyHashVsTarget(const std::array<uint8_t, 32>& hash, const std::array<uint8_t, 32>& target)
{
    for (int i = 31; i >= 0; --i)
    {
        if (hash[i] < target[i])
            return true;
        if (hash[i] > target[i])
            return false;
    }
    return true;
}

int main()
{
    printf("=== DOGE Oracle Validation Test (C++) ===\n\n");

    // From dispatcher log:
    const char* headerHex =
        "04006200"
        "ea3b3d202568269564db2dbe3f75e57cf36cdb071a915e7dc5f41072832bad39"
        "faad5f092f81a998682df02f63ecd13b049c9a5b7ae8c5388bc68227058c4f56"
        "726dd669"
        "615f5719"
        "8ad63189";

    // Expected scrypt output from dispatcher:
    const char* expectedHashHex =
        "9c942c96e09b88559ba9d374670eed06cee26852edd2fe8f901d140000000000";

    // Target from oracle display:
    const char* oracleTargetHex =
        "00000000000000000000000000000000000000000000000041f1290000000000";

    // Parse inputs
    uint8_t header[80];
    hexToBytes(headerHex, header, 80);

    uint8_t expectedHash[32];
    hexToBytes(expectedHashHex, expectedHash, 32);

    std::array<uint8_t, 32> oracleTarget;
    hexToBytes(oracleTargetHex, oracleTarget.data(), 32);

    // Compute scrypt
    std::array<uint8_t, 32> computedHash;
    scrypt_1024_1_1_256(reinterpret_cast<const char*>(header),
                        reinterpret_cast<char*>(computedHash.data()));

    // Print results
    printHex("Header (80B)   ", header, 80);
    printf("\n");
    printHex("Expected hash  ", expectedHash, 32);
    printHex("Computed hash  ", computedHash.data(), 32);
    printf("Hashes match:    %s\n\n", memcmp(expectedHash, computedHash.data(), 32) == 0 ? "YES" : "NO");

    printHex("Oracle target  ", oracleTarget.data(), 32);

    // Verify: hash <= target
    bool isValid = verifyHashVsTarget(computedHash, oracleTarget);
    printf("\nOracle result:   %s\n", isValid ? "VALID" : "INVALID");

    // Also check byte-by-byte where they differ
    printf("\nByte-by-byte comparison (MSB first):\n");
    for (int i = 31; i >= 0; --i)
    {
        uint8_t h = computedHash[i];
        uint8_t t = oracleTarget[i];
        if (h != t)
        {
            printf("  byte[%2d]: hash=0x%02x %s target=0x%02x → %s\n",
                   i, h, h < t ? "<" : ">", t, h < t ? "VALID" : "INVALID");
            break;
        }
    }

    // Also compute nBits target for comparison
    uint32_t nbits;
    memcpy(&nbits, header + 72, 4); // LE
    uint32_t exponent = (nbits >> 24) & 0xFF;
    uint32_t mantissa = nbits & 0x00FFFFFF;
    printf("\nnBits:           0x%08x (exp=%u, mantissa=0x%06x)\n", nbits, exponent, mantissa);

    // Network target from nBits
    std::array<uint8_t, 32> networkTarget = {};
    if (exponent >= 3 && exponent <= 34)
    {
        int startByte = exponent - 3;
        networkTarget[startByte] = mantissa & 0xFF;
        networkTarget[startByte + 1] = (mantissa >> 8) & 0xFF;
        networkTarget[startByte + 2] = (mantissa >> 16) & 0xFF;
    }
    printHex("Network target ", networkTarget.data(), 32);

    bool passesNetwork = verifyHashVsTarget(computedHash, networkTarget);
    printf("Hash passes network target: %s\n", passesNetwork ? "YES" : "NO");

    return 0;
}
