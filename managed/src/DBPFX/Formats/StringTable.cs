using System;
using System.Buffers.Binary;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace DBPFX.Formats
{
    /// <summary>The two string table layouts.</summary>
    public enum StringTableFormat
    {
        /// <summary>Version 2, used by The Sims 3 and Medieval: 64-bit keys, UTF-16 text.</summary>
        Legacy = 2,
        /// <summary>Version 5, used by The Sims 4: 32-bit key hashes, UTF-8 text, per-string flags.</summary>
        Modern = 5,
    }

    /// <summary>
    /// Reads a string table (type 0x220557DA) in place. Nothing is decoded until a string's
    /// <see cref="StringTableItem.Text"/> is asked for.
    /// </summary>
    public readonly ref struct StringTableReader
    {
        internal const uint Magic = 0x4C425453; // "STBL"
        internal const int LegacyHeader = 17, ModernHeader = 21;

        private readonly ReadOnlySpan<byte> _data;

        public StringTableReader(ReadOnlySpan<byte> data)
        {
            if (data.Length < 5 || BinaryPrimitives.ReadUInt32LittleEndian(data) != Magic)
                throw new InvalidDataException("Not a string table");
            _data = data;
            if (data[4] == 2)
            {
                Require(data, LegacyHeader);
                Format = StringTableFormat.Legacy;
                Count = checked((int)BinaryPrimitives.ReadUInt32LittleEndian(data.Slice(7)));
            }
            else if (data[4] == 5)
            {
                Require(data, ModernHeader);
                Format = StringTableFormat.Modern;
                ulong count = BinaryPrimitives.ReadUInt64LittleEndian(data.Slice(7));
                if (count > int.MaxValue) throw new InvalidDataException("String table count is out of range");
                Count = (int)count;
            }
            else
            {
                throw new InvalidDataException("Unknown string table version " + data[4]);
            }
        }

        public StringTableFormat Format { get; }
        public int Count { get; }

        public Enumerator GetEnumerator() =>
            new Enumerator(_data, Format == StringTableFormat.Legacy ? LegacyHeader : ModernHeader, Count, Format);

        public bool TryGet(ulong key, out string text)
        {
            foreach (StringTableItem item in this)
            {
                if (item.Key == key)
                {
                    text = item.Text;
                    return true;
                }
            }
            text = string.Empty;
            return false;
        }

        private static void Require(ReadOnlySpan<byte> data, int length)
        {
            if (data.Length < length) throw new InvalidDataException("String table header is truncated");
        }

        public ref struct Enumerator
        {
            private readonly ReadOnlySpan<byte> _data;
            private readonly int _count;
            private readonly StringTableFormat _format;
            private int _position;
            private int _read;

            internal Enumerator(ReadOnlySpan<byte> data, int start, int count, StringTableFormat format)
            {
                _data = data;
                _position = start;
                _count = count;
                _format = format;
                _read = 0;
                Current = default;
            }

            public StringTableItem Current { get; private set; }

            public bool MoveNext()
            {
                if (_read >= _count) return false;
                ReadOnlySpan<byte> rest = _data.Slice(_position);
                ulong key;
                byte flags = 0;
                int header, textBytes;
                if (_format == StringTableFormat.Modern)
                {
                    if (rest.Length < 7) throw new InvalidDataException("String table entry is truncated");
                    key = BinaryPrimitives.ReadUInt32LittleEndian(rest);
                    flags = rest[4];
                    textBytes = BinaryPrimitives.ReadUInt16LittleEndian(rest.Slice(5));
                    header = 7;
                }
                else
                {
                    if (rest.Length < 12) throw new InvalidDataException("String table entry is truncated");
                    key = BinaryPrimitives.ReadUInt64LittleEndian(rest);
                    uint units = BinaryPrimitives.ReadUInt32LittleEndian(rest.Slice(8));
                    if (units > int.MaxValue / 2) throw new InvalidDataException("String length is out of range");
                    textBytes = (int)units * 2;
                    header = 12;
                }
                if (rest.Length - header < textBytes) throw new InvalidDataException("String table text is truncated");
                Current = new StringTableItem(key, flags, rest.Slice(header, textBytes), _format);
                _position += header + textBytes;
                _read++;
                return true;
            }
        }
    }

    /// <summary>One string of a <see cref="StringTableReader"/>, still in its encoded form.</summary>
    public readonly ref struct StringTableItem
    {
        internal StringTableItem(ulong key, byte flags, ReadOnlySpan<byte> encoded, StringTableFormat format)
        {
            Key = key;
            Flags = flags;
            EncodedText = encoded;
            Format = format;
        }

        /// <summary>64-bit key (legacy) or 32-bit hash widened to 64 bits (modern).</summary>
        public ulong Key { get; }
        /// <summary>Per-string flags (modern format only).</summary>
        public byte Flags { get; }
        /// <summary>The text bytes as stored: UTF-16LE (legacy) or UTF-8 (modern).</summary>
        public ReadOnlySpan<byte> EncodedText { get; }
        public StringTableFormat Format { get; }

        public string Text => Format == StringTableFormat.Legacy
            ? Encoding.Unicode.GetString(EncodedText)
            : Encoding.UTF8.GetString(EncodedText);
    }

    /// <summary>One editable string.</summary>
    public struct StringTableEntry
    {
        public StringTableEntry(ulong key, string text, byte flags = 0)
        {
            Key = key;
            Text = text ?? throw new ArgumentNullException(nameof(text));
            Flags = flags;
        }

        public ulong Key { get; set; }
        public string Text { get; set; }
        public byte Flags { get; set; }
    }

    /// <summary>
    /// An editable string table that keeps entry order and the unknown header fields, so an unchanged table
    /// is written back byte-for-byte.
    /// </summary>
    public sealed class StringTable : IEnumerable<StringTableEntry>
    {
        private readonly List<StringTableEntry> _entries = new List<StringTableEntry>();
        // header fields with no known meaning, kept for exact round trips
        private ushort _legacyA, _legacyB;
        private uint _legacyC;
        private byte _modernCompressed;
        private ushort _modernReserved;

        public StringTable(StringTableFormat format) => Format = format;

        /// <summary>Layout written by <see cref="ToArray"/>. Switching between formats converts the table.</summary>
        public StringTableFormat Format { get; set; }

        public int Count => _entries.Count;

        /// <summary>The entries in file order.</summary>
        public IReadOnlyList<StringTableEntry> Entries => _entries;

        /// <summary>Text of the first string with this key. Setting a missing key appends it.</summary>
        public string this[ulong key]
        {
            get => TryGetValue(key, out string text) ? text : throw new KeyNotFoundException("No string with key " + key.ToString("X"));
            set
            {
                int i = IndexOf(key);
                if (i >= 0) _entries[i] = new StringTableEntry(key, value, _entries[i].Flags);
                else _entries.Add(new StringTableEntry(key, value));
            }
        }

        public void Add(ulong key, string text, byte flags = 0) => _entries.Add(new StringTableEntry(key, text, flags));

        public bool Remove(ulong key)
        {
            int i = IndexOf(key);
            if (i < 0) return false;
            _entries.RemoveAt(i);
            return true;
        }

        public bool TryGetValue(ulong key, out string text)
        {
            int i = IndexOf(key);
            text = i >= 0 ? _entries[i].Text : string.Empty;
            return i >= 0;
        }

        private int IndexOf(ulong key)
        {
            for (int i = 0; i < _entries.Count; i++)
                if (_entries[i].Key == key) return i;
            return -1;
        }

        public static StringTable Parse(ReadOnlySpan<byte> data)
        {
            var reader = new StringTableReader(data);
            var table = new StringTable(reader.Format);
            if (reader.Format == StringTableFormat.Legacy)
            {
                table._legacyA = BinaryPrimitives.ReadUInt16LittleEndian(data.Slice(5));
                table._legacyB = BinaryPrimitives.ReadUInt16LittleEndian(data.Slice(11));
                table._legacyC = BinaryPrimitives.ReadUInt32LittleEndian(data.Slice(13));
            }
            else
            {
                table._modernCompressed = data[6];
                table._modernReserved = BinaryPrimitives.ReadUInt16LittleEndian(data.Slice(15));
            }
            foreach (StringTableItem item in reader)
                table._entries.Add(new StringTableEntry(item.Key, item.Text, item.Flags));
            return table;
        }

        public byte[] ToArray()
        {
            using var output = new MemoryStream();
            using (var w = new BinaryWriter(output, Encoding.UTF8, leaveOpen: true))
            {
                w.Write(StringTableReader.Magic);
                if (Format == StringTableFormat.Legacy) WriteLegacy(w);
                else WriteModern(w);
            }
            return output.ToArray();
        }

        private void WriteLegacy(BinaryWriter w)
        {
            w.Write((byte)2);
            w.Write(_legacyA);
            w.Write((uint)_entries.Count);
            w.Write(_legacyB);
            w.Write(_legacyC);
            foreach (StringTableEntry e in _entries)
            {
                w.Write(e.Key);
                w.Write(e.Text.Length);
                w.Write(Encoding.Unicode.GetBytes(e.Text));
            }
        }

        private void WriteModern(BinaryWriter w)
        {
            var encoded = new byte[_entries.Count][];
            uint textTotal = 0;
            for (int i = 0; i < _entries.Count; i++)
            {
                if (_entries[i].Key > uint.MaxValue)
                    throw new InvalidDataException("Key " + _entries[i].Key.ToString("X") + " does not fit the 32-bit keys of the modern format");
                encoded[i] = Encoding.UTF8.GetBytes(_entries[i].Text);
                if (encoded[i].Length > ushort.MaxValue)
                    throw new InvalidDataException("A string is longer than 65535 bytes, the modern format's limit");
                textTotal += (uint)encoded[i].Length + 1;
            }
            w.Write((ushort)5);
            w.Write(_modernCompressed);
            w.Write((ulong)_entries.Count);
            w.Write(_modernReserved);
            w.Write(textTotal);
            for (int i = 0; i < _entries.Count; i++)
            {
                w.Write((uint)_entries[i].Key);
                w.Write(_entries[i].Flags);
                w.Write((ushort)encoded[i].Length);
                w.Write(encoded[i]);
            }
        }

        public IEnumerator<StringTableEntry> GetEnumerator() => _entries.GetEnumerator();
        IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
    }
}
