// DBPFX - run-time check of the operating system version (see requirements.hpp)
#include "dbpfx/requirements.hpp"

#include <cstdio>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#elif defined(__linux__)
#include <features.h>
#if defined(__GLIBC__)
#include <gnu/libc-version.h>
#endif
#endif

// ---------------------------------------------------------------- build-time floor
// The build machine must already be at least as new as the minimum (CMake checks this too, with a
// friendlier message); this keeps a hand-made build from slipping past it.
#if defined(__linux__)
#if !defined(__GLIBC__)
#error "DBPFX requires glibc (musl and other C libraries are not supported)"
#elif __GLIBC__ < 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ < 35)
#error "DBPFX requires glibc 2.35 or newer to build"
#endif
#endif

namespace dbpfx::requirements {
namespace {

#if defined(_WIN32)
struct windows_version {
    unsigned major = 0, minor = 0, build = 0;
};
// GetVersionEx reports what the application manifest allows; RtlGetVersion reports the real version.
windows_version query_windows() {
    windows_version v;
    using rtl_get_version = LONG(WINAPI*)(OSVERSIONINFOW*);
    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
        if (auto fn = reinterpret_cast<rtl_get_version>(reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion")))) {
            OSVERSIONINFOW info{};
            info.dwOSVersionInfoSize = sizeof info;
            if (fn(&info) == 0) v = {info.dwMajorVersion, info.dwMinorVersion, info.dwBuildNumber};
        }
    }
    return v;
}
#elif defined(__APPLE__)
std::string macos_product_version() {
    char buffer[64]{};
    std::size_t size = sizeof buffer;
    if (sysctlbyname("kern.osproductversion", buffer, &size, nullptr, 0) != 0) return {};
    return buffer;
}
#endif

std::string compute_advisory() {
    if (!unsupported_reason().empty()) return unsupported_reason();
    const char* recommended = nullptr;
#if defined(_WIN32)
    if (query_windows().build < windows_recommended_build) recommended = windows_recommended_name;
#elif defined(__APPLE__)
    unsigned major = 0;
    std::sscanf(macos_product_version().c_str(), "%u", &major);
    if (major < macos_recommended_major) recommended = macos_recommended_name;
#elif defined(__linux__) && defined(__GLIBC__)
    unsigned major = 0, minor = 0;
    std::sscanf(gnu_get_libc_version(), "%u.%u", &major, &minor);
    if (major == glibc_major && minor < glibc_recommended_minor) recommended = linux_recommended_name;
#endif
    if (!recommended) return {};
    return current_platform() + " is supported, but " + recommended + " or newer is recommended";
}

std::string compute_reason() {
#if defined(_WIN32)
    const auto v = query_windows();
    if (v.major > windows_major || (v.major == windows_major && v.build >= windows_build)) return {};
    return "DBPFX requires " + std::string(windows_name) + " or newer (" + windows_recommended_name +
           " recommended); this system is " + current_platform();
#elif defined(__APPLE__)
    unsigned major = 0;
    std::sscanf(macos_product_version().c_str(), "%u", &major);
    if (major >= macos_major) return {};
    return "DBPFX requires " + std::string(macos_name) + " or newer (" + macos_recommended_name +
           " recommended); this system is " + current_platform();
#elif defined(__linux__) && defined(__GLIBC__)
    unsigned major = 0, minor = 0;
    std::sscanf(gnu_get_libc_version(), "%u.%u", &major, &minor);
    if (major > glibc_major || (major == glibc_major && minor >= glibc_minor)) return {};
    return "DBPFX requires " + std::string(linux_name) + "; this system is " + current_platform();
#else
    return "DBPFX does not support this operating system (supported: " + std::string(windows_name) + ", " +
           linux_name + ", " + macos_name + ")";
#endif
}

}  // namespace

std::string current_platform() {
#if defined(_WIN32)
    const auto v = query_windows();
    return "Windows " + std::to_string(v.major) + "." + std::to_string(v.minor) + " build " + std::to_string(v.build);
#elif defined(__APPLE__)
    const auto version = macos_product_version();
    return "macOS " + (version.empty() ? std::string("(unknown version)") : version);
#elif defined(__linux__) && defined(__GLIBC__)
    return std::string("Linux, glibc ") + gnu_get_libc_version();
#else
    return "an unsupported operating system";
#endif
}

const std::string& unsupported_reason() {
    static const std::string reason = compute_reason();
    return reason;
}

const std::string& advisory() {
    static const std::string text = compute_advisory();
    return text;
}

}  // namespace dbpfx::requirements
