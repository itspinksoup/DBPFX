using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Security;
using System.Text;
#if NET5_0_OR_GREATER
using System.Reflection;
using System.Runtime.CompilerServices;
#endif

namespace DBPFX.Native
{
    /// <summary>Bindings to the native engine (native/include/dbpfx/dbpfx.h).</summary>
    [SuppressUnmanagedCodeSecurity]
    internal static unsafe class Engine
    {
        internal const string Library = "DBPFX.Native";

        internal const int NotFound = -1, ErrArgument = -2, ErrIo = -3, ErrFormat = -4, ErrRange = -5;

        [StructLayout(LayoutKind.Sequential)]
        internal struct HeaderData
        {
            public uint Major, Minor, UserMajor, UserMinor, Unused1, Created, Updated, Unused2;
            public uint IndexCount, IndexPositionLow, IndexSize, IndexVersion, IndexPosition, IndexFlags;
        }

        // ------------------------------------------------------------------ packages
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern IntPtr dbpfx_version();
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern IntPtr dbpfx_last_error();
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern int dbpfx_open(byte* path, out PackageHandle package);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern int dbpfx_open_memory(byte* data, ulong length, out PackageHandle package);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern void dbpfx_close(IntPtr package);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern int dbpfx_header_get(PackageHandle package, out HeaderData header);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern byte* dbpfx_header_bytes(PackageHandle package);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern int dbpfx_detected_game(PackageHandle package);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern uint dbpfx_count(PackageHandle package);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern long dbpfx_entries(PackageHandle package, uint first, uint count, ResourceEntry* output);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern long dbpfx_find(PackageHandle package, uint type, uint group, ulong instance);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern long dbpfx_find_type(PackageHandle package, uint type, uint* output, uint capacity);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern int dbpfx_raw(PackageHandle package, uint index, out byte* data, out ulong length);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern int dbpfx_direct(PackageHandle package, uint index, out byte* data, out ulong length);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern int dbpfx_read_into(PackageHandle package, uint index, byte* destination, ulong length);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern void dbpfx_prefetch(PackageHandle package, uint index);

        // ------------------------------------------------------------------ editing
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern int dbpfx_editor_new(int game, out EditorHandle editor);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern int dbpfx_editor_open(PackageHandle package, int game, out EditorHandle editor);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern void dbpfx_editor_free(IntPtr editor);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern int dbpfx_editor_set_game(EditorHandle editor, int game);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern int dbpfx_editor_get_game(EditorHandle editor);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern int dbpfx_editor_set_policy(EditorHandle editor, int policy);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern uint dbpfx_editor_count(EditorHandle editor);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern uint dbpfx_editor_live_count(EditorHandle editor);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern int dbpfx_editor_slot_state(EditorHandle editor, uint slot);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern int dbpfx_editor_key(EditorHandle editor, uint slot, out uint type, out uint group, out ulong instance);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern long dbpfx_editor_find(EditorHandle editor, uint type, uint group, ulong instance);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern long dbpfx_editor_add(EditorHandle editor, uint type, uint group, ulong instance, byte* data, ulong length, int rejectDuplicates, int compress);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern int dbpfx_editor_replace(EditorHandle editor, uint slot, byte* data, ulong length);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern int dbpfx_editor_remove(EditorHandle editor, uint slot);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern int dbpfx_editor_set_key(EditorHandle editor, uint slot, uint type, uint group, ulong instance);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern int dbpfx_editor_set_compression(EditorHandle editor, uint slot, int compress);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern int dbpfx_editor_set_user_version(EditorHandle editor, uint major, uint minor);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern long dbpfx_editor_data_size(EditorHandle editor, uint slot);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern int dbpfx_editor_read(EditorHandle editor, uint slot, byte* destination, ulong length);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern int dbpfx_editor_save_as(EditorHandle editor, byte* path);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern int dbpfx_editor_save_in_place(EditorHandle editor);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern int dbpfx_editor_save_memory(EditorHandle editor, out IntPtr data, out ulong length);

        // ------------------------------------------------------------------ codecs
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern int dbpfx_decompress(byte* input, ulong inputLength, byte* output, ulong outputLength);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern int dbpfx_compress(int method, byte* input, ulong inputLength, out IntPtr output, out ulong outputLength);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern void dbpfx_free(IntPtr memory);

        // ------------------------------------------------------------------ helpers
        internal static string Utf8(IntPtr p)
        {
            if (p == IntPtr.Zero) return string.Empty;
            byte* b = (byte*)p;
            int n = 0;
            while (b[n] != 0) n++;
            return Encoding.UTF8.GetString(b, n);
        }

        /// <summary>NUL-terminated UTF-8 copy of a string, for paths.</summary>
        internal static byte[] Utf8Z(string s)
        {
            var bytes = new byte[Encoding.UTF8.GetByteCount(s) + 1];
            Encoding.UTF8.GetBytes(s, 0, s.Length, bytes, 0);
            return bytes;
        }

        internal static int Check(int status, string? context = null) => status >= 0 ? status : throw Failure(status, context);

        /// <summary>For the 64-bit calls, where -1 is a regular "not found" answer.</summary>
        internal static long Check(long status, string? context = null) => status >= NotFound ? status : throw Failure((int)status, context);

        internal static Exception Failure(int status, string? context)
        {
            string message = Utf8(dbpfx_last_error());
            if (context != null) message = context + ": " + message;
            switch (status)
            {
                case ErrArgument: return new ArgumentException(message);
                case ErrRange: return new ArgumentOutOfRangeException(null, message);
                case ErrFormat: return new InvalidDataException(message);
                case ErrIo:
                    if (Contains(message, "No such file") || Contains(message, "error 2)") || Contains(message, "error 3)"))
                        return new FileNotFoundException(message);
                    if (Contains(message, "Permission denied") || Contains(message, "error 5)"))
                        return new UnauthorizedAccessException(message);
                    return new IOException(message);
                default: return new PackageEngineException(message, status);
            }
        }

        private static bool Contains(string haystack, string needle) => haystack.IndexOf(needle, StringComparison.OrdinalIgnoreCase) >= 0;

#if NET5_0_OR_GREATER
        /// <summary>
        /// Finds the native engine next to the assembly or under runtimes/&lt;rid&gt;/native (NuGet layout),
        /// on top of the runtime's default probing.
        /// </summary>
#pragma warning disable CA2255 // a library-level module initializer is intended here
        [ModuleInitializer]
#pragma warning restore CA2255
        internal static void RegisterResolver()
        {
            NativeLibrary.SetDllImportResolver(typeof(Engine).Assembly, Resolve);
        }

        private static IntPtr Resolve(string name, Assembly assembly, DllImportSearchPath? searchPath)
        {
            if (name != Library) return IntPtr.Zero;
            if (NativeLibrary.TryLoad(name, assembly, searchPath, out IntPtr handle)) return handle;

            string directory = AppContext.BaseDirectory; // also correct for single-file and AOT apps
            string file = OperatingSystem.IsWindows() ? "DBPFX.Native.dll"
                        : OperatingSystem.IsMacOS() ? "libDBPFX.Native.dylib"
                        : "libDBPFX.Native.so";
            string os = OperatingSystem.IsWindows() ? "win" : OperatingSystem.IsMacOS() ? "osx" : "linux";
            string rid = os + "-" + RuntimeInformation.ProcessArchitecture.ToString().ToLowerInvariant();
            foreach (string candidate in new[] { Path.Combine(directory, "runtimes", rid, "native", file), Path.Combine(directory, file) })
                if (File.Exists(candidate) && NativeLibrary.TryLoad(candidate, out handle)) return handle;
            return IntPtr.Zero;
        }
#endif
    }

    /// <summary>Owns a native package. Reference-counted, so leased views can keep the mapping alive.</summary>
    internal sealed class PackageHandle : SafeHandle
    {
        public PackageHandle() : base(IntPtr.Zero, ownsHandle: true) { }
        public override bool IsInvalid => handle == IntPtr.Zero;
        protected override bool ReleaseHandle()
        {
            Engine.dbpfx_close(handle);
            return true;
        }
    }

    internal sealed class EditorHandle : SafeHandle
    {
        public EditorHandle() : base(IntPtr.Zero, ownsHandle: true) { }
        public override bool IsInvalid => handle == IntPtr.Zero;
        protected override bool ReleaseHandle()
        {
            Engine.dbpfx_editor_free(handle);
            return true;
        }
    }
}
