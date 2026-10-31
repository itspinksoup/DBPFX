// DBPFX - unified DBPF package engine (The Sims 3 / The Sims Medieval / The Sims 4)
#pragma once

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace dbpfx {

using byte_span = std::span<const std::byte>;
using byte_vec = std::vector<std::byte>;

/// Thrown whenever bytes do not match what the grammar expects.
struct format_error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

/// Compile-time string usable as a non-type template parameter (field names, magics).
template <std::size_t N>
struct fixed_string {
    char data[N]{};
    constexpr fixed_string(const char (&s)[N]) { std::copy_n(s, N, data); }
    [[nodiscard]] constexpr std::string_view view() const { return {data, N - 1}; }
    [[nodiscard]] static constexpr std::size_t size() { return N - 1; }
};

template <fixed_string A, fixed_string B>
inline constexpr bool same_name = A.view() == B.view();

// ---------------------------------------------------------------- little-endian access
// DBPF is little-endian on every platform the games shipped on. memcpy keeps reads
// alignment-safe and compiles down to single loads.
template <class T>
[[nodiscard]] inline T load_le(const std::byte* p) noexcept {
    static_assert(std::is_trivially_copyable_v<T>);
    T v;
    std::memcpy(&v, p, sizeof(T));
    if constexpr (std::endian::native == std::endian::big && sizeof(T) > 1) {
        auto* b = reinterpret_cast<unsigned char*>(&v);
        std::reverse(b, b + sizeof(T));
    }
    return v;
}

template <class T>
inline void store_le(std::byte* p, T v) noexcept {
    if constexpr (std::endian::native == std::endian::big && sizeof(T) > 1) {
        auto* b = reinterpret_cast<unsigned char*>(&v);
        std::reverse(b, b + sizeof(T));
    }
    std::memcpy(p, &v, sizeof(T));
}

inline void need(const std::byte* p, const std::byte* end, std::size_t n, const char* what) {
    if (p > end || static_cast<std::size_t>(end - p) < n)
        throw format_error(std::string("unexpected end of data while reading ") + what);
}

/// Append-only binary writer used by every serializer.
class writer {
public:
    explicit writer(byte_vec& out) : out_(out) {}

    template <class T>
    void put(T v) {
        const auto at = out_.size();
        out_.resize(at + sizeof(T));
        store_le<T>(out_.data() + at, v);
    }
    void put_bytes(const void* p, std::size_t n) {
        const auto* b = static_cast<const std::byte*>(p);
        out_.insert(out_.end(), b, b + n);
    }
    void put_zeros(std::size_t n) { out_.resize(out_.size() + n); }
    template <class T>
    void patch(std::size_t at, T v) { store_le<T>(out_.data() + at, v); }
    [[nodiscard]] std::size_t pos() const { return out_.size(); }
    [[nodiscard]] byte_vec& buffer() { return out_; }

private:
    byte_vec& out_;
};

inline constexpr std::uint32_t fourcc(std::string_view s) {
    std::uint32_t v = 0;
    for (std::size_t i = 0; i < 4 && i < s.size(); ++i) v |= std::uint32_t(static_cast<unsigned char>(s[i])) << (8 * i);
    return v;
}

}  // namespace dbpfx
