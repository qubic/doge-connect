#!/usr/bin/env python3
"""
Replay the DOGE oracle validation with the exact data from the dispatcher log.
Uses the same validation logic as the oracle: scrypt(header) <= target.
"""

import hashlib
import struct

# ============================================================
# DATA FROM DISPATCHER LOG
# ============================================================
# scrypt input (80 bytes = block header):
scrypt_input_hex = "04006200ea3b3d202568269564db2dbe3f75e57cf36cdb071a915e7dc5f41072832bad39faad5f092f81a998682df02f63ecd13b049c9a5b7ae8c5388bc68227058c4f56726dd669615f57198ad63189"

# scrypt output (32 bytes = hash):
scrypt_output_hex = "9c942c96e09b88559ba9d374670eed06cee26852edd2fe8f901d140000000000"

# Target from oracle display:
oracle_target_hex = "00000000000000000000000000000000000000000000000041f1290000000000"

# ============================================================
# PARSE
# ============================================================
header_bytes = bytes.fromhex(scrypt_input_hex)
hash_bytes = bytes.fromhex(scrypt_output_hex)
target_bytes = bytes.fromhex(oracle_target_hex)

print("=== DOGE Oracle Validation Replay ===")
print()

# Parse the 80-byte header into fields
version = header_bytes[0:4]
prev_hash = header_bytes[4:36]
merkle_root = header_bytes[36:68]
n_time = header_bytes[68:72]
n_bits = header_bytes[72:76]
nonce = header_bytes[76:80]

print(f"Block Header ({len(header_bytes)} bytes):")
print(f"  Version:     {version.hex()}")
print(f"  PrevHash:    {prev_hash.hex()}")
print(f"  MerkleRoot:  {merkle_root.hex()}")
print(f"  nTime:       {n_time.hex()} ({struct.unpack('<I', n_time)[0]})")
print(f"  nBits:       {n_bits.hex()}")
print(f"  Nonce:       {nonce.hex()} ({struct.unpack('<I', nonce)[0]})")
print()

# ============================================================
# VERIFY: hash <= target (byte-by-byte from MSB = index 31)
# ============================================================
# The oracle uses little-endian arrays: byte[31] is MSB
print(f"Hash (LE):   {hash_bytes.hex()}")
print(f"Target (LE): {target_bytes.hex()}")
print()

# Oracle's verifyHashVsTarget: compare from byte[31] down to byte[0]
is_valid = True
for i in range(31, -1, -1):
    h = hash_bytes[i]
    t = target_bytes[i]
    if h < t:
        print(f"  byte[{i:2d}]: hash=0x{h:02x} < target=0x{t:02x} → hash is SMALLER → VALID")
        break
    elif h > t:
        print(f"  byte[{i:2d}]: hash=0x{h:02x} > target=0x{t:02x} → hash is LARGER → INVALID")
        is_valid = False
        break
    else:
        if i > 20:  # only print interesting bytes
            print(f"  byte[{i:2d}]: hash=0x{h:02x} == target=0x{t:02x} → equal, continue")

print()
print(f"Oracle result: {'VALID' if is_valid else 'INVALID'}")
print()

# ============================================================
# CROSS-CHECK: What target does the dispatcher use?
# ============================================================
# The dispatcher uses 0x1f00ffff as base. With the pool difficulty,
# the target is computed differently than nBits.
# Let's compute the target from nBits to see if it matches the oracle's target.
nbits_val = struct.unpack('<I', n_bits)[0]
# nBits compact format: exponent = byte[3], mantissa = bytes[0:3]
exponent = (nbits_val >> 24) & 0xFF
mantissa = nbits_val & 0x00FFFFFF
if exponent <= 3:
    network_target = mantissa >> (8 * (3 - exponent))
else:
    network_target = mantissa << (8 * (exponent - 3))

print(f"nBits:          0x{nbits_val:08x}")
print(f"  exponent:     0x{exponent:02x} ({exponent})")
print(f"  mantissa:     0x{mantissa:06x}")
print(f"  network target: {network_target:064x}")

# Convert to LE bytes for comparison
network_target_bytes = network_target.to_bytes(32, byteorder='little')
print(f"  as LE bytes:    {network_target_bytes.hex()}")
print()

# Compare with oracle target
print(f"Oracle target:    {target_bytes.hex()}")
print(f"Network target:   {network_target_bytes.hex()}")
print(f"Targets match:    {target_bytes == network_target_bytes}")
print()

if target_bytes != network_target_bytes:
    print("*** TARGET MISMATCH — this could explain oracle rejection!")
    print()
    # Show the dispatcher's pool difficulty target
    # The dispatcher computes: base_target(0x1f00ffff) / pool_difficulty
    scrypt_diff1 = 0x0000FFFF << (8 * (0x1F - 3))
    print(f"Scrypt diff 1 target (0x1f00ffff): {scrypt_diff1:064x}")

    # What difficulty would produce the oracle target?
    oracle_target_int = int.from_bytes(target_bytes, byteorder='little')
    if oracle_target_int > 0:
        implied_diff = scrypt_diff1 // oracle_target_int
        print(f"Oracle target implies diff: {implied_diff}")

    # What difficulty would produce the network target?
    network_target_int = int.from_bytes(network_target_bytes, byteorder='little')
    if network_target_int > 0:
        net_diff = scrypt_diff1 // network_target_int
        print(f"Network nBits implies diff: {net_diff}")

# ============================================================
# Check: does the hash pass the NETWORK target (nBits)?
# ============================================================
print()
hash_passes_network = True
for i in range(31, -1, -1):
    h = hash_bytes[i]
    t = network_target_bytes[i]
    if h < t:
        hash_passes_network = True
        break
    elif h > t:
        hash_passes_network = False
        break

print(f"Hash passes oracle target:  {is_valid}")
print(f"Hash passes network target: {hash_passes_network}")
