using System;
using System.IO;
using System.Linq;
using System.Text;
using Xunit;
using static DBPFX.Tests.PackageFileTests;

namespace DBPFX.Tests
{
    public class PackageWriterTests
    {
        private static byte[] Text(int length) =>
            Enumerable.Range(0, length).Select(i => (byte)"plumbob career "[i % 15]).ToArray();

        [Theory]
        [InlineData(Game.Sims3, CompressionMethod.RefPack)]
        [InlineData(Game.Medieval, CompressionMethod.RefPack)]
        [InlineData(Game.Sims4, CompressionMethod.Zlib)]
        public void CreatesPackagesForEachGame(Game game, CompressionMethod expected)
        {
            byte[] bytes;
            using (var writer = PackageWriter.Create(game))
            {
                Assert.True(writer.Add(new ResourceKey(KnownTypes.StringTable, 0, 1), Text(5000)));
                Assert.True(writer.Add(new ResourceKey(KnownTypes.DdsImage, 0, 2), new byte[] { 1, 2, 3 }));
                Assert.True(writer.Add(new ResourceKey(KnownTypes.XmlTuning, 0, 3), Text(5000), Storage.Uncompressed));
                Assert.False(writer.Add(new ResourceKey(KnownTypes.DdsImage, 0, 2), new byte[] { 9 }));
                Assert.Equal(3, writer.Count);
                bytes = writer.ToArray();
            }

            using var package = PackageFile.Load(bytes);
            Assert.Equal(game == Game.Sims4 ? Game.Sims4 : Game.Sims3, package.Game);
            Assert.Equal(expected, package.Find(new ResourceKey(KnownTypes.StringTable, 0, 1)).Compression);
            Assert.Equal(CompressionMethod.None, package.Find(new ResourceKey(KnownTypes.XmlTuning, 0, 3)).Compression);
            Assert.Equal(Text(5000), package.ReadAllBytes(new ResourceKey(KnownTypes.StringTable, 0, 1)));
            Assert.Equal(new byte[] { 1, 2, 3 }, package.ReadAllBytes(new ResourceKey(KnownTypes.DdsImage, 0, 2)));
        }

        [Fact]
        public void EditsByKey()
        {
            using var source = PackageFile.Open(Fixture("ts3.package"));
            var nameMap = source.FindAll(KnownTypes.NameMap).Single().Key;
            var script = source.FindAll(KnownTypes.ScriptAssembly).Single().Key;
            var moved = nameMap.WithInstance(0x9999);

            using var writer = source.Edit();
            writer.Set(new ResourceKey(1, 2, 3), Encoding.ASCII.GetBytes("new"));
            writer.Set(nameMap, Encoding.ASCII.GetBytes("replaced"));
            Assert.True(writer.Rename(nameMap, moved));
            Assert.True(writer.Remove(script));
            Assert.False(writer.Remove(script));
            Assert.Throws<InvalidOperationException>(() => writer.Rename(moved, source[0].Key));
            Assert.Equal("replaced", Encoding.ASCII.GetString(writer.ReadAllBytes(moved)));
            Assert.Equal(6, writer.Count);
            Assert.Contains(new ResourceKey(1, 2, 3), writer.Keys);

            using var result = PackageFile.Load(writer.ToArray());
            Assert.Equal("new", Encoding.ASCII.GetString(result.ReadAllBytes(new ResourceKey(1, 2, 3))));
            Assert.Equal("replaced", Encoding.ASCII.GetString(result.ReadAllBytes(moved)));
            Assert.False(result.Contains(nameMap));
            Assert.False(result.Contains(script));
        }

        [Fact]
        public void UntouchedResourcesAreCopiedByteForByte()
        {
            using var source = PackageFile.Open(Fixture("ts3.package"));
            using var writer = source.Edit();
            writer.Set(new ResourceKey(1, 2, 3), Text(100));
            using var result = PackageFile.Load(writer.ToArray());
            foreach (ResourceEntry entry in source)
            {
                ResourceEntry copy = result.Find(entry.Key);
                Assert.Equal(entry.Compression, copy.Compression);
                Assert.True(source.GetStoredBytes(entry).SequenceEqual(result.GetStoredBytes(copy)));
            }
        }

        [Fact]
        public void ConvertsTheSims4ToTheSims3()
        {
            using var source = PackageFile.Open(Fixture("ts4.package"));
            using var writer = source.Edit(Game.Sims3);
            using var result = PackageFile.Load(writer.ToArray());
            Assert.Equal(Game.Sims3, result.Game);
            Assert.DoesNotContain(result, e => e.Compression == CompressionMethod.Zlib || e.IsDeletedMarker);
            foreach (ResourceEntry entry in source.Where(e => !e.IsDeletedMarker))
                Assert.Equal(source.ReadAllBytes(entry), result.ReadAllBytes(entry.Key));
        }

        [Fact]
        public void StorageCanBeChanged()
        {
            using var source = PackageFile.Open(Fixture("ts3.package"));
            ResourceEntry compressed = source.First(e => e.IsCompressed);
            using var writer = source.Edit();
            Assert.True(writer.SetStorage(compressed.Key, Storage.Uncompressed));
            using var result = PackageFile.Load(writer.ToArray());
            Assert.Equal(CompressionMethod.None, result.Find(compressed.Key).Compression);
            Assert.Equal(source.ReadAllBytes(compressed), result.ReadAllBytes(compressed.Key));
        }

        [Fact]
        public void SavesToFilesStreamsAndInPlace()
        {
            string folder = Path.Combine(Path.GetTempPath(), "dbpfx-" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(folder);
            try
            {
                string path = Path.Combine(folder, "edit.package");
                using (var source = PackageFile.Open(Fixture("ts3.package")))
                using (var writer = source.Edit())
                {
                    writer.Save(path);
                    var stream = new MemoryStream();
                    writer.Save(stream);
                    Assert.Equal(new FileInfo(path).Length, stream.Length);
                }

                var opened = PackageFile.Open(path);
                using (var writer = opened.Edit())
                {
                    writer.Set(new ResourceKey(1, 2, 3), Text(100));
                    opened.Dispose();                // a mapped file cannot be replaced on Windows
                    writer.SaveInPlace();
                }
                using var reopened = PackageFile.Open(path);
                Assert.Equal(7, reopened.Count);
                Assert.Equal(Text(100), reopened.ReadAllBytes(new ResourceKey(1, 2, 3)));
            }
            finally
            {
                Directory.Delete(folder, recursive: true);
            }
        }

        [Fact]
        public void CodecRoundTrip()
        {
            byte[] source = Text(100_000);
            foreach (var method in new[] { CompressionMethod.RefPack, CompressionMethod.Zlib })
            {
                byte[]? packed = Codec.Compress(method, source);
                Assert.NotNull(packed);
                Assert.True(packed!.Length < source.Length);
                var back = new byte[source.Length];
                Codec.Decompress(packed, back);
                Assert.Equal(source, back);
            }
            Assert.Null(Codec.Compress(CompressionMethod.RefPack, new byte[] { 1, 2, 3 }));
            Assert.StartsWith("DBPFX", Codec.EngineVersion);
        }
    }
}
