#define DBPFX_BUILD
#include "dbpfx/dbpfx.h"

#include "dbpfx/package.hpp"

#include <cstdlib>
#include <filesystem>
#include <string>

struct dbpfx_package {
    dbpfx::package pkg;
};
struct dbpfx_editor {
    dbpfx::package_editor ed;
};

static_assert(sizeof(dbpfx_entry) == 40);

namespace {

thread_local std::string g_error;

template <class F>
int guard(F&& f) {
    try {
        g_error.clear();
        return f();
    } catch (const dbpfx::format_error& e) {
        g_error = e.what();
        return DBPFX_E_FORMAT;
    } catch (const std::out_of_range& e) {
        g_error = e.what();
        return DBPFX_E_RANGE;
    } catch (const std::invalid_argument& e) {
        g_error = e.what();
        return DBPFX_E_ARG;
    } catch (const std::filesystem::filesystem_error& e) {
        g_error = e.what();
        return DBPFX_E_IO;
    } catch (const std::runtime_error& e) {
        g_error = e.what();
        return DBPFX_E_IO;
    } catch (const std::exception& e) {
        g_error = e.what();
        return DBPFX_E_OTHER;
    } catch (...) {
        g_error = "unknown error";
        return DBPFX_E_OTHER;
    }
}
template <class F>
int64_t guard64(F&& f) {
    int64_t r = 0;
    const int rc = guard([&] {
        r = f();
        return 0;
    });
    return rc < 0 ? rc : r;
}

std::filesystem::path to_path(const char* utf8) {
    if (!utf8) throw std::invalid_argument("path is null");
    const std::string_view s(utf8);
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}
std::optional<dbpfx::game> to_game(int g) {
    if (g == DBPFX_GAME_AUTO) return std::nullopt;
    if (g < 0 || g > 2) throw std::invalid_argument("unknown game id");
    return static_cast<dbpfx::game>(g);
}
dbpfx::byte_vec copy_in(const void* p, uint64_t n) {
    if (!p && n) throw std::invalid_argument("data is null");
    const auto* b = static_cast<const std::byte*>(p);
    return dbpfx::byte_vec(b, b + n);
}
void to_c(const dbpfx::entry_info& e, std::size_t index, dbpfx_entry& o) {
    o.type = e.key.type;
    o.group = e.key.group;
    o.instance = e.key.instance;
    o.offset = e.offset;
    o.file_size = e.file_size;
    o.mem_size = e.mem_size;
    o.compression = e.compression;
    o.committed = e.committed;
    o.index = static_cast<uint32_t>(index);
    o.reserved = 0;
}
#define REQUIRE(x)                                              \
    do {                                                        \
        if (!(x)) throw std::invalid_argument(#x " is required"); \
    } while (0)

}  // namespace

extern "C" {

#ifndef DBPFX_VERSION
#define DBPFX_VERSION "0.0.0"
#endif
const char* dbpfx_version(void) { return "DBPFX " DBPFX_VERSION; }
const char* dbpfx_last_error(void) { return g_error.c_str(); }

// ---------------------------------------------------------------- packages
int dbpfx_open(const char* path, dbpfx_package** out) {
    return guard([&] {
        REQUIRE(out);
        *out = new dbpfx_package{dbpfx::package::open(to_path(path))};
        return 0;
    });
}
int dbpfx_open_memory(const void* data, uint64_t len, dbpfx_package** out) {
    return guard([&] {
        REQUIRE(out);
        *out = new dbpfx_package{dbpfx::package::from_memory(copy_in(data, len))};
        return 0;
    });
}
void dbpfx_close(dbpfx_package* p) { delete p; }

int dbpfx_header_get(const dbpfx_package* p, dbpfx_header* o) {
    return guard([&] {
        REQUIRE(p && o);
        const auto h = p->pkg.header();
        o->major = h.get<"major">();
        o->minor = h.get<"minor">();
        o->user_major = h.get<"user_major">();
        o->user_minor = h.get<"user_minor">();
        o->unused1 = h.get<"unused1">();
        o->created = h.get<"created">();
        o->updated = h.get<"updated">();
        o->unused2 = h.get<"unused2">();
        o->index_count = h.get<"index_count">();
        o->index_position_low = h.get<"index_position_low">();
        o->index_size = h.get<"index_size">();
        o->index_version = h.get<"index_version">();
        o->index_position = h.get<"index_position">();
        o->index_flags = p->pkg.size() ? p->pkg.index_flags() : 0;
        return 0;
    });
}
const uint8_t* dbpfx_header_bytes(const dbpfx_package* p) {
    return p ? reinterpret_cast<const uint8_t*>(p->pkg.file_bytes().data()) : nullptr;
}
int dbpfx_detected_game(const dbpfx_package* p) {
    return guard([&] {
        REQUIRE(p);
        return static_cast<int>(p->pkg.detected_game());
    });
}
uint32_t dbpfx_count(const dbpfx_package* p) { return p ? static_cast<uint32_t>(p->pkg.size()) : 0; }

int64_t dbpfx_entries(const dbpfx_package* p, uint32_t first, uint32_t n, dbpfx_entry* out) {
    return guard64([&]() -> int64_t {
        REQUIRE(p && (out || n == 0));
        const std::size_t total = p->pkg.size();
        if (first >= total) return 0;
        const std::size_t k = std::min<std::size_t>(n, total - first);
        for (std::size_t j = 0; j < k; ++j) to_c(p->pkg.entry(first + j), first + j, out[j]);
        return static_cast<int64_t>(k);
    });
}
int dbpfx_get_entry(const dbpfx_package* p, uint32_t i, dbpfx_entry* out) {
    return guard([&] {
        REQUIRE(p && out);
        to_c(p->pkg.entry(i), i, *out);
        return 0;
    });
}
int64_t dbpfx_find(const dbpfx_package* p, uint32_t t, uint32_t g, uint64_t i) {
    return guard64([&]() -> int64_t {
        REQUIRE(p);
        const auto r = p->pkg.find({t, g, i});
        return r ? static_cast<int64_t>(*r) : -1;
    });
}
int64_t dbpfx_find_type(const dbpfx_package* p, uint32_t type, uint32_t* out, uint32_t cap) {
    return guard64([&]() -> int64_t {
        REQUIRE(p);
        const auto r = p->pkg.find_type(type);
        for (std::size_t k = 0; k < r.size() && k < cap && out; ++k) out[k] = static_cast<uint32_t>(r[k]);
        return static_cast<int64_t>(r.size());
    });
}
int dbpfx_raw(const dbpfx_package* p, uint32_t i, const uint8_t** data, uint64_t* len) {
    return guard([&] {
        REQUIRE(p && data && len);
        const auto r = p->pkg.raw(i);
        *data = reinterpret_cast<const uint8_t*>(r.data());
        *len = r.size();
        return 0;
    });
}
int dbpfx_direct(const dbpfx_package* p, uint32_t i, const uint8_t** data, uint64_t* len) {
    return guard([&] {
        REQUIRE(p && data && len);
        const auto e = p->pkg.entry(i);
        if (e.is_deleted_record()) {
            *data = nullptr;
            *len = 0;
            return 1;
        }
        if (e.file_size != e.mem_size) return 0;
        const auto r = p->pkg.raw(i);
        *data = reinterpret_cast<const uint8_t*>(r.data());
        *len = r.size();
        return 1;
    });
}
int dbpfx_read_into(const dbpfx_package* p, uint32_t i, void* dst, uint64_t len) {
    return guard([&] {
        REQUIRE(p && (dst || len == 0));
        p->pkg.read_into(i, {static_cast<std::byte*>(dst), static_cast<std::size_t>(len)});
        return 0;
    });
}
void dbpfx_prefetch(const dbpfx_package* p, uint32_t i) {
    guard([&] {
        if (p) p->pkg.prefetch(i);
        return 0;
    });
}

// ---------------------------------------------------------------- editor
int dbpfx_editor_new(int game, dbpfx_editor** out) {
    return guard([&] {
        REQUIRE(out);
        *out = new dbpfx_editor{dbpfx::package_editor(to_game(game).value_or(dbpfx::game::sims3))};
        return 0;
    });
}
int dbpfx_editor_open(const dbpfx_package* base, int game, dbpfx_editor** out) {
    return guard([&] {
        REQUIRE(base && out);
        *out = new dbpfx_editor{dbpfx::package_editor(base->pkg, to_game(game))};
        return 0;
    });
}
void dbpfx_editor_free(dbpfx_editor* e) { delete e; }
int dbpfx_editor_set_game(dbpfx_editor* e, int game) {
    return guard([&] {
        REQUIRE(e);
        e->ed.set_target_game(to_game(game).value_or(e->ed.base().valid() ? e->ed.base().detected_game() : dbpfx::game::sims3));
        return 0;
    });
}
int dbpfx_editor_get_game(const dbpfx_editor* e) { return e ? static_cast<int>(e->ed.target_game()) : DBPFX_E_ARG; }
int dbpfx_editor_set_policy(dbpfx_editor* e, int policy) {
    return guard([&] {
        REQUIRE(e && policy >= 0 && policy <= 2);
        e->ed.set_policy(static_cast<dbpfx::package_editor::compress_policy>(policy));
        return 0;
    });
}
uint32_t dbpfx_editor_count(const dbpfx_editor* e) { return e ? static_cast<uint32_t>(e->ed.size()) : 0; }
int dbpfx_editor_key(const dbpfx_editor* e, uint32_t i, uint32_t* t, uint32_t* g, uint64_t* inst) {
    return guard([&] {
        REQUIRE(e && t && g && inst);
        const auto& k = e->ed.key(i);
        *t = k.type;
        *g = k.group;
        *inst = k.instance;
        return 0;
    });
}
int dbpfx_editor_is_removed(const dbpfx_editor* e, uint32_t i) {
    return guard([&] {
        REQUIRE(e);
        return e->ed.removed(i) ? 1 : 0;
    });
}
int dbpfx_editor_slot_state(const dbpfx_editor* e, uint32_t i) {
    return guard([&] {
        REQUIRE(e);
        return e->ed.removed(i) ? 1 : e->ed.marker(i) ? 2 : 0;
    });
}
uint32_t dbpfx_editor_live_count(const dbpfx_editor* e) { return e ? static_cast<uint32_t>(e->ed.live_count()) : 0; }
int64_t dbpfx_editor_find(const dbpfx_editor* e, uint32_t t, uint32_t g, uint64_t i) {
    return guard64([&]() -> int64_t {
        REQUIRE(e);
        const auto r = e->ed.find({t, g, i});
        return r ? static_cast<int64_t>(*r) : -1;
    });
}
int64_t dbpfx_editor_add(dbpfx_editor* e, uint32_t t, uint32_t g, uint64_t i, const void* data, uint64_t len,
                           int reject_dups, int compress) {
    return guard64([&]() -> int64_t {
        REQUIRE(e);
        std::optional<bool> c;
        if (compress >= 0) c = compress != 0;
        const auto r = e->ed.add({t, g, i}, copy_in(data, len), reject_dups != 0, c);
        return r ? static_cast<int64_t>(*r) : -1;
    });
}
int dbpfx_editor_replace(dbpfx_editor* e, uint32_t i, const void* data, uint64_t len) {
    return guard([&] {
        REQUIRE(e);
        e->ed.replace(i, copy_in(data, len));
        return 0;
    });
}
int dbpfx_editor_remove(dbpfx_editor* e, uint32_t i) {
    return guard([&] {
        REQUIRE(e);
        e->ed.remove(i);
        return 0;
    });
}
int dbpfx_editor_restore(dbpfx_editor* e, uint32_t i) {
    return guard([&] {
        REQUIRE(e);
        e->ed.restore(i);
        return 0;
    });
}
int dbpfx_editor_set_key(dbpfx_editor* e, uint32_t i, uint32_t t, uint32_t g, uint64_t inst) {
    return guard([&] {
        REQUIRE(e);
        e->ed.set_key(i, {t, g, inst});
        return 0;
    });
}
int dbpfx_editor_set_compression(dbpfx_editor* e, uint32_t i, int c) {
    return guard([&] {
        REQUIRE(e);
        e->ed.set_compression(i, c != 0);
        return 0;
    });
}
int dbpfx_editor_set_user_version(dbpfx_editor* e, uint32_t major, uint32_t minor) {
    return guard([&] {
        REQUIRE(e);
        e->ed.set_user_version(major, minor);
        return 0;
    });
}
int64_t dbpfx_editor_data_size(const dbpfx_editor* e, uint32_t i) {
    return guard64([&]() -> int64_t {
        REQUIRE(e);
        return static_cast<int64_t>(e->ed.read(i).size());
    });
}
int dbpfx_editor_read(const dbpfx_editor* e, uint32_t i, void* dst, uint64_t len) {
    return guard([&] {
        REQUIRE(e && (dst || len == 0));
        const auto b = e->ed.read(i);
        if (b.size() != len) throw std::invalid_argument("buffer size must equal the resource size");
        if (len) std::memcpy(dst, b.data(), len);
        return 0;
    });
}
int dbpfx_editor_save_as(const dbpfx_editor* e, const char* path) {
    return guard([&] {
        REQUIRE(e);
        e->ed.save_as(to_path(path));
        return 0;
    });
}
int dbpfx_editor_save_in_place(dbpfx_editor* e) {
    return guard([&] {
        REQUIRE(e);
        e->ed.save_in_place();
        return 0;
    });
}
int dbpfx_editor_save_memory(const dbpfx_editor* e, uint8_t** out, uint64_t* len) {
    return guard([&] {
        REQUIRE(e && out && len);
        const auto v = e->ed.save_to_memory();
        auto* m = static_cast<uint8_t*>(std::malloc(v.empty() ? 1 : v.size()));
        if (!m) throw std::bad_alloc();
        std::memcpy(m, v.data(), v.size());
        *out = m;
        *len = v.size();
        return 0;
    });
}

// ---------------------------------------------------------------- codecs
int dbpfx_decompress(const void* in, uint64_t in_len, void* out, uint64_t out_len) {
    return guard([&] {
        REQUIRE((in || !in_len) && (out || !out_len));
        dbpfx::compression::decompress({static_cast<const std::byte*>(in), static_cast<std::size_t>(in_len)},
                                     {static_cast<std::byte*>(out), static_cast<std::size_t>(out_len)});
        return 0;
    });
}
int dbpfx_compress(int kind, const void* in, uint64_t in_len, uint8_t** out, uint64_t* out_len) {
    return guard([&] {
        REQUIRE((in || !in_len) && out && out_len);
        const dbpfx::byte_span src{static_cast<const std::byte*>(in), static_cast<std::size_t>(in_len)};
        dbpfx::byte_vec v;
        if (kind == DBPFX_COMPRESS_ZLIB) v = dbpfx::compression::zlib_compress(src);
        else if (kind == DBPFX_COMPRESS_REFPACK) v = dbpfx::compression::refpack_compress(src);
        else throw std::invalid_argument("unknown compression kind");
        *out = nullptr;
        *out_len = 0;
        if (v.empty()) return 0;
        auto* m = static_cast<uint8_t*>(std::malloc(v.size()));
        if (!m) throw std::bad_alloc();
        std::memcpy(m, v.data(), v.size());
        *out = m;
        *out_len = v.size();
        return 1;
    });
}
void dbpfx_free(void* p) { std::free(p); }

}  // extern "C"
