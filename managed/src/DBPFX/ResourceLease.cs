using System;
using System.Buffers;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;

namespace DBPFX
{
    /// <summary>
    /// Temporary access to one resource's bytes: a window into the memory-mapped package (no copy), or a
    /// pooled buffer holding decompressed data. Dispose it to release the mapping or return the buffer;
    /// a forgotten lease is released by the garbage collector.
    /// </summary>
    public sealed unsafe class ResourceLease : IDisposable
    {
        private byte* _mapped;
        private byte[]? _buffer;
        private readonly bool _pooled;
        private SafeHandle? _owner;
        private MemoryManager<byte>? _memory;
        private int _released;

        /// <summary>A lease over zero bytes. Disposing it does nothing.</summary>
        public static ResourceLease Empty { get; } = new ResourceLease(null, Array.Empty<byte>(), 0, pooled: false, owner: null);

        private ResourceLease(byte* mapped, byte[]? buffer, int length, bool pooled, SafeHandle? owner)
        {
            _mapped = mapped;
            _buffer = buffer;
            _pooled = pooled;
            _owner = owner;
            Length = length;
            if (owner == null) GC.SuppressFinalize(this);
        }

        internal static ResourceLease FromMapping(SafeHandle owner, byte* data, int length)
        {
            bool added = false;
            owner.DangerousAddRef(ref added); // keeps the file mapped for as long as this lease lives
            if (!added) throw new ObjectDisposedException(nameof(PackageFile));
            return new ResourceLease(data, null, length, pooled: false, owner);
        }

        internal static ResourceLease FromPool(byte[] buffer, int length) =>
            new ResourceLease(null, buffer, length, pooled: true, owner: null);

        /// <summary>Wraps an existing array (not pooled, not copied).</summary>
        public static ResourceLease FromArray(byte[] bytes)
        {
            if (bytes == null) throw new ArgumentNullException(nameof(bytes));
            return new ResourceLease(null, bytes, bytes.Length, pooled: false, owner: null);
        }

        public int Length { get; }

        /// <summary>True when the bytes are read directly from the package file's mapping.</summary>
        public bool IsMapped => _owner != null;

        public ReadOnlySpan<byte> Span
        {
            get
            {
                EnsureAlive();
                return _buffer != null ? new ReadOnlySpan<byte>(_buffer, 0, Length) : new ReadOnlySpan<byte>(_mapped, Length);
            }
        }

        public ReadOnlyMemory<byte> Memory
        {
            get
            {
                EnsureAlive();
                if (_buffer != null) return new ReadOnlyMemory<byte>(_buffer, 0, Length);
                if (Length == 0) return ReadOnlyMemory<byte>.Empty;
                _memory ??= new MappedMemory(_mapped, Length);
                return _memory.Memory;
            }
        }

        public byte[] ToArray() => Span.ToArray();

        /// <summary>
        /// A read-only stream over the bytes, without copying. With <paramref name="ownsLease"/> the stream
        /// releases this lease when it is closed; otherwise it must not be used after the lease is disposed.
        /// </summary>
        public Stream AsStream(bool ownsLease = false)
        {
            EnsureAlive();
            Stream inner = _buffer != null
                ? new MemoryStream(_buffer, 0, Length, writable: false, publiclyVisible: true)
                : new UnmanagedMemoryStream(_mapped, Length, Length, FileAccess.Read);
            return ownsLease ? new LeaseStream(inner, this) : inner;
        }

        private void EnsureAlive()
        {
            if (Volatile.Read(ref _released) != 0) throw new ObjectDisposedException(nameof(ResourceLease));
        }

        public void Dispose()
        {
            if (ReferenceEquals(this, Empty) || Interlocked.Exchange(ref _released, 1) != 0) return;
            if (_pooled && _buffer != null) ArrayPool<byte>.Shared.Return(_buffer);
            _buffer = null;
            _mapped = null;
            _owner?.DangerousRelease();
            _owner = null;
            GC.SuppressFinalize(this);
        }

        ~ResourceLease()
        {
            // Only the mapping reference matters here; pooled arrays are never returned from the finalizer.
            if (Interlocked.Exchange(ref _released, 1) == 0) _owner?.DangerousRelease();
        }

        private sealed class MappedMemory : MemoryManager<byte>
        {
            private readonly byte* _pointer;
            private readonly int _length;

            public MappedMemory(byte* pointer, int length)
            {
                _pointer = pointer;
                _length = length;
            }

            public override Span<byte> GetSpan() => new Span<byte>(_pointer, _length);
            public override MemoryHandle Pin(int elementIndex = 0) => new MemoryHandle(_pointer + elementIndex);
            public override void Unpin() { }
            protected override void Dispose(bool disposing) { }
        }

        private sealed class LeaseStream : Stream
        {
            private readonly Stream _inner;
            private readonly ResourceLease _lease;

            public LeaseStream(Stream inner, ResourceLease lease)
            {
                _inner = inner;
                _lease = lease;
            }

            public override bool CanRead => _inner.CanRead;
            public override bool CanSeek => _inner.CanSeek;
            public override bool CanWrite => false;
            public override long Length => _inner.Length;
            public override long Position { get => _inner.Position; set => _inner.Position = value; }
            public override void Flush() { }
            public override int Read(byte[] buffer, int offset, int count) => _inner.Read(buffer, offset, count);
            public override int Read(Span<byte> buffer) => _inner.Read(buffer);
            public override int ReadByte() => _inner.ReadByte();
            public override long Seek(long offset, SeekOrigin origin) => _inner.Seek(offset, origin);
            public override void SetLength(long value) => throw new NotSupportedException();
            public override void Write(byte[] buffer, int offset, int count) => throw new NotSupportedException();

            protected override void Dispose(bool disposing)
            {
                if (disposing)
                {
                    _inner.Dispose();
                    _lease.Dispose();
                }
                base.Dispose(disposing);
            }
        }
    }
}
