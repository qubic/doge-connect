using System.Collections.Concurrent;
using System.Net.WebSockets;

namespace Qubic.Doge.Relay;

/// <summary>
/// Tracks all connected WebSocket clients and maintains a ring buffer of recent events.
/// Thread-safe.
/// </summary>
public class ClientRegistry
{
    private readonly ConcurrentDictionary<Guid, WebSocket> _clients = new();
    private readonly Queue<byte[]> _ring;
    private readonly object _ringLock = new();
    private readonly int _ringCapacity;

    public int Count => _clients.Count;

    public ClientRegistry(IConfiguration config)
    {
        _ringCapacity = int.TryParse(config["Relay:RingBufferSize"], out var n) ? n : 100;
        _ring = new Queue<byte[]>(_ringCapacity);
    }

    public void Register(Guid id, WebSocket ws) => _clients[id] = ws;

    public void Unregister(Guid id) => _clients.TryRemove(id, out _);

    /// <summary>Broadcast a message to all connected clients. Removes clients that fail to receive.</summary>
    public async Task BroadcastAsync(byte[] payload, CancellationToken ct)
    {
        // Append to ring buffer.
        lock (_ringLock)
        {
            if (_ring.Count >= _ringCapacity)
                _ring.Dequeue();
            _ring.Enqueue(payload);
        }

        var dead = new List<Guid>();
        foreach (var kv in _clients)
        {
            var ws = kv.Value;
            if (ws.State != WebSocketState.Open)
            {
                dead.Add(kv.Key);
                continue;
            }
            try
            {
                await ws.SendAsync(payload, WebSocketMessageType.Text, true, ct);
            }
            catch
            {
                dead.Add(kv.Key);
            }
        }
        foreach (var id in dead)
            _clients.TryRemove(id, out _);
    }

    /// <summary>Snapshot of the ring buffer for new clients to get recent history.</summary>
    public byte[][] GetRecent()
    {
        lock (_ringLock)
        {
            return _ring.ToArray();
        }
    }
}
