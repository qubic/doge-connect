using Qubic.Doge.Shared;
using Qubic.Network;
using Qubic.Serialization;

namespace Qubic.Doge.NetBridge;

/// <summary>
/// One-way bridge: relays DOGE mining tasks (type 68) and solutions (type 69)
/// from mainnet peers to testnet peers. Packets are forwarded raw with dejavu zeroed.
/// </summary>
public class BridgeRelay : IDisposable
{
    private readonly List<QubicStreamClient> _mainnetClients = new();
    private readonly List<QubicStreamClient> _testnetClients = new();
    private readonly PacketDeduplicator _dedup;

    private long _tasksRelayed;
    private long _solutionsRelayed;
    private long _duplicatesSkipped;
    private long _packetsReceived;

    public long TasksRelayed => _tasksRelayed;
    public long SolutionsRelayed => _solutionsRelayed;
    public long DuplicatesSkipped => _duplicatesSkipped;
    public long PacketsReceived => _packetsReceived;

    public BridgeRelay(
        IReadOnlyList<string> mainnetPeers, int mainnetPort,
        IReadOnlyList<string> testnetPeers, int testnetPort,
        int deduplicationWindow = 2000)
    {
        _dedup = new PacketDeduplicator(deduplicationWindow);

        // One client per mainnet peer (source).
        foreach (var peer in mainnetPeers)
        {
            var client = new QubicStreamClient(new QubicStreamOptions
            {
                Nodes = [$"{peer}:{mainnetPort}"],
            });
            client.PacketReceived += OnMainnetPacketReceived;
            _mainnetClients.Add(client);
        }

        // One client per testnet peer (destination).
        foreach (var peer in testnetPeers)
        {
            var client = new QubicStreamClient(new QubicStreamOptions
            {
                Nodes = [$"{peer}:{testnetPort}"],
            });
            _testnetClients.Add(client);
        }
    }

    public async Task StartAsync(CancellationToken ct)
    {
        // Connect all clients in parallel.
        var tasks = new List<Task>();

        foreach (var client in _mainnetClients)
            tasks.Add(ConnectWithRetry(client, "mainnet", ct));

        foreach (var client in _testnetClients)
            tasks.Add(ConnectWithRetry(client, "testnet", ct));

        await Task.WhenAll(tasks);

        var mainConnected = _mainnetClients.Count(c => c.State == QubicStreamConnectionState.Connected);
        var testConnected = _testnetClients.Count(c => c.State == QubicStreamConnectionState.Connected);
        Log($"Connected: {mainConnected}/{_mainnetClients.Count} mainnet, {testConnected}/{_testnetClients.Count} testnet", ConsoleColor.Green);
    }

    private async Task ConnectWithRetry(QubicStreamClient client, string label, CancellationToken ct)
    {
        try
        {
            await client.ConnectAsync(ct);
            Log($"  [{label}] {client.ActiveNodeEndpoint} connected", ConsoleColor.Green);
        }
        catch (Exception ex)
        {
            Log($"  [{label}] {client.ActiveNodeEndpoint} failed: {ex.Message}", ConsoleColor.Red);
        }
    }

    private void OnMainnetPacketReceived(object? sender, QubicPacketReceivedEventArgs e)
    {
        Interlocked.Increment(ref _packetsReceived);

        // Only relay DOGE mining packets.
        if (e.Header.Type != QubicConstants.PacketTypeCustomMiningTask &&
            e.Header.Type != QubicConstants.PacketTypeCustomMiningSolution)
            return;

        var rawPacket = e.RawPacket;

        // Deduplication check.
        if (_dedup.IsDuplicate(rawPacket))
        {
            Interlocked.Increment(ref _duplicatesSkipped);
            return;
        }

        // Zero the dejavu field (bytes 4-7) so the testnet gossip layer will propagate.
        var relayPacket = new byte[rawPacket.Length];
        Buffer.BlockCopy(rawPacket, 0, relayPacket, 0, rawPacket.Length);
        relayPacket[4] = 0;
        relayPacket[5] = 0;
        relayPacket[6] = 0;
        relayPacket[7] = 0;

        // Track stats.
        var isTask = e.Header.Type == QubicConstants.PacketTypeCustomMiningTask;
        if (isTask)
            Interlocked.Increment(ref _tasksRelayed);
        else
            Interlocked.Increment(ref _solutionsRelayed);

        // Forward to all connected testnet peers.
        foreach (var client in _testnetClients)
        {
            if (client.State != QubicStreamConnectionState.Connected)
                continue;
            try
            {
                _ = client.SendRawPacketAsync(relayPacket, CancellationToken.None);
            }
            catch
            {
                // fire-and-forget, don't let send failures block the receive loop
            }
        }
    }

    public void PrintStats()
    {
        var mainConn = _mainnetClients.Count(c => c.State == QubicStreamConnectionState.Connected);
        var testConn = _testnetClients.Count(c => c.State == QubicStreamConnectionState.Connected);
        Log($"[stats] mainnet: {mainConn}/{_mainnetClients.Count} | testnet: {testConn}/{_testnetClients.Count} | " +
            $"packets: {_packetsReceived} | tasks: {_tasksRelayed} | solutions: {_solutionsRelayed} | " +
            $"dedup: {_duplicatesSkipped}", ConsoleColor.Gray);
    }

    public void Dispose()
    {
        foreach (var c in _mainnetClients) c.Dispose();
        foreach (var c in _testnetClients) c.Dispose();
    }

    private static void Log(string message, ConsoleColor color = ConsoleColor.White)
    {
        var ts = DateTime.UtcNow.ToString("yyyy-MM-dd HH:mm:ss");
        Console.ForegroundColor = color;
        Console.WriteLine($"[{ts}] {message}");
        Console.ResetColor();
    }
}
