using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using DBPFX.Native;
using static DBPFX.Native.Engine;

namespace DBPFX
{
    /// <summary>
    /// Builds or changes a package, addressed by <see cref="ResourceKey"/>. Edits are copy-on-write in the
    /// native engine: nothing is written until a Save call, and resources you do not touch are copied
    /// byte-for-byte from the source file without being decompressed.
    /// </summary>
    public sealed unsafe class PackageWriter : IDisposable
    {
        private readonly EditorHandle _handle;

        internal PackageWriter(PackageHandle source, Game? target)
        {
            Check(dbpfx_editor_open(source, target.HasValue ? (int)target.Value : -1, out _handle));
            Check(dbpfx_editor_set_policy(_handle, 0)); // "keep": the Storage argument decides per call
        }

        private PackageWriter(Game game)
        {
            Check(dbpfx_editor_new((int)game, out _handle));
            Check(dbpfx_editor_set_policy(_handle, 0));
        }

        /// <summary>An empty package for <paramref name="game"/> (DBPF 2.0 for TS3 and Medieval, 2.1 for TS4).</summary>
        public static PackageWriter Create(Game game) => new PackageWriter(game);

        /// <summary>The game whose format is written: header version and compression codec.</summary>
        public Game TargetGame
        {
            get => (Game)Check(dbpfx_editor_get_game(_handle));
            set => Check(dbpfx_editor_set_game(_handle, (int)value));
        }

        /// <summary>Number of resources that will be written (TS4 deleted markers not counted).</summary>
        public int Count => (int)dbpfx_editor_live_count(_handle);

        public bool Contains(ResourceKey key) => SlotOf(key) >= 0;

        /// <summary>Keys of all resources that will be written, in output order.</summary>
        public IReadOnlyList<ResourceKey> Keys
        {
            get
            {
                uint slots = dbpfx_editor_count(_handle);
                var keys = new List<ResourceKey>((int)slots);
                for (uint i = 0; i < slots; i++)
                {
                    if (Check(dbpfx_editor_slot_state(_handle, i)) != 0) continue;
                    Check(dbpfx_editor_key(_handle, i, out uint type, out uint group, out ulong instance));
                    keys.Add(new ResourceKey(type, group, instance));
                }
                return keys;
            }
        }

        /// <summary>Add the resource, or replace its data if the key already exists.</summary>
        public void Set(ResourceKey key, ReadOnlySpan<byte> data, Storage storage = Storage.Auto)
        {
            int slot = SlotOf(key);
            if (slot < 0)
            {
                Insert(key, data, storage, rejectDuplicate: false);
                return;
            }
            fixed (byte* p = data)
                Check(dbpfx_editor_replace(_handle, (uint)slot, p, (ulong)data.Length));
            if (storage != Storage.Auto)
                Check(dbpfx_editor_set_compression(_handle, (uint)slot, storage == Storage.Compressed ? 1 : 0));
        }

        /// <summary>Add a resource. Returns false (and changes nothing) if the key already exists.</summary>
        public bool Add(ResourceKey key, ReadOnlySpan<byte> data, Storage storage = Storage.Auto) =>
            Insert(key, data, storage, rejectDuplicate: true);

        /// <summary>Remove a resource (every copy, if the source package had duplicates). False if absent.</summary>
        public bool Remove(ResourceKey key)
        {
            bool removed = false;
            for (int slot = SlotOf(key); slot >= 0; slot = SlotOf(key))
            {
                Check(dbpfx_editor_remove(_handle, (uint)slot));
                removed = true;
            }
            return removed;
        }

        /// <summary>Give a resource a new key, keeping its data. False if <paramref name="from"/> is absent.</summary>
        /// <exception cref="InvalidOperationException"><paramref name="to"/> is already in use.</exception>
        public bool Rename(ResourceKey from, ResourceKey to)
        {
            int slot = SlotOf(from);
            if (slot < 0) return false;
            if (from == to) return true;
            if (Contains(to)) throw new InvalidOperationException("A resource with key " + to + " already exists");
            Check(dbpfx_editor_set_key(_handle, (uint)slot, to.Type, to.Group, to.Instance));
            return true;
        }

        /// <summary>Change how an existing resource is stored. False if absent.</summary>
        public bool SetStorage(ResourceKey key, Storage storage)
        {
            int slot = SlotOf(key);
            if (slot < 0) return false;
            if (storage != Storage.Auto)
                Check(dbpfx_editor_set_compression(_handle, (uint)slot, storage == Storage.Compressed ? 1 : 0));
            return true;
        }

        /// <summary>Current (uncompressed) data of a resource, including pending changes.</summary>
        /// <exception cref="KeyNotFoundException"/>
        public byte[] ReadAllBytes(ResourceKey key)
        {
            int slot = SlotOf(key);
            if (slot < 0) throw new KeyNotFoundException("No resource " + key);
            long size = Check(dbpfx_editor_data_size(_handle, (uint)slot));
            var bytes = new byte[size];
            fixed (byte* p = bytes)
                Check(dbpfx_editor_read(_handle, (uint)slot, p, (ulong)size));
            return bytes;
        }

        public void SetUserVersion(uint major, uint minor) => Check(dbpfx_editor_set_user_version(_handle, major, minor));

        // ================================================================== saving
        /// <summary>Write the package to a file. The output is streamed; it never sits in memory whole.</summary>
        public void Save(string path)
        {
            if (path == null) throw new ArgumentNullException(nameof(path));
            string full = Path.GetFullPath(path);
            byte[] utf8 = Utf8Z(full);
            fixed (byte* p = utf8)
                Check(dbpfx_editor_save_as(_handle, p), full);
        }

        public void Save(Stream stream)
        {
            if (stream == null) throw new ArgumentNullException(nameof(stream));
            Check(dbpfx_editor_save_memory(_handle, out IntPtr data, out ulong length));
            try
            {
                stream.Write(new ReadOnlySpan<byte>((void*)data, checked((int)length)));
                stream.Flush();
            }
            finally
            {
                dbpfx_free(data);
            }
        }

        public byte[] ToArray()
        {
            Check(dbpfx_editor_save_memory(_handle, out IntPtr data, out ulong length));
            try
            {
                var bytes = new byte[checked((int)length)];
                Marshal.Copy(data, bytes, 0, bytes.Length);
                return bytes;
            }
            finally
            {
                dbpfx_free(data);
            }
        }

        /// <summary>
        /// Overwrite the source file (written to a temporary file first, then swapped in) and continue editing
        /// the result. On Windows, dispose every <see cref="PackageFile"/> and <see cref="ResourceLease"/> on
        /// that file first: a mapped file cannot be replaced.
        /// </summary>
        public void SaveInPlace() => Check(dbpfx_editor_save_in_place(_handle));

        public void Dispose() => _handle.Dispose();

        // ================================================================== internals
        private int SlotOf(ResourceKey key) => (int)Check(dbpfx_editor_find(_handle, key.Type, key.Group, key.Instance));

        private bool Insert(ResourceKey key, ReadOnlySpan<byte> data, Storage storage, bool rejectDuplicate)
        {
            fixed (byte* p = data)
            {
                long slot = Check(dbpfx_editor_add(_handle, key.Type, key.Group, key.Instance, p, (ulong)data.Length,
                                                     rejectDuplicate ? 1 : 0, storage == Storage.Uncompressed ? 0 : 1));
                return slot >= 0;
            }
        }
    }
}
