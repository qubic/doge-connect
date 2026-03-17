#include "dispatcher_signing.h"

#include "k12_and_key_utils.h"

#include <cstring>

bool initSigningContext(const std::string& seed, DispatcherSigningContext& ctx)
{
    if (seed.size() != 55)
        return false;

    if (!getSubseedFromSeed(reinterpret_cast<const uint8_t*>(seed.c_str()), ctx.subseed.data()))
        return false;

    unsigned char privateKey[32];
    getPrivateKeyFromSubSeed(ctx.subseed.data(), privateKey);
    getPublicKeyFromPrivateKey(privateKey, ctx.publicKey.data());

    // Clear the private key from the stack.
    memset(privateKey, 0, 32);

    return true;
}

void signTaskPacket(const DispatcherSigningContext& ctx, const uint8_t* data, unsigned int dataSize, uint8_t* signatureOut)
{
    // Hash the task data to produce a 32-byte message digest.
    unsigned char digest[32];
    KangarooTwelve(data, dataSize, digest, 32);

    // Sign the digest using SchnorrQ.
    sign(ctx.subseed.data(), ctx.publicKey.data(), digest, signatureOut);
}
