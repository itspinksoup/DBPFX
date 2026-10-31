using System;
using System.Reflection;
using System.Runtime.InteropServices;
using static DBPFX.Native.Engine;

namespace DBPFX
{
    /// <summary>
    /// Minimum supported software. DBPFX only runs on current systems and runtimes, so every report comes
    /// from a recent, supported setup:
    /// <list type="bullet">
    /// <item>Windows 10 22H2 (build 19045), Linux with glibc 2.35, macOS 14 - checked by the native engine.
    /// Windows 11, glibc 2.39 and macOS 15 are recommended: older supported systems work, and
    /// <see cref="Advisory"/> says so.</item>
    /// <item>.NET 10 or newer; through the .NET Standard 2.1 build also Unity 6 or Mono 6.12 - checked here</item>
    /// </list>
    /// Every entry point (<see cref="PackageFile.Open"/>, <see cref="PackageFile.Load(ReadOnlySpan{byte})"/>,
    /// <see cref="Codec"/>) throws <see cref="PlatformNotSupportedException"/> on anything older.
    /// </summary>
    public static class Requirements
    {
        public const int MinimumDotNet = 10;
        public const int MinimumUnity = 6000;      // Unity 6
        public static readonly Version MinimumMono = new Version(6, 12);

        private static readonly object Gate = new object();
        private static bool _checked;
        private static string? _reason;

        /// <summary>True when the operating system and the .NET runtime meet the minimum.</summary>
        public static bool IsSupported => UnsupportedReason == null;

        /// <summary>Why this system is not supported, or null when it is.</summary>
        public static string? UnsupportedReason
        {
            get
            {
                lock (Gate)
                {
                    if (!_checked)
                    {
                        _reason = RuntimeProblem() ?? OperatingSystemProblem();
                        _checked = true;
                    }
                    return _reason;
                }
            }
        }

        /// <summary>True when the system is supported and at least the recommended version (e.g. Windows 11).</summary>
        public static bool IsRecommended => IsSupported && Advisory == null;

        /// <summary>
        /// Null on a recommended system; otherwise a notice that the system works but is older than
        /// recommended (or, on an unsupported one, why it is not supported).
        /// </summary>
        public static string? Advisory
        {
            get
            {
                string? reason = UnsupportedReason;
                if (reason != null) return reason;
                string text = Utf8(dbpfx_platform_advisory());
                return text.Length == 0 ? null : text;
            }
        }

        /// <summary>The running runtime and operating system, as DBPFX sees them.</summary>
        public static string CurrentPlatform => RuntimeInformation.FrameworkDescription + " on " + RuntimeInformation.OSDescription;

        /// <exception cref="PlatformNotSupportedException"/>
        public static void EnsureSupported()
        {
            string? reason = UnsupportedReason;
            if (reason != null) throw new PlatformNotSupportedException(reason);
        }

        private static string? RuntimeProblem()
        {
            string framework = RuntimeInformation.FrameworkDescription;

            // Unity (Mono or IL2CPP): the engine version is the meaningful one
            Type? application = Type.GetType("UnityEngine.Application, UnityEngine.CoreModule", throwOnError: false);
            if (application != null)
            {
                string? unity = application.GetProperty("unityVersion", BindingFlags.Public | BindingFlags.Static)?.GetValue(null) as string;
                int major = LeadingNumber(unity);
                return major >= MinimumUnity ? null
                    : "DBPFX requires Unity 6 or newer; this is Unity " + (unity ?? "(unknown version)");
            }

            if (framework.StartsWith("Mono", StringComparison.OrdinalIgnoreCase))
            {
                Version? mono = ParseVersion(framework.Substring(4));
                return mono != null && mono >= MinimumMono ? null
                    : "DBPFX requires Mono " + MinimumMono + " or newer; this is " + framework;
            }

            if (framework.StartsWith(".NET Framework", StringComparison.OrdinalIgnoreCase))
                return "DBPFX does not run on .NET Framework; use .NET " + MinimumDotNet + " or newer";

            // .NET Core 3.x reports ".NET Core", .NET 5+ reports ".NET"; Environment.Version is the runtime version
            return Environment.Version.Major >= MinimumDotNet ? null
                : "DBPFX requires .NET " + MinimumDotNet + " or newer; this is " + framework;
        }

        private static string? OperatingSystemProblem()
        {
            // a missing native library is not cached: DllNotFoundException reaches the caller as is
            try
            {
                return dbpfx_platform_check() == 0 ? null : Utf8(dbpfx_last_error());
            }
            catch (EntryPointNotFoundException)
            {
                return "The native engine (DBPFX.Native) is older than this assembly; rebuild both together";
            }
        }

        private static int LeadingNumber(string? text)
        {
            int value = 0, i = 0;
            if (text == null) return 0;
            while (i < text.Length && char.IsDigit(text[i])) value = value * 10 + (text[i++] - '0');
            return value;
        }

        private static Version? ParseVersion(string text)
        {
            text = text.Trim();
            int end = 0;
            while (end < text.Length && (char.IsDigit(text[end]) || text[end] == '.')) end++;
            return Version.TryParse(text.Substring(0, end).TrimEnd('.'), out Version? v) ? v : null;
        }
    }
}
