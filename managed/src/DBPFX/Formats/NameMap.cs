using System;
using System.Buffers.Binary;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace DBPFX.Formats
{
    /// <summary>Reads a name map (type 0x0166038C: instance number to name) in place.</summary>
    public readonly ref struct NameMapReader
    {
        private readonly ReadOnlySpan<byte> _data;

        public NameMapReader(ReadOnlySpan<byte> data)
        {
            if (data.Length < 8) throw new InvalidDataException("Name map is truncated");
            uint count = BinaryPrimitives.ReadUInt32LittleEndian(data.Slice(4));
            if (count > (uint)(data.Length - 8) / 12) throw new InvalidDataException("Name map count exceeds its data");
            _data = data;
            Version = BinaryPrimitives.ReadUInt32LittleEndian(data);
            Count = (int)count;
        }

        public uint Version { get; }
        public int Count { get; }

        public Enumerator GetEnumerator() => new Enumerator(_data, Count);

        public bool TryGet(ulong instance, out string name)
        {
            foreach (NameMapItem item in this)
            {
                if (item.Instance == instance)
                {
                    name = item.Name;
                    return true;
                }
            }
            name = string.Empty;
            return false;
        }

        public ref struct Enumerator
        {
            private readonly ReadOnlySpan<byte> _data;
            private readonly int _count;
            private int _position;
            private int _read;

            internal Enumerator(ReadOnlySpan<byte> data, int count)
            {
                _data = data;
                _count = count;
                _position = 8;
                _read = 0;
                Current = default;
            }

            public NameMapItem Current { get; private set; }

            public bool MoveNext()
            {
                if (_read >= _count) return false;
                ReadOnlySpan<byte> rest = _data.Slice(_position);
                if (rest.Length < 12) throw new InvalidDataException("Name map entry is truncated");
                ulong instance = BinaryPrimitives.ReadUInt64LittleEndian(rest);
                uint length = BinaryPrimitives.ReadUInt32LittleEndian(rest.Slice(8));
                if (length > (uint)(rest.Length - 12)) throw new InvalidDataException("Name map text is truncated");
                Current = new NameMapItem(instance, rest.Slice(12, (int)length));
                _position += 12 + (int)length;
                _read++;
                return true;
            }
        }
    }

    public readonly ref struct NameMapItem
    {
        internal NameMapItem(ulong instance, ReadOnlySpan<byte> encoded)
        {
            Instance = instance;
            EncodedName = encoded;
        }

        public ulong Instance { get; }
        /// <summary>The name as stored (UTF-8).</summary>
        public ReadOnlySpan<byte> EncodedName { get; }
        public string Name => Encoding.UTF8.GetString(EncodedName);
    }

    /// <summary>An editable name map. Keeps insertion order; the first name wins for duplicate instances.</summary>
    public sealed class NameMap : IEnumerable<KeyValuePair<ulong, string>>
    {
        private readonly List<ulong> _order = new List<ulong>();
        private readonly Dictionary<ulong, string> _names = new Dictionary<ulong, string>();

        public uint Version { get; set; } = 1;
        public int Count => _order.Count;

        public string this[ulong instance]
        {
            get => _names[instance];
            set
            {
                if (value == null) throw new ArgumentNullException(nameof(value));
                if (!_names.ContainsKey(instance)) _order.Add(instance);
                _names[instance] = value;
            }
        }

        public bool TryGetValue(ulong instance, out string name)
        {
            if (_names.TryGetValue(instance, out var found))
            {
                name = found;
                return true;
            }
            name = string.Empty;
            return false;
        }

        public bool Remove(ulong instance)
        {
            if (!_names.Remove(instance)) return false;
            _order.Remove(instance);
            return true;
        }

        public static NameMap Parse(ReadOnlySpan<byte> data)
        {
            var reader = new NameMapReader(data);
            var map = new NameMap { Version = reader.Version };
            foreach (NameMapItem item in reader)
                if (!map._names.ContainsKey(item.Instance)) map[item.Instance] = item.Name;
            return map;
        }

        public byte[] ToArray()
        {
            using var output = new MemoryStream();
            using (var w = new BinaryWriter(output, Encoding.UTF8, leaveOpen: true))
            {
                w.Write(Version);
                w.Write(_order.Count);
                foreach (ulong instance in _order)
                {
                    byte[] name = Encoding.UTF8.GetBytes(_names[instance]);
                    w.Write(instance);
                    w.Write(name.Length);
                    w.Write(name);
                }
            }
            return output.ToArray();
        }

        public IEnumerator<KeyValuePair<ulong, string>> GetEnumerator()
        {
            foreach (ulong instance in _order) yield return new KeyValuePair<ulong, string>(instance, _names[instance]);
        }

        IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
    }
}
