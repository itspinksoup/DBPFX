// DBPFX - read-only memory-mapped file
#pragma once

#include "platform.hpp"

#include <filesystem>
#include <memory>

namespace dbpfx {

/// Maps a whole file into the address space, read-only. Pages are faulted in by the OS only
/// when touched, so opening a 2 GB package costs the same as opening a 2 KB one.
class mapped_file {
public:
    mapped_file() = default;
    explicit mapped_file(const std::filesystem::path& path);
    ~mapped_file();

    mapped_file(const mapped_file&) = delete;
    mapped_file& operator=(const mapped_file&) = delete;
    mapped_file(mapped_file&& o) noexcept { swap(o); }
    mapped_file& operator=(mapped_file&& o) noexcept {
        if (this != &o) {
            close();
            swap(o);
        }
        return *this;
    }

    void close() noexcept;
    [[nodiscard]] byte_span bytes() const { return {data_, size_}; }
    [[nodiscard]] const std::byte* data() const { return data_; }
    [[nodiscard]] std::size_t size() const { return size_; }
    [[nodiscard]] bool is_open() const { return opened_; }

    /// Hint the OS that [offset, offset+len) will be read soon / sequentially.
    void prefetch(std::size_t offset, std::size_t len) const noexcept;

private:
    void swap(mapped_file& o) noexcept {
        std::swap(data_, o.data_);
        std::swap(size_, o.size_);
        std::swap(opened_, o.opened_);
#ifdef _WIN32
        std::swap(file_, o.file_);
        std::swap(mapping_, o.mapping_);
#endif
    }

    const std::byte* data_ = nullptr;
    std::size_t size_ = 0;
    bool opened_ = false;
#ifdef _WIN32
    void* file_ = nullptr;
    void* mapping_ = nullptr;
#endif
};

}  // namespace dbpfx
