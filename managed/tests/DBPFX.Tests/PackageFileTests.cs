using System;
using System.IO;
using System.Linq;
using Xunit;

namespace DBPFX.Tests
{
    public class PackageFileTests
    {
        internal static string Fixture(string name) => Path.Combine(AppContext.BaseDirectory, "fixtures", name);

        [Fact]
        public void OpensTheSims3Layout()
        {
            using var package = PackageFile.Open(Fixture("ts3.package"));
            Assert.Equal(Game.Sims3, package.Game);
            Assert.Equal(6, package.Count);
            Assert.Equal(2u, package.Header.Major);
            Assert.Equal(0u, package.Header.Minor);
            Assert.Equal((byte)'D', package.RawHeader[0]);
            Assert.Contains(package, e => e.Compression == CompressionMethod.RefPack);
            for (int i = 0; i < package.Count; i++) Assert.Equal(i, package[i].Index);
        }

        [Fact]
        public void OpensTheSims4Layout()
        {
            using var package = PackageFile.Open(Fixture("ts4.package"));
            Assert.Equal(Game.Sims4, package.Game);
            Assert.Equal(1u, package.Header.Minor);
            Assert.Contains(package, e => e.Compression == CompressionMethod.Zlib);
            ResourceEntry marker = Assert.Single(package, e => e.IsDeletedMarker);
            Assert.Equal(0, marker.Size);
            Assert.False(package.Contains(marker.Key));
        }

        [Fact]
        public void FindsByKeyAndType()
        {
            using var package = PackageFile.Open(Fixture("ts4.package"));
            var key = ResourceKey.Parse("545AC67A:80000000:00F7A2C3F2E55B22");
            Assert.True(package.TryFind(key, out ResourceEntry entry));
            Assert.Equal(key, entry.Key);
            Assert.False(package.TryFind(new ResourceKey(1, 2, 3), out _));
            Assert.Throws<System.Collections.Generic.KeyNotFoundException>(() => package.Find(new ResourceKey(1, 2, 3)));
            Assert.Single(package.FindAll(KnownTypes.StringTable));
            Assert.Empty(package.FindAll(0x12345678));
        }

        [Fact]
        public void UncompressedDataIsMappedAndOutlivesThePackage()
        {
            var package = PackageFile.Open(Fixture("ts3.package"));
            ResourceEntry entry = package.FindAll(KnownTypes.NameMap).Single();
            Assert.True(package.TryGetBytes(entry, out ReadOnlySpan<byte> direct));
            ResourceLease lease = package.Read(entry);
            Assert.True(lease.IsMapped);
            Assert.True(direct.SequenceEqual(lease.Span));

            package.Dispose();                       // the lease keeps the mapping alive...
            Assert.True(package.IsDisposed);
            Assert.Throws<ObjectDisposedException>(() => package.Count == 0 ? 0 : package[0].Index);
            Assert.Throws<ObjectDisposedException>(() => package.Read(entry));
            Assert.Equal("llama_hat", Formats.NameMap.Parse(lease.Span)[0xABCDEF0123]);
            lease.Dispose();                         // ...until it is released
            Assert.Throws<ObjectDisposedException>(() => lease.Span.Length);
        }

        [Fact]
        public void CompressedDataIsDecodedIntoAPooledBuffer()
        {
            using var package = PackageFile.Open(Fixture("ts3.package"));
            ResourceEntry entry = package.First(e => e.IsCompressed);
            Assert.False(package.TryGetBytes(entry, out _));
            using ResourceLease lease = package.Read(entry);
            Assert.False(lease.IsMapped);
            Assert.Equal(entry.Size, lease.Length);
            Assert.Equal(package.ReadAllBytes(entry), lease.ToArray());
            using Stream stream = lease.AsStream();
            Assert.Equal(entry.Size, stream.Length);
        }

        [Fact]
        public void RejectsEntriesFromAnotherPackage()
        {
            using var a = PackageFile.Open(Fixture("ts3.package"));
            using var b = PackageFile.Open(Fixture("ts4.package"));
            Assert.Throws<ArgumentException>(() => b.ReadAllBytes(a[0]));
        }

        [Fact]
        public void ErrorsAreStandardExceptions()
        {
            Assert.Throws<FileNotFoundException>(() => PackageFile.Open(Fixture("missing.package")));
            Assert.Throws<InvalidDataException>(() => PackageFile.Load(new byte[200]));
            using var package = PackageFile.Open(Fixture("ts3.package"));
            Assert.Throws<ArgumentException>(() => package.ReadInto(package[0], new byte[1]));
        }

        [Fact]
        public void EmptyPackage()
        {
            using var package = PackageFile.Open(Fixture("empty.package"));
            Assert.Empty(package);
            Assert.True(package.Entries.IsEmpty);
        }

        [Fact]
        public void LoadsFromStreamsAndMemory()
        {
            byte[] bytes = File.ReadAllBytes(Fixture("ts4.package"));
            using var fromMemory = PackageFile.Load(bytes);
            using var fromStream = PackageFile.Load(new MemoryStream(bytes));
            Assert.Null(fromMemory.Path);
            Assert.Equal(fromMemory.Count, fromStream.Count);
        }

        [Fact]
        public void ResourceKeyText()
        {
            var key = new ResourceKey(0x220557DA, 0x80000000, 0x00F7A2C3F2E55B21);
            Assert.Equal("220557DA:80000000:00F7A2C3F2E55B21", key.ToString());
            Assert.Equal(key, ResourceKey.Parse(key.ToString()));
            Assert.Equal(key, ResourceKey.Parse("0x220557DA-0x80000000-0x00F7A2C3F2E55B21"));
            Assert.False(ResourceKey.TryParse("not a key", out _));
            Assert.True(new ResourceKey(1, 0, 0).CompareTo(new ResourceKey(2, 0, 0)) < 0);
        }

        [Fact]
        public void KnownTypeTable()
        {
            Assert.True(DBPFX.KnownTypes.TryGet(DBPFX.KnownTypes.StringTable, Game.Sims3, out var ts3));
            Assert.Equal("STBL", ts3.Tag);
            Assert.Equal(Games.Sims3AndMedieval, ts3.Games);
            Assert.True(DBPFX.KnownTypes.TryGet(DBPFX.KnownTypes.StringTable, Game.Medieval, out var tsm));
            Assert.Same(ts3, tsm);                                    // Medieval shares The Sims 3 meanings
            Assert.Equal("String table", DBPFX.KnownTypes.NameOf(DBPFX.KnownTypes.StringTable, Game.Sims4));

            // one ID, two meanings
            Assert.Equal("XML resource", DBPFX.KnownTypes.NameOf(0x0333406C, Game.Sims3));
            Assert.Equal("Font configuration", DBPFX.KnownTypes.NameOf(0x0333406C, Game.Sims4));
            Assert.Equal(2, DBPFX.KnownTypes.Lookup(0x0333406C).Count);

            Assert.Equal("Buff tuning", DBPFX.KnownTypes.NameOf(0x6017E896, Game.Sims4));
            Assert.False(DBPFX.KnownTypes.IsKnown(0x6017E896, Game.Sims3));
            Assert.Equal("12345678", DBPFX.KnownTypes.TagOf(0x12345678));
            Assert.True(DBPFX.KnownTypes.All.Count > 400);

            DBPFX.KnownTypes.Register(0x12345678, "TEST", "Test type", Games.Sims4);
            Assert.Equal("Test type", DBPFX.KnownTypes.NameOf(0x12345678, Game.Sims4));
        }
    }
}
