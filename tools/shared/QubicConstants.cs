namespace Qubic.Doge.Shared;

/// <summary>
/// Qubic protocol constants relevant to DOGE mining.
/// </summary>
public static class QubicConstants
{
    public const int SignatureSize = 64;
    public const int PublicKeySize = 32;
    public const int NumberOfComputors = 676;
    public const int DefaultPort = 21841;

    /// <summary>Qubic packet types for custom mining messages.</summary>
    public const byte PacketTypeCustomMiningTask = 68;
    public const byte PacketTypeCustomMiningSolution = 69;
    public const byte PacketTypeBroadcastMessage = 1;
}
