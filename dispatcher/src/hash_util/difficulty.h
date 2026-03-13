#pragma once

#include <array>
#include <cstdint>

/**
 * @brief Calculate the compact 4-byte representation from the full 32-byte representation. This conversion is lossy.
 * @param fullRep Full 32-byte representation of a 256-bit number (little endian).
 * @return 4-byte compact representation for the input number (bytes 0-2 mantissa in little endian, byte 3 exponent).
 */
std::array<uint8_t, 4> calculateCompactRepFromFullRep(const std::array<uint8_t, 32>& fullRep);

/**
 * @brief Calculate the full 32-byte representation from the compact 4-byte representation.
 * @param compactRep Compact 4-byte representation of a 256-bit number (bytes 0-2 mantissa in little endian, byte 3 exponent).
 * @return 32-byte full representation for the input number (little endian).
 */
std::array<uint8_t, 32> calculateFullRepFromCompactRep(const std::array<uint8_t, 4>& compactRep);

/**
 * @brief Divide the 256-bit base target by a positive integer difficulty.
 * @param baseTarget The 256-bit dividend number (little endian).
 * @param difficulty The divisor.
 * @return The resulting quotient of the division (little endian).
 */
std::array<uint8_t, 32> divideTarget(const std::array<uint8_t, 32>& baseTarget, uint64_t difficulty);

/**
 * @brief 
 * A class to handle the compact and full representation of network difficulty
 * represented by the target number. All byte-arrays are treated as little-endian:
 *
 *   m_compactRep[0, 1, 2] = mantissa (least significant to most significant byte)
 *   m_compactRep[3] = exponent
 *
 * Note that the conversion from the full representation to the compact representation
 * is lossy because we only have 3 bytes for the mantissa.
 */
class DifficultyTarget
{
public:
    DifficultyTarget() = delete;
    DifficultyTarget(std::array<uint8_t, 4> compactRep)
    {
        m_compactRep = std::move(compactRep);
        m_fullRep = calculateFullRepFromCompactRep(m_compactRep);
    }

    DifficultyTarget(std::array<uint8_t, 32> fullRep)
    {
        m_fullRep = std::move(fullRep);
        m_compactRep = calculateCompactRepFromFullRep(m_fullRep);
    }

    const std::array<uint8_t, 4>& getCompactRep() const
    {
        return m_compactRep;
    }

    std::array<uint8_t, 32> getFullRep() const
    {
        return m_fullRep;
    }

    void div(uint64_t divisor)
    {
        m_fullRep = divideTarget(m_fullRep, divisor);
        m_compactRep = calculateCompactRepFromFullRep(m_fullRep);
    }

private:
    std::array<uint8_t, 4> m_compactRep;
    std::array<uint8_t, 32> m_fullRep;
};