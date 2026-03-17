#include <iostream>
#include <cstring>
#include <array>
#include <vector>
#include <cstdint>

#include "dispatcher_signing.h"
#include "k12_and_key_utils.h"

static std::string bytesToHex(const uint8_t* data, size_t len)
{
    static const char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(len * 2);
    for (size_t i = 0; i < len; ++i)
    {
        result.push_back(hex[data[i] >> 4]);
        result.push_back(hex[data[i] & 0x0f]);
    }
    return result;
}

bool testSignAndVerify(const std::string& seed, const uint8_t* data, unsigned int dataSize, const std::string& testName)
{
    std::cout << "--- " << testName << " ---" << std::endl;

    // Init signing context.
    DispatcherSigningContext ctx;
    if (!initSigningContext(seed, ctx))
    {
        std::cerr << "FAIL: initSigningContext failed." << std::endl;
        return false;
    }
    std::cout << "Public key: " << bytesToHex(ctx.publicKey.data(), 32) << std::endl;

    // Sign.
    std::array<uint8_t, 64> signature;
    signTaskPacket(ctx, data, dataSize, signature.data());
    std::cout << "Signature:  " << bytesToHex(signature.data(), 64) << std::endl;

    // Compute the same digest that signTaskPacket uses internally.
    unsigned char digest[32];
    KangarooTwelve(data, dataSize, digest, 32);

    // Verify with the qubic verify() function.
    bool valid = verify(ctx.publicKey.data(), digest, signature.data());
    std::cout << "Verify:     " << (valid ? "PASS" : "FAIL") << std::endl;

    // Verify with wrong public key should fail.
    std::array<uint8_t, 32> wrongKey{};
    wrongKey[0] = ctx.publicKey[0] ^ 0x01;
    for (int i = 1; i < 32; ++i) wrongKey[i] = ctx.publicKey[i];
    bool wrongKeyResult = verify(wrongKey.data(), digest, signature.data());
    std::cout << "Wrong key:  " << (wrongKeyResult ? "FAIL (should not verify)" : "PASS (correctly rejected)") << std::endl;

    // Verify with tampered data should fail.
    unsigned char tamperedDigest[32];
    memcpy(tamperedDigest, digest, 32);
    tamperedDigest[0] ^= 0x01;
    bool tamperedResult = verify(ctx.publicKey.data(), tamperedDigest, signature.data());
    std::cout << "Tampered:   " << (tamperedResult ? "FAIL (should not verify)" : "PASS (correctly rejected)") << std::endl;

    return valid && !wrongKeyResult && !tamperedResult;
}

int main()
{
    bool allPassed = true;

    // Test 1: Simple known seed with small data.
    {
        const std::string seed = "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabc";
        std::vector<uint8_t> data = { 0x01, 0x02, 0x03, 0x04, 0x05 };
        allPassed &= testSignAndVerify(seed, data.data(), data.size(), "Small data");
    }

    // Test 2: All-a seed with larger payload (simulates a task packet).
    {
        const std::string seed = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
        std::vector<uint8_t> data(256);
        for (int i = 0; i < 256; ++i)
            data[i] = static_cast<uint8_t>(i);
        allPassed &= testSignAndVerify(seed, data.data(), data.size(), "256-byte payload");
    }

    // Test 3: Signing same data twice should produce the same signature (deterministic).
    {
        const std::string seed = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
        std::vector<uint8_t> data = { 0xDE, 0xAD, 0xBE, 0xEF };

        DispatcherSigningContext ctx;
        initSigningContext(seed, ctx);

        std::array<uint8_t, 64> sig1, sig2;
        signTaskPacket(ctx, data.data(), data.size(), sig1.data());
        signTaskPacket(ctx, data.data(), data.size(), sig2.data());

        bool deterministic = (sig1 == sig2);
        std::cout << "--- Deterministic signing ---" << std::endl;
        std::cout << "Same sig:   " << (deterministic ? "PASS" : "FAIL") << std::endl;
        allPassed &= deterministic;
    }

    std::cout << std::endl;
    std::cout << (allPassed ? "ALL TESTS PASSED" : "SOME TESTS FAILED") << std::endl;

    return allPassed ? 0 : 1;
}
