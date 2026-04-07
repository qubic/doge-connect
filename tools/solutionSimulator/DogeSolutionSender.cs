using System.Runtime.InteropServices;
using Qubic.Crypto;
using Qubic.Doge.Shared;
using Qubic.Network;
using Qubic.Serialization;

namespace Qubic.Doge.SolutionSimulator;

/// <summary>
/// Connects to a Qubic node and sends demo DOGE solution packets (type 69).
/// Uses Qubic.NET for network communication and signing.
/// </summary>
public class DogeSolutionSender : IDisposable
{
    private readonly QubicStreamClient _client;
    private readonly string _ip;
    private readonly int _port;
    private readonly string _seed;
    private readonly byte[] _senderPublicKey;
    private readonly QubicCrypt _crypt = new();

    public DogeSolutionSender(string ip, int port, string seed, byte[] senderPublicKey)
    {
        _ip = ip;
        _port = port;
        _seed = seed;
        _senderPublicKey = senderPublicKey;

        _client = new QubicStreamClient(new QubicStreamOptions
        {
            Nodes = [$"{ip}:{port}"],
        });

        _client.PacketReceived += (_, e) =>
        {
            Log($"Received packet type={e.Header.Type} size={e.Header.PacketSize} from {_ip}", ConsoleColor.DarkGray);
        };
    }

    public async Task<bool> ConnectAsync(CancellationToken ct = default)
    {
        Log($"Connecting to {_ip}:{_port}...", ConsoleColor.Gray);
        try
        {
            await _client.ConnectAsync(ct);
            Log($"Connected to {_ip}:{_port}", ConsoleColor.Green);
            return _client.State == QubicStreamConnectionState.Connected;
        }
        catch (Exception ex)
        {
            Log($"Failed to connect to {_ip}: {ex.Message}", ConsoleColor.Red);
            return false;
        }
    }

    public bool IsConnected => _client.State == QubicStreamConnectionState.Connected;

    /// <summary>
    /// Build and send a demo DOGE solution packet (type 69).
    /// </summary>
    public async Task SendDemoSolutionAsync(ulong jobId = 0, CancellationToken ct = default)
    {
        if (!IsConnected)
        {
            Log("Not connected, cannot send.", ConsoleColor.Red);
            return;
        }

        if (jobId == 0)
            jobId = (ulong)DateTimeOffset.UtcNow.ToUnixTimeSeconds();

        // Build the solution payload (without header, without signature)
        var payload = BuildDemoSolutionPayload(jobId);

        // Sign: KangarooTwelve(payload) → digest → SchnorrQ sign
        var signature = _crypt.SignRaw(_seed, payload);
        Log($"Signature: {Convert.ToHexString(signature)[..32]}... ({signature.Length} bytes)", ConsoleColor.DarkCyan);

        // Combine: payload + signature
        var fullPayload = new byte[payload.Length + signature.Length];
        Buffer.BlockCopy(payload, 0, fullPayload, 0, payload.Length);
        Buffer.BlockCopy(signature, 0, fullPayload, payload.Length, signature.Length);

        // Build the packet: 8-byte header + fullPayload
        // Header format: size[3 bytes LE] + type[1 byte] + dejavu[4 bytes]
        var totalSize = QubicPacketHeader.Size + fullPayload.Length;
        var packet = new byte[totalSize];
        packet[0] = (byte)(totalSize & 0xFF);
        packet[1] = (byte)((totalSize >> 8) & 0xFF);
        packet[2] = (byte)((totalSize >> 16) & 0xFF);
        packet[3] = QubicConstants.PacketTypeCustomMiningSolution;
        // dejavu bytes 4-7 = 0 (broadcast)
        Buffer.BlockCopy(fullPayload, 0, packet, QubicPacketHeader.Size, fullPayload.Length);

        Log($"Sending demo solution: {packet.Length} bytes (payload={payload.Length}, sig={signature.Length})", ConsoleColor.Cyan);
        PrintDemoSolution(jobId);

        await _client.SendRawPacketAsync(packet, ct);
        Log($"Demo solution sent to {_ip}!", ConsoleColor.Green);
    }

    private byte[] BuildDemoSolutionPayload(ulong jobId)
    {
        var solHeaderSize = Marshal.SizeOf<CustomQubicMiningSolutionHeader>();
        var fixedSize = Marshal.SizeOf<QubicDogeMiningSolutionFixed>();

        var demoNTime = new byte[] { 0x01, 0x02, 0x03, 0x04 };
        var demoNonce = new byte[] { 0xDE, 0xAD, 0xBE, 0xEF };
        var demoMerkleRoot = new byte[32];
        for (int i = 0; i < 32; i++)
            demoMerkleRoot[i] = (byte)(0xAA + (i % 16));
        var demoExtraNonce2 = new byte[8];
        demoExtraNonce2[0] = 0x01; demoExtraNonce2[1] = 0x02;
        demoExtraNonce2[2] = 0x03; demoExtraNonce2[3] = 0x04;

        var totalSize = solHeaderSize + fixedSize;
        var payload = new byte[totalSize];

        var solHeader = new CustomQubicMiningSolutionHeader
        {
            sourcePublicKey = _senderPublicKey,
            jobId = jobId,
            customMiningType = (byte)CustomMiningType.DOGE
        };
        var solHeaderBytes = Marshalling.Serialize(solHeader);
        Buffer.BlockCopy(solHeaderBytes, 0, payload, 0, solHeaderBytes.Length);

        var fixedSol = new QubicDogeMiningSolutionFixed
        {
            nTime = demoNTime,
            nonce = demoNonce,
            merkleRoot = demoMerkleRoot,
            extraNonce2 = demoExtraNonce2
        };
        var fixedBytes = Marshalling.Serialize(fixedSol);
        Buffer.BlockCopy(fixedBytes, 0, payload, solHeaderBytes.Length, fixedBytes.Length);

        return payload;
    }

    private void PrintDemoSolution(ulong jobId)
    {
        var identity = _crypt.GetIdentityFromPublicKey(_senderPublicKey);
        Console.WriteLine($"  Job ID:        {jobId}");
        Console.WriteLine($"  Type:          DOGE (0)");
        Console.WriteLine($"  Sender:        {identity}");
        Console.WriteLine($"  Nonce:         DEADBEEF");
        Console.WriteLine($"  ExtraNonce2:   01020304 00000000");
        Console.WriteLine();
    }

    public void Dispose()
    {
        _client.Dispose();
    }

    private static void Log(string message, ConsoleColor color = ConsoleColor.White)
    {
        var ts = DateTime.UtcNow.ToString("yyyy-MM-dd HH:mm:ss");
        Console.ForegroundColor = color;
        Console.WriteLine($"[{ts}] {message}");
        Console.ResetColor();
    }
}
