using Qubic.Crypto;
using Qubic.Doge.Shared;
using Qubic.Network;
using Qubic.Serialization;

namespace Qubic.Doge.Monitor;

/// <summary>
/// Connects to a single Qubic node and monitors for DOGE mining tasks and solutions.
/// Uses Qubic.NET's QubicStreamClient for network communication.
/// </summary>
public class DogeNodeConnection : IDisposable
{
    private readonly QubicStreamClient _client;
    private readonly string _ip;
    private readonly int _port;
    private readonly QubicCrypt _crypt = new();
    private readonly byte[] _dispatcherPublicKey;

    public Action<DogeTaskInfo>? OnDogeTaskReceived;
    public Action<DogeSolutionInfo>? OnDogeSolutionReceived;

    private DateTime _lastPacketReceived = DateTime.UtcNow;
    private int _taskCount;
    private int _solutionCount;
    private int _packetCount;

    public string IpAddress => _ip;

    public DogeNodeConnection(string ip, byte[] dispatcherPublicKey, int port = 21841)
    {
        _ip = ip;
        _port = port;
        _dispatcherPublicKey = dispatcherPublicKey;

        _client = new QubicStreamClient(new QubicStreamOptions
        {
            Nodes = [$"{ip}:{port}"],
        });

        _client.PacketReceived += OnPacketReceived;
    }

    public async Task StartAsync(CancellationToken ct)
    {
        Log($"Connecting to {_ip}:{_port}...", ConsoleColor.Gray);
        try
        {
            await _client.ConnectAsync(ct);
            Log($"Connected to {_ip}:{_port}", ConsoleColor.Green);
        }
        catch (Exception ex)
        {
            Log($"Failed to connect to {_ip}: {ex.Message}", ConsoleColor.Red);
        }
    }

    private void OnPacketReceived(object? sender, QubicPacketReceivedEventArgs e)
    {
        _lastPacketReceived = DateTime.UtcNow;
        _packetCount++;

        switch (e.Header.Type)
        {
            case QubicConstants.PacketTypeCustomMiningTask:
                ProcessDogeTask(e);
                break;
            case QubicConstants.PacketTypeCustomMiningSolution:
                ProcessDogeSolution(e);
                break;
        }
    }

    private void ProcessDogeTask(QubicPacketReceivedEventArgs e)
    {
        var payload = e.Payload.ToArray();
        if (payload.Length < 10)
            return;

        var task = DogeTaskParser.Parse(payload);
        if (task == null)
        {
            Log($"Failed to parse DOGE task (size={payload.Length})", ConsoleColor.Red);
            return;
        }

        var messageToVerify = payload.AsSpan(0, payload.Length - QubicConstants.SignatureSize).ToArray();
        task.SignatureValid = _crypt.Verify(_dispatcherPublicKey, messageToVerify, task.Signature);
        task.SourcePeer = _ip;
        task.ReceivedAt = DateTime.UtcNow;
        _taskCount++;

        task.Print();
        OnDogeTaskReceived?.Invoke(task);
    }

    private void ProcessDogeSolution(QubicPacketReceivedEventArgs e)
    {
        var payload = e.Payload.ToArray();
        if (payload.Length < 41)
            return;

        var solution = DogeTaskParser.ParseSolution(payload);
        if (solution == null)
        {
            Log($"Failed to parse DOGE solution (size={payload.Length})", ConsoleColor.Red);
            return;
        }

        var messageToVerify = payload.AsSpan(0, payload.Length - QubicConstants.SignatureSize).ToArray();
        solution.SignatureValid = _crypt.Verify(solution.SourcePublicKey, messageToVerify, solution.Signature);
        solution.SourceIdentity = _crypt.GetIdentityFromPublicKey(solution.SourcePublicKey);
        solution.SourcePeer = _ip;
        solution.ReceivedAt = DateTime.UtcNow;
        _solutionCount++;

        solution.Print();
        OnDogeSolutionReceived?.Invoke(solution);
    }

    public void PrintStats()
    {
        var connected = _client.State == QubicStreamConnectionState.Connected;
        var status = connected ? "CONNECTED" : "DISCONNECTED";
        var color = connected ? ConsoleColor.Green : ConsoleColor.Red;
        Log($"[{_ip}] {status} | Packets: {_packetCount} | Tasks: {_taskCount} | Solutions: {_solutionCount} | Last: {_lastPacketReceived:HH:mm:ss}", color);
    }

    public void Dispose()
    {
        _client.Dispose();
        OnDogeTaskReceived = null;
        OnDogeSolutionReceived = null;
    }

    private static void Log(string message, ConsoleColor color = ConsoleColor.White)
    {
        var ts = DateTime.UtcNow.ToString("yyyy-MM-dd HH:mm:ss");
        Console.ForegroundColor = color;
        Console.WriteLine($"[{ts}] {message}");
        Console.ResetColor();
    }
}
