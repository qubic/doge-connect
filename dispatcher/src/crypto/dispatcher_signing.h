#pragma once

#include <array>
#include <string>
#include <cstdint>

/**
 * @brief Holds the derived key material for signing dispatcher task packets.
 * Derived once at startup from the 55-character seed.
 */
struct DispatcherSigningContext
{
    std::array<uint8_t, 32> subseed;
    std::array<uint8_t, 32> publicKey;
};

/**
 * @brief Initialize a signing context from a 55-character lowercase seed.
 * Derives: seed -> subseed -> privateKey -> publicKey.
 * @param seed The 55-character seed string (a-z only).
 * @param ctx Output signing context.
 * @return True if the seed was valid and key derivation succeeded.
 */
bool initSigningContext(const std::string& seed, DispatcherSigningContext& ctx);

/**
 * @brief Sign a task packet using the dispatcher's identity.
 * Hashes the data with KangarooTwelve to produce a 32-byte digest, then signs with SchnorrQ.
 * @param ctx The signing context (subseed + publicKey).
 * @param data Pointer to the data to sign (everything between the header and the signature).
 * @param dataSize Size of the data in bytes.
 * @param signatureOut Output buffer for the 64-byte signature.
 */
void signTaskPacket(const DispatcherSigningContext& ctx, const uint8_t* data, unsigned int dataSize, uint8_t* signatureOut);
