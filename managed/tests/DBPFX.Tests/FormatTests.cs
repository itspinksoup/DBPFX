using System.Linq;
using DBPFX.Formats;
using Xunit;
using static DBPFX.Tests.PackageFileTests;

namespace DBPFX.Tests
{
    public class FormatTests
    {
        [Fact]
        public void LegacyStringTable()
        {
            using var package = PackageFile.Open(Fixture("ts3.package"));
            using ResourceLease data = package.Read(package.FindAll(KnownTypes.StringTable).Single());
            var reader = new StringTableReader(data.Span);
            Assert.Equal(StringTableFormat.Legacy, reader.Format);
            Assert.Equal(3, reader.Count);
            Assert.True(reader.TryGet(0x00000000DEADBEEF, out string text));
            Assert.Equal("Ñandú – ünïcødé ✓", text);
            Assert.Equal(data.ToArray(), StringTable.Parse(data.Span).ToArray()); // unchanged => identical bytes
        }

        [Fact]
        public void ModernStringTable()
        {
            using var package = PackageFile.Open(Fixture("ts4.package"));
            using ResourceLease data = package.Read(package.FindAll(KnownTypes.StringTable).Single());
            var reader = new StringTableReader(data.Span);
            Assert.Equal(StringTableFormat.Modern, reader.Format);
            Assert.True(reader.TryGet(0xDEADBEEF, out string text));
            Assert.Equal("Ñandú ✓", text);
            Assert.Equal(data.ToArray(), StringTable.Parse(data.Span).ToArray());
        }

        [Fact]
        public void StringTableEditingAndConversion()
        {
            var table = new StringTable(StringTableFormat.Legacy);
            table.Add(1, "one");
            table[2] = "two";
            table[1] = "uno";
            Assert.Equal(2, table.Count);
            Assert.True(table.Remove(2));

            table.Format = StringTableFormat.Modern;
            var back = StringTable.Parse(table.ToArray());
            Assert.Equal(StringTableFormat.Modern, back.Format);
            Assert.Equal("uno", back[1]);
        }

        [Fact]
        public void NameMapRoundTrip()
        {
            using var package = PackageFile.Open(Fixture("ts3.package"));
            using ResourceLease data = package.Read(package.FindAll(KnownTypes.NameMap).Single());
            var reader = new NameMapReader(data.Span);
            Assert.Equal(2, reader.Count);
            Assert.True(reader.TryGet(0xABCDEF0123, out string name));
            Assert.Equal("llama_hat", name);

            NameMap map = NameMap.Parse(data.Span);
            Assert.Equal(data.ToArray(), map.ToArray());
            map[0x42] = "new_name";
            Assert.Equal("new_name", NameMap.Parse(map.ToArray())[0x42]);
        }
    }
}
