#include "dbpfx/mapped_file.hpp"

#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#endif

namespace dbpfx {

#ifdef _WIN32

static std::string win_error(const char* what) {
    return std::string(what) + " failed (Win32 error " + std::to_string(::GetLastError()) + ")";
}

mapped_file::mapped_file(const std::filesystem::path& path) {
    // Share everything: other tools (and the game) may keep the package open, and we never
    // write through this mapping.
    HANDLE f = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS, nullptr);
    if (f == INVALID_HANDLE_VALUE) throw std::runtime_error(win_error("CreateFileW"));
    LARGE_INTEGER sz{};
    if (!::GetFileSizeEx(f, &sz)) {
        ::CloseHandle(f);
        throw std::runtime_error(win_error("GetFileSizeEx"));
    }
    file_ = f;
    size_ = static_cast<std::size_t>(sz.QuadPart);
    opened_ = true;
    if (size_ == 0) return;  // cannot map an empty file; bytes() is simply empty

    HANDLE m = ::CreateFileMappingW(f, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!m) {
        close();
        throw std::runtime_error(win_error("CreateFileMappingW"));
    }
    mapping_ = m;
    void* v = ::MapViewOfFile(m, FILE_MAP_READ, 0, 0, 0);
    if (!v) {
        close();
        throw std::runtime_error(win_error("MapViewOfFile"));
    }
    data_ = static_cast<const std::byte*>(v);
}

void mapped_file::close() noexcept {
    if (data_) ::UnmapViewOfFile(data_);
    if (mapping_) ::CloseHandle(static_cast<HANDLE>(mapping_));
    if (file_) ::CloseHandle(static_cast<HANDLE>(file_));
    data_ = nullptr;
    mapping_ = nullptr;
    file_ = nullptr;
    size_ = 0;
    opened_ = false;
}

void mapped_file::prefetch(std::size_t offset, std::size_t len) const noexcept {
    if (!data_ || offset >= size_) return;
    len = std::min(len, size_ - offset);
#if defined(_WIN32_WINNT) && _WIN32_WINNT >= 0x0602
    WIN32_MEMORY_RANGE_ENTRY r{const_cast<std::byte*>(data_ + offset), len};
    ::PrefetchVirtualMemory(::GetCurrentProcess(), 1, &r, 0);
#else
    (void)len;
#endif
}

#else  // POSIX

mapped_file::mapped_file(const std::filesystem::path& path) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) throw std::runtime_error("open('" + path.string() + "'): " + std::strerror(errno));
    struct stat st{};
    if (::fstat(fd, &st) != 0) {
        const int e = errno;
        ::close(fd);
        throw std::runtime_error(std::string("fstat: ") + std::strerror(e));
    }
    size_ = static_cast<std::size_t>(st.st_size);
    opened_ = true;
    if (size_ > 0) {
        void* v = ::mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd, 0);
        if (v == MAP_FAILED) {
            const int e = errno;
            ::close(fd);
            size_ = 0;
            opened_ = false;
            throw std::runtime_error(std::string("mmap: ") + std::strerror(e));
        }
        data_ = static_cast<const std::byte*>(v);
        ::madvise(v, size_, MADV_RANDOM);
    }
    ::close(fd);  // the mapping keeps the file alive
}

void mapped_file::close() noexcept {
    if (data_) ::munmap(const_cast<std::byte*>(data_), size_);
    data_ = nullptr;
    size_ = 0;
    opened_ = false;
}

void mapped_file::prefetch(std::size_t offset, std::size_t len) const noexcept {
    if (!data_ || offset >= size_) return;
    const auto page = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
    const std::size_t start = offset & ~(page - 1);
    len = std::min(len, size_ - offset) + (offset - start);
    ::madvise(const_cast<std::byte*>(data_ + start), len, MADV_WILLNEED);
}

#endif

mapped_file::~mapped_file() { close(); }

}  // namespace dbpfx
