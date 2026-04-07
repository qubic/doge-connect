using System.Security.Cryptography;

namespace Qubic.Doge.NetBridge;

/// <summary>
/// Fixed-size ring buffer of packet hashes to prevent re-relaying the same packet.
/// Thread-safe.
/// </summary>
public class PacketDeduplicator
{
    private readonly HashSet<long> _seen;
    private readonly Queue<long> _order;
    private readonly int _capacity;
    private readonly object _lock = new();

    public PacketDeduplicator(int capacity = 2000)
    {
        _capacity = capacity;
        _seen = new HashSet<long>(capacity);
        _order = new Queue<long>(capacity);
    }

    /// <summary>
    /// Returns true if this packet was already seen (duplicate). Otherwise marks it as seen.
    /// Uses a fast hash of the payload (skipping the 4-byte dejavu field which we zero out).
    /// </summary>
    public bool IsDuplicate(byte[] rawPacket)
    {
        // Hash the packet payload (bytes 8+), excluding the dejavu field which varies per relay.
        // Using a simple 64-bit hash for speed.
        var hash = ComputeHash(rawPacket);

        lock (_lock)
        {
            if (_seen.Contains(hash))
                return true;

            _seen.Add(hash);
            _order.Enqueue(hash);

            while (_order.Count > _capacity)
            {
                var old = _order.Dequeue();
                _seen.Remove(old);
            }

            return false;
        }
    }

    private static long ComputeHash(byte[] data)
    {
        // Fast non-crypto hash: XOR-fold SHA256 to 64 bits.
        // Only hash from byte 8 onward (skip header with dejavu).
        var start = Math.Min(8, data.Length);
        var span = data.AsSpan(start);
        Span<byte> sha = stackalloc byte[32];
        SHA256.HashData(span, sha);
        return BitConverter.ToInt64(sha) ^ BitConverter.ToInt64(sha.Slice(8))
             ^ BitConverter.ToInt64(sha.Slice(16)) ^ BitConverter.ToInt64(sha.Slice(24));
    }
}
