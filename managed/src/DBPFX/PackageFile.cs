using System;
using System.Buffers;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Threading;
using DBPFX.Native;
using static DBPFX.Native.Engine;

namespace DBPFX
{
    /// <summary>
    /// A DBPF package from The Sims 3, The Sims Medieval or The Sims 4, memory-mapped by the native engine.
    /// Opening reads only the header. The index is decoded in one native call the first time it is
    /// needed, and resource bytes are touched only when read; uncompressed ones are never copied.
    /// </summary>
    /// <remarks>Safe to read from several threads at once. Changes go through <see cref="Edit"/>.</remarks>
    public sealed unsafe class PackageFile : IDisposable, IReadOnlyList<ResourceEntry>
    {
        private readonly PackageHandle _handle;
        private ResourceEntry[]? _entries;
        private PackageHeader? _header;
        private int _disposed;

        private PackageFile(PackageHandle handle, string? path)
        {
            _handle = handle;
            Path = path;
            Count = checked((int)dbpfx_count(handle));
        }

        // ================================================================== opening
        /// <summary>Map a package file. Other programs may keep it open; it is never written through.</summary>
        /// <exception cref="FileNotFoundException"/>
        /// <exception cref="PlatformNotSupportedException">The system is older than <see cref="Requirements"/> allows.</exception>
        /// <exception cref="InvalidDataException">Not a DBPF 2.x package, or its index is damaged.</exception>
        public static PackageFile Open(string path)
        {
            if (path == null) throw new ArgumentNullException(nameof(path));
            Requirements.EnsureSupported();
            string full = System.IO.Path.GetFullPath(path);
            byte[] utf8 = Utf8Z(full);
            fixed (byte* p = utf8)
            {
                int status = dbpfx_open(p, out PackageHandle handle);
                if (status < 0)
                {
                    handle.Dispose();
                    throw Failure(status, full);
                }
                return new PackageFile(handle, full);
            }
        }

        /// <summary>Open a package held in memory. The bytes are copied once into native memory.</summary>
        public static PackageFile Load(ReadOnlySpan<byte> bytes)
        {
            Requirements.EnsureSupported();
            fixed (byte* p = bytes)
            {
                int status = dbpfx_open_memory(p, (ulong)bytes.Length, out PackageHandle handle);
                if (status < 0)
                {
                    handle.Dispose();
                    throw Failure(status, null);
                }
                return new PackageFile(handle, null);
            }
        }

        /// <summary>Open a package from a stream. File streams are mapped; anything else is read into memory.</summary>
        public static PackageFile Load(Stream stream)
        {
            if (stream == null) throw new ArgumentNullException(nameof(stream));
            if (stream is FileStream file && File.Exists(file.Name)) return Open(file.Name);
            using var buffer = new MemoryStream();
            stream.CopyTo(buffer);
            return Load(new ReadOnlySpan<byte>(buffer.GetBuffer(), 0, (int)buffer.Length));
        }

        // ================================================================== header
        /// <summary>Full path of the mapped file; null for packages loaded from memory.</summary>
        public string? Path { get; }

        /// <summary>
        /// The game, read from the header: DBPF 2.1 is The Sims 4. DBPF 2.0 is reported as The Sims 3,
        /// because Medieval packages are byte-for-byte the same format.
        /// </summary>
        public Game Game => (Game)Check(dbpfx_detected_game(Live));

        public PackageHeader Header
        {
            get
            {
                PackageHandle handle = Live;
                if (_header is PackageHeader cached) return cached;
                Check(dbpfx_header_get(handle, out HeaderData data));
                var header = new PackageHeader(data);
                _header = header;
                return header;
            }
        }

        /// <summary>The 96 header bytes, read straight from the map. Valid until the package is disposed.</summary>
        public ReadOnlySpan<byte> RawHeader => new ReadOnlySpan<byte>(dbpfx_header_bytes(Live), 96);

        // ================================================================== index
        /// <summary>Number of index entries (TS4 deleted markers included).</summary>
        public int Count { get; }

        public ReadOnlySpan<ResourceEntry> Entries => LoadEntries();

        public ResourceEntry this[int index] => LoadEntries()[index];

        private ResourceEntry[] LoadEntries()
        {
            PackageHandle handle = Live; // throws once disposed, even when the index is cached
            ResourceEntry[]? entries = Volatile.Read(ref _entries);
            if (entries != null) return entries;
            entries = new ResourceEntry[Count];
            fixed (ResourceEntry* p = entries)
                Check(dbpfx_entries(handle, 0, (uint)Count, p));
            return Interlocked.CompareExchange(ref _entries, entries, null) ?? entries;
        }

        public bool Contains(ResourceKey key) => IndexOf(key) >= 0;

        /// <summary>Find a resource by key (native hash lookup, built on first use).</summary>
        public bool TryFind(ResourceKey key, out ResourceEntry entry)
        {
            int i = IndexOf(key);
            entry = i >= 0 ? this[i] : default;
            return i >= 0;
        }

        /// <exception cref="KeyNotFoundException"/>
        public ResourceEntry Find(ResourceKey key) =>
            TryFind(key, out var entry) ? entry : throw new KeyNotFoundException("No resource " + key + " in this package");

        /// <summary>Every entry of one resource type, in index order.</summary>
        public ResourceEntry[] FindAll(uint type)
        {
            long count = Check(dbpfx_find_type(Live, type, null, 0));
            if (count == 0) return Array.Empty<ResourceEntry>();
            var positions = new uint[count];
            fixed (uint* p = positions)
                Check(dbpfx_find_type(Live, type, p, (uint)count));
            ResourceEntry[] all = LoadEntries();
            var result = new ResourceEntry[count];
            for (int i = 0; i < result.Length; i++) result[i] = all[positions[i]];
            return result;
        }

        private int IndexOf(ResourceKey key) => (int)Check(dbpfx_find(Live, key.Type, key.Group, key.Instance));

        // ================================================================== reading
        /// <summary>
        /// The bytes exactly as stored (possibly compressed), straight from the map. Valid until the package
        /// is disposed; use <see cref="Read(in ResourceEntry)"/> for data that must outlive it.
        /// </summary>
        public ReadOnlySpan<byte> GetStoredBytes(in ResourceEntry entry)
        {
            Check(dbpfx_raw(Live, Own(entry), out byte* data, out ulong length));
            return new ReadOnlySpan<byte>(data, checked((int)length));
        }

        /// <summary>
        /// The resource bytes without any copy, if it is stored uncompressed (same lifetime as
        /// <see cref="GetStoredBytes"/>). Returns false for compressed resources.
        /// </summary>
        public bool TryGetBytes(in ResourceEntry entry, out ReadOnlySpan<byte> bytes)
        {
            if (Check(dbpfx_direct(Live, Own(entry), out byte* data, out ulong length)) == 1)
            {
                bytes = new ReadOnlySpan<byte>(data, checked((int)length));
                return true;
            }
            bytes = default;
            return false;
        }

        /// <summary>Decompress (or copy) a resource into <paramref name="destination"/>, which must be exactly <see cref="ResourceEntry.Size"/> long.</summary>
        public void ReadInto(in ResourceEntry entry, Span<byte> destination)
        {
            uint index = Own(entry);
            fixed (byte* d = destination)
                Check(dbpfx_read_into(Live, index, d, (ulong)destination.Length));
        }

        public byte[] ReadAllBytes(in ResourceEntry entry)
        {
            if (entry.IsDeletedMarker) return Array.Empty<byte>();
            var bytes = new byte[checked((int)entry.Size)];
            ReadInto(entry, bytes);
            return bytes;
        }

        public byte[] ReadAllBytes(ResourceKey key) => ReadAllBytes(Find(key));

        /// <summary>
        /// The resource bytes, without a copy when stored uncompressed (the lease keeps the file mapped, even
        /// after this package is disposed) or decoded into a pooled buffer. Dispose the lease when done.
        /// </summary>
        public ResourceLease Read(in ResourceEntry entry)
        {
            uint index = Own(entry);
            if (entry.IsDeletedMarker) return ResourceLease.Empty;
            if (Check(dbpfx_direct(Live, index, out byte* data, out ulong length)) == 1)
                return ResourceLease.FromMapping(Live, data, checked((int)length));

            int size = checked((int)entry.Size);
            byte[] buffer = ArrayPool<byte>.Shared.Rent(Math.Max(size, 1));
            try
            {
                fixed (byte* d = buffer)
                    Check(dbpfx_read_into(Live, index, d, (ulong)size));
                return ResourceLease.FromPool(buffer, size);
            }
            catch
            {
                ArrayPool<byte>.Shared.Return(buffer);
                throw;
            }
        }

        public ResourceLease Read(ResourceKey key) => Read(Find(key));

        /// <summary>A read-only stream over the resource. Disposing the stream releases the data.</summary>
        public Stream OpenStream(in ResourceEntry entry) => Read(entry).AsStream(ownsLease: true);

        /// <summary>Ask the OS to page in a resource's bytes ahead of use.</summary>
        public void Prefetch(in ResourceEntry entry) => dbpfx_prefetch(Live, Own(entry));

        /// <summary>Guards against entries taken from a different package.</summary>
        private uint Own(in ResourceEntry entry)
        {
            int i = entry.Index;
            if ((uint)i >= (uint)Count) throw new ArgumentException("The entry does not belong to this package", nameof(entry));
            ref readonly ResourceEntry mine = ref LoadEntries()[i];
            if (mine.Key != entry.Key || mine.Offset != entry.Offset)
                throw new ArgumentException("The entry does not belong to this package", nameof(entry));
            return (uint)i;
        }

        // ================================================================== editing
        /// <summary>
        /// Start editing. Saving copies untouched resources byte-for-byte from the map. Pass a different
        /// <paramref name="target"/> to convert, e.g. a The Sims 4 package for The Sims 3 (zlib data is
        /// re-encoded as RefPack and deleted markers are dropped).
        /// </summary>
        public PackageWriter Edit(Game? target = null) => new PackageWriter(Live, target);

        // ================================================================== plumbing
        /// <summary>True after <see cref="Dispose"/>, even while leases still keep the file mapped.</summary>
        public bool IsDisposed => Volatile.Read(ref _disposed) != 0;

        /// <summary>
        /// Releases this package. Data already handed out as <see cref="ResourceLease"/> stays readable; the file
        /// is unmapped when the last lease is released.
        /// </summary>
        public void Dispose()
        {
            if (Interlocked.Exchange(ref _disposed, 1) == 0) _handle.Dispose();
        }

        /// <summary>The native handle, or ObjectDisposedException once this package is disposed.</summary>
        private PackageHandle Live =>
            Volatile.Read(ref _disposed) == 0 ? _handle : throw new ObjectDisposedException(nameof(PackageFile));

        public IEnumerator<ResourceEntry> GetEnumerator()
        {
            ResourceEntry[] entries = LoadEntries();
            for (int i = 0; i < entries.Length; i++) yield return entries[i];
        }

        IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

        public override string ToString() => (Path ?? "(memory)") + " - " + Header;
    }
}
