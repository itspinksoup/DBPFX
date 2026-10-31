// DBPFX - minimum supported platforms
//
// DBPFX refuses to run below a minimum operating system version and notes when the system is older
// than the recommended one, so that every report comes from a recent, supported environment. The
// limits are enforced when building (CMake, compiler and SDK checks) and at run time (here). Change
// them in one place: this header and the "requirements" block of native/CMakeLists.txt.
#pragma once

#include <stdexcept>
#include <string>

namespace dbpfx::requirements {

// Minimum: refused below this. Recommended: works below this, with a notice (see advisory()).

// Windows: minimum Windows 10 22H2 (build 19045), recommended Windows 11 (build 22000)
inline constexpr unsigned windows_major = 10;
inline constexpr unsigned windows_build = 19045;
inline constexpr unsigned windows_recommended_build = 22000;
inline constexpr const char* windows_name = "Windows 10 22H2 (build 19045)";
inline constexpr const char* windows_recommended_name = "Windows 11";

// Linux: minimum glibc 2.35 (Ubuntu 22.04, Debian 12 has 2.36, RHEL 9 has 2.34 and is not enough),
// recommended glibc 2.39 (Ubuntu 24.04, Debian 13, Fedora 40, RHEL 10)
inline constexpr unsigned glibc_major = 2;
inline constexpr unsigned glibc_minor = 35;
inline constexpr unsigned glibc_recommended_minor = 39;
inline constexpr const char* linux_name = "Linux with glibc 2.35 (Ubuntu 22.04, Debian 12, Fedora 36 or newer)";
inline constexpr const char* linux_recommended_name = "Linux with glibc 2.39 (Ubuntu 24.04, Debian 13, Fedora 40, RHEL 10)";

// macOS: minimum macOS 14 Sonoma (the oldest .NET 10 runs on), recommended macOS 15 Sequoia
inline constexpr unsigned macos_major = 14;
inline constexpr unsigned macos_recommended_major = 15;
inline constexpr const char* macos_name = "macOS 14";
inline constexpr const char* macos_recommended_name = "macOS 15";

/// Thrown when DBPFX is used on an operating system older than the minimum.
struct platform_error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

/// Readable description of the running system, e.g. "Windows build 26100".
[[nodiscard]] std::string current_platform();

/// Empty when the running system is supported, otherwise the reason it is not. Checked once.
[[nodiscard]] const std::string& unsupported_reason();

[[nodiscard]] inline bool platform_supported() { return unsupported_reason().empty(); }

/// Empty on a recommended system, otherwise a notice that the system is supported but older than
/// recommended (or, on an unsupported system, the same text as unsupported_reason()). Checked once.
[[nodiscard]] const std::string& advisory();

[[nodiscard]] inline bool platform_recommended() { return advisory().empty(); }

/// Throws platform_error on an unsupported system.
inline void require_supported_platform() {
    if (const auto& why = unsupported_reason(); !why.empty()) throw platform_error(why);
}

}  // namespace dbpfx::requirements
