using System.Runtime.InteropServices;

namespace Qubic.Doge.Shared;

/// <summary>
/// Simple struct ↔ byte[] marshalling using StructLayout.
/// </summary>
public static class Marshalling
{
    public static T Deserialize<T>(byte[] data) where T : struct
    {
        var handle = GCHandle.Alloc(data, GCHandleType.Pinned);
        try
        {
            return Marshal.PtrToStructure<T>(handle.AddrOfPinnedObject());
        }
        finally
        {
            handle.Free();
        }
    }

    public static byte[] Serialize<T>(T structure) where T : struct
    {
        var size = Marshal.SizeOf<T>();
        var buffer = new byte[size];
        var handle = GCHandle.Alloc(buffer, GCHandleType.Pinned);
        try
        {
            Marshal.StructureToPtr(structure, handle.AddrOfPinnedObject(), false);
        }
        finally
        {
            handle.Free();
        }
        return buffer;
    }
}
