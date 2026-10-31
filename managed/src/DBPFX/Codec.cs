using System;
using System.Runtime.InteropServices;
using static DBPFX.Native.Engine;

namespace DBPFX
{
    /// <summary>Direct access to the package compression codecs.</summary>
    public static unsafe class Codec
    {
        /// <summary>Version of the native engine, e.g. "DBPFX 0.1.0-canary".</summary>
        public static string EngineVersion => Utf8(dbpfx_version());

        /// <summary>
        /// Decompress RefPack or zlib data (detected from its first bytes) into <paramref name="destination"/>,
        /// which must be exactly the uncompressed size.
        /// </summary>
        public static void Decompress(ReadOnlySpan<byte> source, Span<byte> destination)
        {
            Requirements.EnsureSupported();
            fixed (byte* s = source)
            fixed (byte* d = destination)
                Check(dbpfx_decompress(s, (ulong)source.Length, d, (ulong)destination.Length));
        }

        /// <summary>
        /// Compress with <see cref="CompressionMethod.RefPack"/> (all three games) or
        /// <see cref="CompressionMethod.Zlib"/> (The Sims 4). Returns null when the data would not get smaller.
        /// </summary>
        public static byte[]? Compress(CompressionMethod method, ReadOnlySpan<byte> source)
        {
            if (method != CompressionMethod.RefPack && method != CompressionMethod.Zlib)
                throw new ArgumentException("Only RefPack and Zlib can be written", nameof(method));
            Requirements.EnsureSupported();
            fixed (byte* s = source)
            {
                if (Check(dbpfx_compress((int)method, s, (ulong)source.Length, out IntPtr output, out ulong length)) == 0)
                    return null;
                try
                {
                    var result = new byte[checked((int)length)];
                    Marshal.Copy(output, result, 0, result.Length);
                    return result;
                }
                finally
                {
                    dbpfx_free(output);
                }
            }
        }

        /// <summary>The codec a game uses when writing compressed resources.</summary>
        public static CompressionMethod DefaultFor(Game game) => game == Game.Sims4 ? CompressionMethod.Zlib : CompressionMethod.RefPack;
    }
}
