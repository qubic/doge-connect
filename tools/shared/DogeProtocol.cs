using System.Buffers.Binary;
using System.Runtime.InteropServices;

namespace Qubic.Doge.Shared;

public enum CustomMiningType : byte
{
    DOGE = 0
}

/// <summary>
/// Wire header for type 68 (CustomQubicMiningTask).
/// Packed right after the 8-byte qubic packet header.
/// </summary>
[StructLayout(LayoutKind.Sequential, Pack = 1)]
public struct CustomQubicMiningTaskHeader
{
    public ulong jobId;
    public byte customMiningType;
}

/// <summary>
/// Fixed-size portion of QubicDogeMiningTask (after the 9-byte task header).
/// Variable-length data follows immediately after this struct.
/// </summary>
[StructLayout(LayoutKind.Sequential, Pack = 1)]
public struct QubicDogeMiningTaskFixed
{
    public byte cleanJobQueue;

    [MarshalAs(UnmanagedType.ByValArray, SizeConst = 4)]
    public byte[] dispatcherDifficulty;

    [MarshalAs(UnmanagedType.ByValArray, SizeConst = 4)]
    public byte[] version;

    [MarshalAs(UnmanagedType.ByValArray, SizeConst = 4)]
    public byte[] nTime;

    [MarshalAs(UnmanagedType.ByValArray, SizeConst = 4)]
    public byte[] nBits;

    [MarshalAs(UnmanagedType.ByValArray, SizeConst = 32)]
    public byte[] prevHash;

    public uint extraNonce1NumBytes;
    public uint coinbase1NumBytes;
    public uint coinbase2NumBytes;
    public uint numMerkleBranches;

    /// <summary>extraNonce2 size is a protocol constant (8 bytes), not a wire field.</summary>
    public const uint ExtraNonce2NumBytes = 8;
}

/// <summary>
/// Fully parsed DOGE mining task with variable-length fields materialized.
/// </summary>
public class DogeTaskInfo
{
    public ulong JobId { get; set; }
    public CustomMiningType MiningType { get; set; }

    public bool CleanJobQueue { get; set; }
    public byte[] DispatcherDifficulty { get; set; } = Array.Empty<byte>();
    public uint ExtraNonce2NumBytes { get; set; }
    public byte[] Version { get; set; } = Array.Empty<byte>();
    public byte[] NTime { get; set; } = Array.Empty<byte>();
    public byte[] NBits { get; set; } = Array.Empty<byte>();
    public byte[] PrevHash { get; set; } = Array.Empty<byte>();

    public byte[] ExtraNonce1 { get; set; } = Array.Empty<byte>();
    public byte[] Coinbase1 { get; set; } = Array.Empty<byte>();
    public byte[] Coinbase2 { get; set; } = Array.Empty<byte>();
    public List<byte[]> MerkleBranches { get; set; } = new();

    public byte[] Signature { get; set; } = Array.Empty<byte>();
    public bool SignatureValid { get; set; }

    public string SourcePeer { get; set; } = "";
    public DateTime ReceivedAt { get; set; }
    public int RawPacketSize { get; set; }

    public void Print()
    {
        var ts = ReceivedAt.ToString("yyyy-MM-dd HH:mm:ss");

        Console.ForegroundColor = ConsoleColor.Cyan;
        Console.WriteLine($"[{ts}] DOGE Task received from {SourcePeer}");
        Console.ResetColor();

        Console.WriteLine($"  Job ID:        {JobId}");
        Console.WriteLine($"  Type:          {MiningType} ({(byte)MiningType})");

        Console.ForegroundColor = SignatureValid ? ConsoleColor.Green : ConsoleColor.Red;
        Console.WriteLine($"  Signature:     {(SignatureValid ? "VALID" : "INVALID")}");
        Console.ResetColor();

        Console.WriteLine($"  Packet Size:   {RawPacketSize} bytes");
        Console.WriteLine($"  Clean Queue:   {(CleanJobQueue ? "Yes" : "No")}");
        Console.WriteLine($"  Difficulty:    0x{Convert.ToHexString(DispatcherDifficulty)} (compact)");
        Console.WriteLine($"  Version:       0x{Convert.ToHexString(Version)}");

        var nTimeVal = BinaryPrimitives.ReadUInt32LittleEndian(NTime);
        var nTimeDate = DateTimeOffset.FromUnixTimeSeconds(nTimeVal).UtcDateTime;
        Console.WriteLine($"  nTime:         {nTimeVal} ({nTimeDate:yyyy-MM-dd HH:mm:ss} UTC)");

        Console.WriteLine($"  nBits:         0x{Convert.ToHexString(NBits)}");
        Console.WriteLine($"  PrevHash:      {Convert.ToHexString(PrevHash)[..32]}...");
        Console.WriteLine($"  ExtraNonce1:   {ExtraNonce1.Length} bytes ({Convert.ToHexString(ExtraNonce1)})");
        Console.WriteLine($"  ExtraNonce2:   {ExtraNonce2NumBytes} bytes");
        Console.WriteLine($"  Coinbase1:     {Coinbase1.Length} bytes");
        Console.WriteLine($"  Coinbase2:     {Coinbase2.Length} bytes");
        Console.WriteLine($"  Merkle:        {MerkleBranches.Count} branches");

        if (MerkleBranches.Count > 0)
        {
            for (int i = 0; i < Math.Min(MerkleBranches.Count, 3); i++)
                Console.WriteLine($"    [{i}] {Convert.ToHexString(MerkleBranches[i])}");
            if (MerkleBranches.Count > 3)
                Console.WriteLine($"    ... and {MerkleBranches.Count - 3} more");
        }

        Console.WriteLine();
    }
}

/// <summary>
/// Wire header for type 69 (CustomQubicMiningSolution).
/// Contains the miner's sourcePublicKey for signature verification.
/// </summary>
[StructLayout(LayoutKind.Sequential, Pack = 1)]
public struct CustomQubicMiningSolutionHeader
{
    [MarshalAs(UnmanagedType.ByValArray, SizeConst = 32)]
    public byte[] sourcePublicKey;

    public ulong jobId;
    public byte customMiningType;
}

/// <summary>
/// Fixed-size portion of QubicDogeMiningSolution.
/// The 8-byte extraNonce2: compID(4, big-endian) + minerExtraNonce2(4).
/// </summary>
[StructLayout(LayoutKind.Sequential, Pack = 1)]
public struct QubicDogeMiningSolutionFixed
{
    [MarshalAs(UnmanagedType.ByValArray, SizeConst = 4)]
    public byte[] nTime;

    [MarshalAs(UnmanagedType.ByValArray, SizeConst = 4)]
    public byte[] nonce;

    [MarshalAs(UnmanagedType.ByValArray, SizeConst = 32)]
    public byte[] merkleRoot;

    [MarshalAs(UnmanagedType.ByValArray, SizeConst = 8)]
    public byte[] extraNonce2;
}

/// <summary>
/// Fully parsed DOGE mining solution.
/// </summary>
public class DogeSolutionInfo
{
    public byte[] SourcePublicKey { get; set; } = Array.Empty<byte>();
    public string SourceIdentity { get; set; } = "";
    public ulong JobId { get; set; }
    public CustomMiningType MiningType { get; set; }

    public byte[] NTime { get; set; } = Array.Empty<byte>();
    public byte[] Nonce { get; set; } = Array.Empty<byte>();
    public byte[] MerkleRoot { get; set; } = Array.Empty<byte>();
    public byte[] ExtraNonce2 { get; set; } = Array.Empty<byte>();

    public byte[] Signature { get; set; } = Array.Empty<byte>();
    public bool SignatureValid { get; set; }

    public string SourcePeer { get; set; } = "";
    public DateTime ReceivedAt { get; set; }
    public int RawPacketSize { get; set; }

    /// <summary>Extract computor index from extraNonce2 (bytes 0-3, big-endian, % 676).</summary>
    public int ComputorIndex
    {
        get
        {
            if (ExtraNonce2.Length < 4) return -1;
            uint compId = (uint)(ExtraNonce2[0] << 24 | ExtraNonce2[1] << 16 | ExtraNonce2[2] << 8 | ExtraNonce2[3]);
            return (int)(compId % QubicConstants.NumberOfComputors);
        }
    }

    public void Print()
    {
        var ts = ReceivedAt.ToString("yyyy-MM-dd HH:mm:ss");

        Console.ForegroundColor = ConsoleColor.Magenta;
        Console.WriteLine($"[{ts}] DOGE Solution received from {SourcePeer}");
        Console.ResetColor();

        Console.WriteLine($"  Job ID:        {JobId}");
        Console.WriteLine($"  Miner:         {SourceIdentity}");
        Console.WriteLine($"  Computor:      {ComputorIndex}");

        Console.ForegroundColor = SignatureValid ? ConsoleColor.Green : ConsoleColor.Red;
        Console.WriteLine($"  Signature:     {(SignatureValid ? "VALID" : "INVALID")}");
        Console.ResetColor();

        Console.WriteLine($"  Nonce:         {Convert.ToHexString(Nonce)}");
        Console.WriteLine($"  ExtraNonce2:   {Convert.ToHexString(ExtraNonce2)}");
        Console.WriteLine();
    }
}

/// <summary>
/// Parser for DOGE mining task and solution payloads from qubic packets.
/// </summary>
public static class DogeTaskParser
{
    public static DogeTaskInfo? Parse(byte[] payload, int headerOffset = 0)
    {
        var taskHeaderSize = Marshal.SizeOf<CustomQubicMiningTaskHeader>();
        var fixedSize = Marshal.SizeOf<QubicDogeMiningTaskFixed>();

        if (payload.Length < headerOffset + taskHeaderSize + fixedSize + QubicConstants.SignatureSize)
            return null;

        var taskHeaderBytes = payload.AsSpan(headerOffset, taskHeaderSize).ToArray();
        var taskHeader = Marshalling.Deserialize<CustomQubicMiningTaskHeader>(taskHeaderBytes);

        var fixedBytes = payload.AsSpan(headerOffset + taskHeaderSize, fixedSize).ToArray();
        var fixedTask = Marshalling.Deserialize<QubicDogeMiningTaskFixed>(fixedBytes);

        var info = new DogeTaskInfo
        {
            JobId = taskHeader.jobId,
            MiningType = (CustomMiningType)taskHeader.customMiningType,
            CleanJobQueue = fixedTask.cleanJobQueue != 0,
            DispatcherDifficulty = fixedTask.dispatcherDifficulty ?? new byte[4],
            ExtraNonce2NumBytes = QubicDogeMiningTaskFixed.ExtraNonce2NumBytes,
            Version = fixedTask.version ?? new byte[4],
            NTime = fixedTask.nTime ?? new byte[4],
            NBits = fixedTask.nBits ?? new byte[4],
            PrevHash = fixedTask.prevHash ?? new byte[32],
            RawPacketSize = payload.Length
        };

        var offset = headerOffset + taskHeaderSize + fixedSize;
        var sigStart = payload.Length - QubicConstants.SignatureSize;

        try
        {
            if (fixedTask.extraNonce1NumBytes > 0 && offset + fixedTask.extraNonce1NumBytes <= sigStart)
            {
                info.ExtraNonce1 = payload.AsSpan(offset, (int)fixedTask.extraNonce1NumBytes).ToArray();
                offset += (int)fixedTask.extraNonce1NumBytes;
            }

            if (fixedTask.coinbase1NumBytes > 0 && offset + fixedTask.coinbase1NumBytes <= sigStart)
            {
                info.Coinbase1 = payload.AsSpan(offset, (int)fixedTask.coinbase1NumBytes).ToArray();
                offset += (int)fixedTask.coinbase1NumBytes;
            }

            if (fixedTask.coinbase2NumBytes > 0 && offset + fixedTask.coinbase2NumBytes <= sigStart)
            {
                info.Coinbase2 = payload.AsSpan(offset, (int)fixedTask.coinbase2NumBytes).ToArray();
                offset += (int)fixedTask.coinbase2NumBytes;
            }

            int branchSizesLen = (int)fixedTask.numMerkleBranches * 4;
            offset += branchSizesLen;

            for (uint i = 0; i < fixedTask.numMerkleBranches && offset + 32 <= sigStart; i++)
            {
                info.MerkleBranches.Add(payload.AsSpan(offset, 32).ToArray());
                offset += 32;
            }
        }
        catch
        {
            // partial parse is fine for monitoring
        }

        info.Signature = payload.AsSpan(sigStart, QubicConstants.SignatureSize).ToArray();
        return info;
    }

    public static DogeSolutionInfo? ParseSolution(byte[] payload, int headerOffset = 0)
    {
        var solHeaderSize = Marshal.SizeOf<CustomQubicMiningSolutionHeader>();
        var fixedSize = Marshal.SizeOf<QubicDogeMiningSolutionFixed>();

        if (payload.Length < headerOffset + solHeaderSize + fixedSize + QubicConstants.SignatureSize)
            return null;

        var solHeaderBytes = payload.AsSpan(headerOffset, solHeaderSize).ToArray();
        var solHeader = Marshalling.Deserialize<CustomQubicMiningSolutionHeader>(solHeaderBytes);

        var fixedBytes = payload.AsSpan(headerOffset + solHeaderSize, fixedSize).ToArray();
        var fixedSol = Marshalling.Deserialize<QubicDogeMiningSolutionFixed>(fixedBytes);

        var info = new DogeSolutionInfo
        {
            SourcePublicKey = solHeader.sourcePublicKey ?? new byte[32],
            JobId = solHeader.jobId,
            MiningType = (CustomMiningType)solHeader.customMiningType,
            NTime = fixedSol.nTime ?? new byte[4],
            Nonce = fixedSol.nonce ?? new byte[4],
            MerkleRoot = fixedSol.merkleRoot ?? new byte[32],
            ExtraNonce2 = fixedSol.extraNonce2 ?? new byte[8],
            RawPacketSize = payload.Length
        };

        var sigStart = headerOffset + solHeaderSize + fixedSize;
        info.Signature = payload.AsSpan(sigStart, QubicConstants.SignatureSize).ToArray();

        return info;
    }
}
