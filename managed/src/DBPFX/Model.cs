using System;
using System.Globalization;
using System.Runtime.InteropServices;

namespace DBPFX
{
    /// <summary>The game a package belongs to. The Sims 3 and The Sims Medieval share DBPF 2.0 exactly.</summary>
    public enum Game
    {
        Sims3 = 0,
        Medieval = 1,
        Sims4 = 2,
    }

    [Flags]
    public enum Games
    {
        None = 0,
        Sims3 = 1,
        Medieval = 2,
        Sims4 = 4,
        Sims3AndMedieval = Sims3 | Medieval,
        All = Sims3 | Medieval | Sims4,
    }

    /// <summary>How a resource's bytes are stored, as recorded in the package index.</summary>
    public enum CompressionMethod : ushort
    {
        None = 0x0000,
        /// <summary>zlib; what The Sims 4 writes.</summary>
        Zlib = 0x5A42,
        /// <summary>The Sims 4 streamable RefPack.</summary>
        Streamable = 0xFFFE,
        /// <summary>RefPack; what The Sims 3 and Medieval write. The Sims 4 reads it too.</summary>
        RefPack = 0xFFFF,
        /// <summary>The Sims 4 deleted-record marker: an index entry without a resource.</summary>
        DeletedMarker = 0xFFE0,
    }

    /// <summary>How the writer stores data it is given.</summary>
    public enum Storage
    {
        /// <summary>New resources are compressed; replaced ones keep the way the original was stored.</summary>
        Auto = 0,
        /// <summary>Compressed with the target game's codec (only when that makes it smaller).</summary>
        Compressed = 1,
        Uncompressed = 2,
    }

    /// <summary>
    /// Type, group and instance: the identity of a resource. Text form is <c>TTTTTTTT:GGGGGGGG:IIIIIIIIIIIIIIII</c>
    /// (hexadecimal); <see cref="Parse"/> also accepts '-' separators and "0x" prefixes.
    /// </summary>
    public readonly record struct ResourceKey(uint Type, uint Group, ulong Instance) : IComparable<ResourceKey>
    {
        public int CompareTo(ResourceKey other)
        {
            int c = Type.CompareTo(other.Type);
            if (c != 0) return c;
            c = Group.CompareTo(other.Group);
            return c != 0 ? c : Instance.CompareTo(other.Instance);
        }

        public ResourceKey WithType(uint type) => new ResourceKey(type, Group, Instance);
        public ResourceKey WithGroup(uint group) => new ResourceKey(Type, group, Instance);
        public ResourceKey WithInstance(ulong instance) => new ResourceKey(Type, Group, instance);

        public override string ToString() =>
            Type.ToString("X8", CultureInfo.InvariantCulture) + ":" +
            Group.ToString("X8", CultureInfo.InvariantCulture) + ":" +
            Instance.ToString("X16", CultureInfo.InvariantCulture);

        public static ResourceKey Parse(string text) =>
            TryParse(text, out var key) ? key : throw new FormatException("Not a resource key: '" + text + "'");

        public static bool TryParse(string? text, out ResourceKey key)
        {
            key = default;
            if (text == null) return false;
            string[] parts = text.Trim().Split(':', '-');
            if (parts.Length != 3) return false;
            if (!Hex(parts[0], out ulong type) || type > uint.MaxValue) return false;
            if (!Hex(parts[1], out ulong group) || group > uint.MaxValue) return false;
            if (!Hex(parts[2], out ulong instance)) return false;
            key = new ResourceKey((uint)type, (uint)group, instance);
            return true;
        }

        private static bool Hex(string part, out ulong value)
        {
            part = part.Trim();
            if (part.StartsWith("0x", StringComparison.OrdinalIgnoreCase)) part = part.Substring(2);
            return ulong.TryParse(part, NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out value);
        }
    }

    /// <summary>
    /// One record of a package's index. The layout mirrors the native <c>dbpfx_entry</c> (40 bytes) so the
    /// whole index is filled by a single native call.
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public readonly struct ResourceEntry
    {
#pragma warning disable CS0649 // written by the native engine
        private readonly uint _type;
        private readonly uint _group;
        private readonly ulong _instance;
        private readonly uint _offset;
        private readonly uint _storedSize;
        private readonly uint _size;
        private readonly ushort _compression;
        private readonly ushort _committed;
        private readonly uint _index;
        private readonly uint _reserved;
#pragma warning restore CS0649

        public ResourceKey Key => new ResourceKey(_type, _group, _instance);
        public uint Type => _type;
        public uint Group => _group;
        public ulong Instance => _instance;

        /// <summary>Position of this entry in the package index.</summary>
        public int Index => (int)_index;
        /// <summary>Where the stored bytes start in the file.</summary>
        public long Offset => _offset;
        /// <summary>Size as stored in the file (compressed size for compressed resources).</summary>
        public int StoredSize => (int)_storedSize;
        /// <summary>Size of the resource once decompressed.</summary>
        public long Size => IsDeletedMarker ? 0 : _size;
        public CompressionMethod Compression => (CompressionMethod)_compression;
        public bool IsCompressed => _storedSize != _size && !IsDeletedMarker;
        /// <summary>A The Sims 4 deleted-record marker: listed in the index, but holds no resource.</summary>
        public bool IsDeletedMarker => _compression == 0xFFE0 || (_storedSize == 1 && _size == uint.MaxValue);

        internal uint RawSize => _size;

        public override string ToString() =>
            IsDeletedMarker ? Key + " (deleted marker)" : Key + " (" + _size + " bytes" + (IsCompressed ? ", " + Compression : "") + ")";
    }

    /// <summary>The 96-byte DBPF header.</summary>
    public readonly struct PackageHeader
    {
        internal PackageHeader(in Native.Engine.HeaderData h)
        {
            Major = h.Major;
            Minor = h.Minor;
            UserMajor = h.UserMajor;
            UserMinor = h.UserMinor;
            Created = h.Created;
            Updated = h.Updated;
            IndexCount = h.IndexCount;
            IndexSize = h.IndexSize;
            IndexPosition = h.IndexPosition != 0 ? h.IndexPosition : h.IndexPositionLow;
            IndexVersion = h.IndexVersion;
            IndexFlags = h.IndexFlags;
        }

        /// <summary>Always 2.</summary>
        public uint Major { get; }
        /// <summary>0 for The Sims 3 / Medieval, 1 for The Sims 4.</summary>
        public uint Minor { get; }
        public uint UserMajor { get; }
        public uint UserMinor { get; }
        public uint Created { get; }
        public uint Updated { get; }
        public uint IndexCount { get; }
        public uint IndexSize { get; }
        public uint IndexPosition { get; }
        public uint IndexVersion { get; }
        /// <summary>1 = all entries share the type, 2 = the group, 4 = the high half of the instance.</summary>
        public uint IndexFlags { get; }

        public override string ToString() => "DBPF " + Major + "." + Minor + ", " + IndexCount + " entries";
    }

    /// <summary>A failure of the native engine that no standard exception type describes.</summary>
    public sealed class PackageEngineException : Exception
    {
        public PackageEngineException(string message, int code) : base(message) { Code = code; }
        public int Code { get; }
    }
}

#if !NET5_0_OR_GREATER
namespace System.Runtime.CompilerServices
{
    /// <summary>Lets record structs compile for .NET Standard 2.1.</summary>
    internal static class IsExternalInit { }
}
#endif
