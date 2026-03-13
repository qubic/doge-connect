#include "difficulty.h"

#include <array>
#include <cstdint>

#ifdef _MSC_VER
#include <immintrin.h>
// MSVC only supports _udiv128 in 64-bit mode
#endif // GCC/Clang handles __int128 natively


std::array<uint8_t, 32> calculateFullRepFromCompactRep(const std::array<uint8_t, 4>& compactRep)
{
    std::array<uint8_t, 32> fullRep;
    fullRep.fill(0);

    uint8_t exponent = compactRep[3];

    // The target is mantissa * 256^(exponent - 3).
    // This means the mantissa starts at byte index (exponent - 3).
    int start_index = exponent - 3;

    for (int i = 0; i < 3; ++i)
    {
        int target_idx = start_index + i;
        if (target_idx >= 0 && target_idx < 32)
        {
            fullRep[target_idx] = compactRep[i];
        }
    }

    return fullRep;
}

std::array<uint8_t, 4> calculateCompactRepFromFullRep(const std::array<uint8_t, 32>& fullRep)
{
    std::array<uint8_t, 4> compactRep;
    compactRep.fill(0);

    // Find the exponent (index of the most significant byte + 1).
    int exponent = 0;
    for (int i = 31; i >= 0; --i)
    {
        if (fullRep[i] != 0)
        {
            exponent = i + 1;
            break;
        }
    }

    // Extract the mantissa (3 bytes ending at exponent-1).
    uint32_t mantissa = 0;
    if (exponent <= 3)
    {
        // For small targets, just take the first 3 bytes.
        mantissa = fullRep[0] | (fullRep[1] << 8) | (fullRep[2] << 16);
        exponent = 3;
    }
    else
    {
        mantissa = fullRep[exponent - 3] |
            (fullRep[exponent - 2] << 8) |
            (fullRep[exponent - 1] << 16);
    }

    // Handle the "Sign Bit" rule:
    // If the high bit of the mantissa (0x00800000) is set, Dogecoin treats it as negative.
    // We shift right and increment exponent.
    if (mantissa & 0x00800000)
    {
        mantissa >>= 8;
        exponent++;
    }

    // Store in little-endian compactRep [m1, m2, m3, exp].
    compactRep[0] = mantissa & 0xFF;
    compactRep[1] = (mantissa >> 8) & 0xFF;
    compactRep[2] = (mantissa >> 16) & 0xFF;
    compactRep[3] = static_cast<uint8_t>(exponent);

    return compactRep;
}

std::array<uint8_t, 32> divideTarget(const std::array<uint8_t, 32>& baseTarget, uint64_t difficulty)
{
    if (difficulty <= 1) return baseTarget;

    std::array<uint8_t, 32> result = { 0 };

    // We treat the 256-bit target as 4 words of 64-bits each.
    // Index 3 is the most significant (bytes 24-31).
    // Index 0 is the least significant (bytes 0-7).
    uint64_t words[4];
    for (int i = 0; i < 4; ++i)
    {
        words[i] = 0;
        for (int j = 0; j < 8; ++j)
            words[i] |= (static_cast<uint64_t>(baseTarget[i * 8 + j]) << (8 * j));
    }

    uint64_t remainder = 0;
    uint64_t resultWords[4];

    // Perform long division from most significant word to least significant.
    for (int i = 3; i >= 0; --i)
    {
        // To divide 256-bit by 64-bit, we need 128-bit intermediate math.
#ifdef _MSC_VER
        uint64_t high = remainder;
        uint64_t low = words[i];
        uint64_t resultRemainder;
        resultWords[i] = _udiv128(high, low, difficulty, &resultRemainder);
        remainder = resultRemainder;
#else
        unsigned __int128 current = (static_cast<unsigned __int128>(remainder) << 64) | words[i];
        resultWords[i] = static_cast<uint64_t>(current / difficulty);
        remainder = static_cast<uint64_t>(current % difficulty);
#endif
    }

    // Convert the 64-bit result words back into the little-endian byte array.
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 8; ++j)
            result[i * 8 + j] = static_cast<uint8_t>((resultWords[i] >> (8 * j)) & 0xFF);

    return result;
}