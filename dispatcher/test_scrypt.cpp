#include <cstdio>
#include <cstring>
#include <cstdint>
#include "dispatcher/src/hash_util/scrypt.h"
#include "dispatcher/src/hash_util/hash_util.h"

// Convert hex string to bytes (big endian / natural order)
static void hexToBytes(const char* hex, uint8_t* out, int len) {
    for (int i = 0; i < len; i++) {
        unsigned int byte;
        sscanf(hex + 2*i, "%02x", &byte);
        out[i] = (uint8_t)byte;
    }
}

int main() {
    // Header from the stratum server log (identical to dispatcher's header)
    const char* headerHex = "00000020556cf6485aa2a035cfb23035260cda27586ad94e52b17c04a687c0047fb6bbc2d01e4539169eaf3ecf82a9da94a5b3b591a19f62c2850cecdbd29de9f50b92c68b6ec869fc342e197734bfde";

    uint8_t header[80];
    hexToBytes(headerHex, header, 80);

    printf("Header (%zu chars = %d bytes):\n", strlen(headerHex), (int)(strlen(headerHex)/2));
    for (int i = 0; i < 80; i++) printf("%02x", header[i]);
    printf("\n\n");

    uint8_t hash[32];
    scrypt_1024_1_1_256((const char*)header, (char*)hash);

    printf("Dispatcher scrypt hash (raw byte order):\n");
    for (int i = 0; i < 32; i++) printf("%02x", hash[i]);
    printf("\n\n");

    printf("Dispatcher scrypt hash (reversed/LE display):\n");
    for (int i = 31; i >= 0; i--) printf("%02x", hash[i]);
    printf("\n\n");

    // Expected hash from stratum server (displayed with trailing zeros = LE)
    printf("Expected hash (from stratum, raw byte order):\n");
    printf("c07afdbf72452d7c40fe4dcb07a33ac72008ec4ae32de6322442840a00000000\n\n");

    printf("Dispatcher hash (raw byte order from log):\n");
    printf("9b6cb76391935d1eae240ac912963c43722d3b652d7ebec361799f8e65110f0b\n\n");

    return 0;
}
