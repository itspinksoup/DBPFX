// DBPFX - internal package state (header-only so inline accessors stay inline)
#pragma once

#include <mutex>
#include <unordered_map>
#include <variant>

namespace dbpfx {

/// Byte offsets of every index-entry field, compiled once per package from the grammar.
/// -1 means "constant, taken from the index header".
struct index_layout {
    const std::byte* entries = nullptr;
    std::size_t count = 0;
    std::size_t stride = 0;
    std::uint32_t flags = 0;
    std::uint32_t const_type = 0, const_group = 0, const_instance_hi = 0;
    int off_type = -1, off_group = -1, off_instance_hi = -1;
    int off_instance_lo = 0, off_offset = 0, off_file_size = 0, off_mem_size = 0, off_compression = 0, off_committed = 0;

    [[nodiscard]] const std::byte* at(std::size_t i) const { return entries + i * stride; }
    [[nodiscard]] std::uint32_t type(const std::byte* e) const { return off_type < 0 ? const_type : load_le<std::uint32_t>(e + off_type); }
    [[nodiscard]] std::uint32_t group(const std::byte* e) const { return off_group < 0 ? const_group : load_le<std::uint32_t>(e + off_group); }
    [[nodiscard]] std::uint64_t instance(const std::byte* e) const {
        const std::uint64_t hi = off_instance_hi < 0 ? const_instance_hi : load_le<std::uint32_t>(e + off_instance_hi);
        return (hi << 32) | load_le<std::uint32_t>(e + off_instance_lo);
    }
};

struct package::state {
    std::variant<mapped_file, byte_vec> storage;
    std::filesystem::path path;
    byte_span bytes;
    index_layout layout;

    mutable std::once_flag lookup_once;
    mutable std::unordered_map<tgi, std::uint32_t, tgi_hash> lookup;

    [[nodiscard]] header_view header() const { return g::parse<schema::dbpf_header>(bytes); }
    [[nodiscard]] std::size_t index_position() const {
        const auto h = header();
        const std::uint32_t p = h.get<"index_position">();
        return p != 0 ? p : h.get<"index_position_low">();
    }
};

inline auto package::index_view() const {
    const auto pos = st_->index_position();
    const auto count = header().get<"index_count">();
    return g::parse<schema::dbpf_index>(st_->bytes.subspan(pos), g::root_ctx{{"index_count", count}});
}

}  // namespace dbpfx
