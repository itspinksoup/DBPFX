// DBPFX - lazy, zero-copy DBPF package
#pragma once

#include "compression.hpp"
#include "mapped_file.hpp"
#include "schema.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <string>
#include <vector>

namespace dbpfx {

// ============================================================================ games
/// Container dialect. The Sims 3 and The Sims Medieval share DBPF 2.0 byte-for-byte;
/// The Sims 4 uses DBPF 2.1 and zlib instead of RefPack for new data.
enum class game : std::uint8_t { sims3 = 0, medieval = 1, sims4 = 2 };

struct game_profile {
    std::uint32_t major;
    std::uint32_t minor;
    compression::kind write_compression;  // used for resources we (re)compress
};
[[nodiscard]] constexpr game_profile profile_of(game g) {
    return g == game::sims4 ? game_profile{2, 1, compression::kind::zlib} : game_profile{2, 0, compression::kind::refpack};
}
[[nodiscard]] const char* game_name(game g);
/// Can this game read data stored with compression kind `c`? (TS3/TSM have no zlib.)
[[nodiscard]] constexpr bool game_reads(game g, std::uint16_t c) {
    if (c == 0 || c == 0xFFFF || c == 0xFFE0) return true;
    return g == game::sims4 && (c == 0x5A42 || c == 0xFFFE);
}

// ============================================================================ keys & entries
struct tgi {
    std::uint32_t type = 0;
    std::uint32_t group = 0;
    std::uint64_t instance = 0;
    friend bool operator==(const tgi&, const tgi&) = default;
    [[nodiscard]] std::string str() const;  // "TTTTTTTT:GGGGGGGG:IIIIIIIIIIIIIIII"
};
struct tgi_hash {
    std::size_t operator()(const tgi& k) const noexcept {
        std::uint64_t h = k.instance * 0x9E3779B97F4A7C15ull;
        h ^= (std::uint64_t(k.type) << 32 | k.group) + 0x632BE59BD9B4E019ull + (h << 6) + (h >> 2);
        return static_cast<std::size_t>(h ^ (h >> 31));
    }
};

/// Plain decoded index record (40 bytes, C-compatible). Produced on demand, never stored.
struct entry_info {
    tgi key;
    std::uint32_t offset = 0;
    std::uint32_t file_size = 0;  // without the 0x80000000 "extended" bit
    std::uint32_t mem_size = 0;
    std::uint16_t compression = 0;
    std::uint16_t committed = 1;
    [[nodiscard]] bool is_compressed() const { return compression != 0 && compression != 0xFFE0; }
    [[nodiscard]] bool is_deleted_record() const {
        return compression == 0xFFE0 || (file_size == 1 && mem_size == 0xFFFFFFFFu);
    }
};

// ============================================================================ resource bytes
/// Bytes of one resource. Uncompressed resources point straight into the mapped file
/// (zero-copy); compressed ones own a freshly decoded buffer. Either way the blob keeps the
/// backing storage alive, so it stays valid even after the package object is gone.
class blob {
public:
    blob() = default;
    blob(byte_span v, std::shared_ptr<const void> keep) : view_(v), keep_(std::move(keep)) {}
    explicit blob(byte_vec owned) {
        auto p = std::make_shared<byte_vec>(std::move(owned));
        view_ = {p->data(), p->size()};
        keep_ = std::move(p);
        owned_ = true;
    }
    [[nodiscard]] byte_span bytes() const { return view_; }
    [[nodiscard]] const std::byte* data() const { return view_.data(); }
    [[nodiscard]] std::size_t size() const { return view_.size(); }
    [[nodiscard]] bool zero_copy() const { return !owned_ && keep_ != nullptr; }
    [[nodiscard]] byte_vec copy() const { return byte_vec(view_.begin(), view_.end()); }

private:
    byte_span view_{};
    std::shared_ptr<const void> keep_;
    bool owned_ = false;
};

// ============================================================================ package
using header_view = g::view<schema::dbpf_header, g::root_ctx>;

/// Read-only package. Opening maps the file and validates the 96-byte header; nothing else
/// is touched until asked for. Cheap to copy (shared state), safe to read from many threads.
class package {
public:
    package() = default;
    static package open(const std::filesystem::path& path);
    static package from_memory(byte_vec bytes);

    [[nodiscard]] bool valid() const { return st_ != nullptr; }
    [[nodiscard]] const std::filesystem::path& path() const;
    [[nodiscard]] byte_span file_bytes() const;

    // --- header (lazy grammar view over the mapped bytes)
    [[nodiscard]] header_view header() const;
    [[nodiscard]] std::uint32_t major() const { return header().get<"major">(); }
    [[nodiscard]] std::uint32_t minor() const { return header().get<"minor">(); }
    /// Best guess from the header: 2.1 => sims4, otherwise sims3 (Medieval is indistinguishable).
    [[nodiscard]] game detected_game() const { return minor() >= 1 ? game::sims4 : game::sims3; }

    // --- index
    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] std::uint32_t index_flags() const;
    [[nodiscard]] entry_info entry(std::size_t i) const;
    [[nodiscard]] tgi key(std::size_t i) const;
    /// First live entry with this key (built into a hash table on first call).
    [[nodiscard]] std::optional<std::size_t> find(const tgi& k) const;
    /// All entry positions whose type matches (linear scan straight over the mapped index).
    [[nodiscard]] std::vector<std::size_t> find_type(std::uint32_t type) const;
    /// Bulk-decode the index into caller memory (used by the C ABI / C#: one call, no per-entry overhead).
    void read_index(std::span<entry_info> out, std::size_t first = 0) const;

    // --- data
    /// Stored bytes exactly as in the file (possibly compressed). Always zero-copy.
    [[nodiscard]] byte_span raw(std::size_t i) const;
    /// Uncompressed bytes. Zero-copy when the resource is stored uncompressed.
    [[nodiscard]] blob read(std::size_t i) const;
    /// Decompress into caller memory; `out.size()` must equal entry(i).mem_size.
    void read_into(std::size_t i, std::span<std::byte> out) const;
    /// Hint the OS to page in the stored bytes of entry i.
    void prefetch(std::size_t i) const;

    /// Full grammar view of the index (for tooling that wants every field).
    [[nodiscard]] auto index_view() const;

    struct state;

private:
    explicit package(std::shared_ptr<const state> s) : st_(std::move(s)) {}
    std::shared_ptr<const state> st_;
    friend class package_editor;
};

// ============================================================================ editor
/// Copy-on-write edit session over a package (or a brand-new one). Untouched resources are
/// streamed straight from the map to the output on save, without being decompressed.
class package_editor {
public:
    enum class compress_policy : std::uint8_t {
        keep,    // replaced data is stored the way the original was; added data follows its own flag
        always,  // compress every added/replaced resource when it helps
        never,
    };

    explicit package_editor(game g);  // new, empty package
    explicit package_editor(package base, std::optional<game> g = std::nullopt);

    [[nodiscard]] game target_game() const { return game_; }
    void set_target_game(game g) { game_ = g; }
    void set_policy(compress_policy p) { policy_ = p; }

    [[nodiscard]] std::size_t size() const { return slots_.size(); }  // includes removed slots
    [[nodiscard]] std::size_t live_count() const;
    [[nodiscard]] const tgi& key(std::size_t i) const { return slots_.at(i).key; }
    [[nodiscard]] bool removed(std::size_t i) const { return slots_.at(i).removed; }
    /// TS4 deleted-record marker carried over from the source package (no data, never found by key).
    [[nodiscard]] bool marker(std::size_t i) const { return slots_.at(i).marker; }
    [[nodiscard]] bool dirty(std::size_t i) const { return slots_.at(i).data.has_value(); }
    [[nodiscard]] std::optional<std::size_t> find(const tgi& k) const;

    /// Add a resource; returns its slot, or nullopt if reject_dups and the key exists.
    std::optional<std::size_t> add(const tgi& k, byte_vec data, bool reject_dups = false, std::optional<bool> compress = {});
    void replace(std::size_t i, byte_vec data);
    void remove(std::size_t i);
    void restore(std::size_t i);
    void set_key(std::size_t i, const tgi& k);
    void set_compression(std::size_t i, bool compress);
    [[nodiscard]] blob read(std::size_t i) const;

    void set_user_version(std::uint32_t major, std::uint32_t minor);
    void set_timestamps(std::uint32_t created, std::uint32_t updated);

    /// Write a complete package. Returns the number of resources written.
    std::size_t save_as(const std::filesystem::path& path) const;
    [[nodiscard]] byte_vec save_to_memory() const;
    /// Save over the base package's own file (temp file + atomic replace), then re-open it.
    void save_in_place();

    [[nodiscard]] const package& base() const { return base_; }

private:
    struct slot {
        tgi key;
        std::optional<std::size_t> original;  // index in base_
        std::optional<byte_vec> data;         // replaced/added content (uncompressed)
        bool removed = false;
        bool compress = false;
        bool marker = false;
    };
    // key -> live, non-marker slots holding it (ascending); find() returns the first
    std::unordered_map<tgi, std::vector<std::size_t>, tgi_hash> by_key_;
    void index_slot(std::size_t i);
    void unindex_slot(std::size_t i);
    struct sink;
    std::size_t write(sink& out) const;

    package base_;
    game game_ = game::sims3;
    compress_policy policy_ = compress_policy::keep;
    std::vector<slot> slots_;
    std::optional<std::pair<std::uint32_t, std::uint32_t>> user_version_, timestamps_;
};

// ============================================================================ text helpers
using g::utf16_to_utf8;
using g::utf8_to_utf16;

}  // namespace dbpfx

// index_view() needs the state definition
#include "detail/package_state.hpp"
