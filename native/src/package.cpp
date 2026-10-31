#include "dbpfx/package.hpp"
#include "dbpfx/requirements.hpp"

#include <cstdio>
#include <fstream>
#include <map>
#include <set>

namespace dbpfx {

const char* game_name(game g) {
    switch (g) {
        case game::sims3: return "The Sims 3";
        case game::medieval: return "The Sims Medieval";
        case game::sims4: return "The Sims 4";
    }
    return "?";
}

std::string tgi::str() const {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%08X:%08X:%016llX", type, group, static_cast<unsigned long long>(instance));
    return buf;
}

// ============================================================================ text
namespace g {
std::string utf16_to_utf8(std::u16string_view s) {
    std::string o;
    o.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        std::uint32_t c = s[i];
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] < 0xE000)
            c = 0x10000 + ((c - 0xD800) << 10) + (s[++i] - 0xDC00);
        if (c < 0x80) {
            o += static_cast<char>(c);
        } else if (c < 0x800) {
            o += static_cast<char>(0xC0 | (c >> 6));
            o += static_cast<char>(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            o += static_cast<char>(0xE0 | (c >> 12));
            o += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            o += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            o += static_cast<char>(0xF0 | (c >> 18));
            o += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
            o += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            o += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return o;
}

std::u16string utf8_to_utf16(std::string_view s) {
    std::u16string o;
    o.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        const auto c0 = static_cast<unsigned char>(s[i]);
        std::uint32_t c;
        std::size_t n;
        if (c0 < 0x80) { c = c0; n = 1; }
        else if ((c0 >> 5) == 0x6) { c = c0 & 0x1F; n = 2; }
        else if ((c0 >> 4) == 0xE) { c = c0 & 0x0F; n = 3; }
        else if ((c0 >> 3) == 0x1E) { c = c0 & 0x07; n = 4; }
        else { c = 0xFFFD; n = 1; }
        if (i + n > s.size()) { c = 0xFFFD; n = s.size() - i; }
        else for (std::size_t k = 1; k < n; ++k) c = (c << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        i += n;
        if (c >= 0x10000) {
            c -= 0x10000;
            o += static_cast<char16_t>(0xD800 + (c >> 10));
            o += static_cast<char16_t>(0xDC00 + (c & 0x3FF));
        } else {
            o += static_cast<char16_t>(c);
        }
    }
    return o;
}
}  // namespace g

// ============================================================================ open
namespace {

void compile_layout(package::state& st) {
    const auto h = st.header();
    if (h.get<"major">() != 2)
        throw format_error("unsupported DBPF major version " + std::to_string(h.get<"major">()) + " (expected 2)");
    const auto count = h.get<"index_count">();
    const std::size_t pos = st.index_position();
    auto& L = st.layout;
    L.count = count;
    if (count == 0 && (pos == 0 || pos >= st.bytes.size())) return;  // empty package
    if (pos < schema::dbpf_header::fixed_size || pos >= st.bytes.size())
        throw format_error("index position 0x" + std::to_string(pos) + " is outside the file");

    // Let the grammar find every offset once; afterwards entries are read with plain loads.
    const auto idx = g::parse<schema::dbpf_index>(st.bytes.subspan(pos), g::root_ctx{{"index_count", count}});
    L.flags = idx.get<"flags">();
    L.const_type = idx.get<"type">().value_or(0);
    L.const_group = idx.get<"group">().value_or(0);
    L.const_instance_hi = idx.get<"instance_hi">().value_or(0);
    const auto entries = idx.get<"entries">();  // validates count * stride fits the file
    if (count == 0) return;
    L.stride = entries.stride();
    L.entries = entries.element_data(0);
    const auto e0 = entries[0];
    L.off_type = (L.flags & schema::index_flags::const_type) ? -1 : static_cast<int>(e0.offset<"type">());
    L.off_group = (L.flags & schema::index_flags::const_group) ? -1 : static_cast<int>(e0.offset<"group">());
    L.off_instance_hi = (L.flags & schema::index_flags::const_instance_hi) ? -1 : static_cast<int>(e0.offset<"instance_hi">());
    L.off_instance_lo = static_cast<int>(e0.offset<"instance_lo">());
    L.off_offset = static_cast<int>(e0.offset<"offset">());
    L.off_file_size = static_cast<int>(e0.offset<"file_size">());
    L.off_mem_size = static_cast<int>(e0.offset<"mem_size">());
    L.off_compression = static_cast<int>(e0.offset<"compression">());
    L.off_committed = static_cast<int>(e0.offset<"committed">());
}

std::shared_ptr<package::state> finish(std::shared_ptr<package::state> st) {
    if (st->bytes.size() < schema::dbpf_header::fixed_size) throw format_error("file is too small to be a DBPF package");
    (void)st->header().get<"magic">();  // throws on bad magic
    compile_layout(*st);
    return st;
}

}  // namespace

package package::open(const std::filesystem::path& path) {
    requirements::require_supported_platform();
    auto st = std::make_shared<state>();
    st->storage = mapped_file(path);
    st->path = path;
    st->bytes = std::get<mapped_file>(st->storage).bytes();
    return package(finish(std::move(st)));
}

package package::from_memory(byte_vec bytes) {
    requirements::require_supported_platform();
    auto st = std::make_shared<state>();
    st->storage = std::move(bytes);
    const auto& v = std::get<byte_vec>(st->storage);
    st->bytes = {v.data(), v.size()};
    return package(finish(std::move(st)));
}

const std::filesystem::path& package::path() const { return st_->path; }
byte_span package::file_bytes() const { return st_->bytes; }
header_view package::header() const { return st_->header(); }
std::size_t package::size() const { return st_ ? st_->layout.count : 0; }
std::uint32_t package::index_flags() const { return st_->layout.flags; }

// ============================================================================ index
tgi package::key(std::size_t i) const {
    const auto& L = st_->layout;
    if (i >= L.count) throw std::out_of_range("entry index");
    const auto* e = L.at(i);
    return {L.type(e), L.group(e), L.instance(e)};
}

entry_info package::entry(std::size_t i) const {
    const auto& L = st_->layout;
    if (i >= L.count) throw std::out_of_range("entry index");
    const auto* e = L.at(i);
    entry_info r;
    r.key = {L.type(e), L.group(e), L.instance(e)};
    r.offset = load_le<std::uint32_t>(e + L.off_offset);
    r.file_size = load_le<std::uint32_t>(e + L.off_file_size) & 0x7FFFFFFFu;
    r.mem_size = load_le<std::uint32_t>(e + L.off_mem_size);
    r.compression = load_le<std::uint16_t>(e + L.off_compression);
    r.committed = load_le<std::uint16_t>(e + L.off_committed);
    return r;
}

void package::read_index(std::span<entry_info> out, std::size_t first) const {
    const std::size_t n = std::min(out.size(), size() > first ? size() - first : 0);
    for (std::size_t k = 0; k < n; ++k) out[k] = entry(first + k);
}

std::optional<std::size_t> package::find(const tgi& k) const {
    const auto& st = *st_;
    std::call_once(st.lookup_once, [&] {
        st.lookup.reserve(st.layout.count);
        for (std::size_t i = 0; i < st.layout.count; ++i) {
            const auto e = entry(i);
            if (!e.is_deleted_record()) st.lookup.emplace(e.key, static_cast<std::uint32_t>(i));  // keeps first
        }
    });
    const auto it = st.lookup.find(k);
    if (it == st.lookup.end()) return std::nullopt;
    return it->second;
}

std::vector<std::size_t> package::find_type(std::uint32_t type) const {
    std::vector<std::size_t> r;
    const auto& L = st_->layout;
    if (L.off_type < 0) {
        if (L.const_type == type)
            for (std::size_t i = 0; i < L.count; ++i) r.push_back(i);
        return r;
    }
    for (std::size_t i = 0; i < L.count; ++i)
        if (load_le<std::uint32_t>(L.at(i) + L.off_type) == type) r.push_back(i);
    return r;
}

// ============================================================================ data
byte_span package::raw(std::size_t i) const {
    const auto e = entry(i);
    if (e.is_deleted_record()) return {};
    const auto& b = st_->bytes;
    if (e.offset > b.size() || e.file_size > b.size() - e.offset)
        throw format_error("resource " + e.key.str() + " lies outside the file");
    return b.subspan(e.offset, e.file_size);
}

void package::prefetch(std::size_t i) const {
    const auto e = entry(i);
    if (const auto* m = std::get_if<mapped_file>(&st_->storage)) m->prefetch(e.offset, e.file_size);
}

void package::read_into(std::size_t i, std::span<std::byte> out) const {
    const auto e = entry(i);
    if (out.size() != e.mem_size) throw std::invalid_argument("read_into: buffer size must equal mem_size");
    const auto r = raw(i);
    if (e.file_size == e.mem_size) {
        if (!r.empty()) std::memcpy(out.data(), r.data(), r.size());
        return;
    }
    compression::decompress(r, out, e.compression);
}

blob package::read(std::size_t i) const {
    const auto e = entry(i);
    if (e.is_deleted_record()) return blob(byte_vec{});
    const auto r = raw(i);
    // Equal sizes mean "stored", regardless of the compression field.
    if (e.file_size == e.mem_size) return blob(r, st_);
    byte_vec out(e.mem_size);
    compression::decompress(r, {out.data(), out.size()}, e.compression);
    return blob(std::move(out));
}

// ============================================================================ editor
package_editor::package_editor(game g) : game_(g) { requirements::require_supported_platform(); }

package_editor::package_editor(package base, std::optional<game> g) : base_(std::move(base)) {
    game_ = g.value_or(base_.detected_game());
    slots_.reserve(base_.size());
    by_key_.reserve(base_.size());
    for (std::size_t i = 0; i < base_.size(); ++i) {
        const auto e = base_.entry(i);
        slot s;
        s.key = e.key;
        s.original = i;
        s.compress = e.is_compressed();
        s.marker = e.is_deleted_record();
        slots_.push_back(std::move(s));
        index_slot(i);
    }
}

void package_editor::index_slot(std::size_t i) {
    const auto& s = slots_[i];
    if (s.removed || s.marker) return;
    auto& v = by_key_[s.key];
    v.insert(std::lower_bound(v.begin(), v.end(), i), i);
}

void package_editor::unindex_slot(std::size_t i) {
    const auto it = by_key_.find(slots_[i].key);
    if (it == by_key_.end()) return;
    auto& v = it->second;
    const auto pos = std::lower_bound(v.begin(), v.end(), i);
    if (pos != v.end() && *pos == i) v.erase(pos);
    if (v.empty()) by_key_.erase(it);
}

std::size_t package_editor::live_count() const {
    return static_cast<std::size_t>(std::count_if(slots_.begin(), slots_.end(), [](const slot& s) { return !s.removed && !s.marker; }));
}

std::optional<std::size_t> package_editor::find(const tgi& k) const {
    const auto it = by_key_.find(k);
    if (it == by_key_.end() || it->second.empty()) return std::nullopt;
    return it->second.front();
}

std::optional<std::size_t> package_editor::add(const tgi& k, byte_vec data, bool reject_dups, std::optional<bool> compress) {
    if (reject_dups && find(k)) return std::nullopt;
    slot s;
    s.key = k;
    s.data = std::move(data);
    s.compress = compress.value_or(policy_ != compress_policy::never);
    slots_.push_back(std::move(s));
    index_slot(slots_.size() - 1);
    return slots_.size() - 1;
}

void package_editor::replace(std::size_t i, byte_vec data) {
    auto& s = slots_.at(i);
    s.data = std::move(data);
    if (s.marker) {  // giving a marker real content turns it into a normal resource
        s.marker = false;
        index_slot(i);
    }
}
void package_editor::remove(std::size_t i) {
    if (slots_.at(i).removed) return;
    unindex_slot(i);
    slots_[i].removed = true;
}
void package_editor::restore(std::size_t i) {
    if (!slots_.at(i).removed) return;
    slots_[i].removed = false;
    index_slot(i);
}
void package_editor::set_key(std::size_t i, const tgi& k) {
    (void)slots_.at(i);
    unindex_slot(i);  // under the old key
    slots_[i].key = k;
    index_slot(i);
}
void package_editor::set_compression(std::size_t i, bool c) {
    auto& s = slots_.at(i);
    if (s.compress == c) return;
    s.compress = c;
    // must be re-encoded (deleted-record markers carry no data and stay markers)
    if (!s.data && s.original && !base_.entry(*s.original).is_deleted_record()) s.data = base_.read(*s.original).copy();
}
void package_editor::set_user_version(std::uint32_t major, std::uint32_t minor) { user_version_ = {major, minor}; }
void package_editor::set_timestamps(std::uint32_t created, std::uint32_t updated) { timestamps_ = {created, updated}; }

blob package_editor::read(std::size_t i) const {
    const auto& s = slots_.at(i);
    if (s.data) return blob(byte_vec(*s.data));
    if (s.original) return base_.read(*s.original);
    return blob(byte_vec{});
}

struct package_editor::sink {
    std::function<void(const void*, std::size_t)> write;
    std::function<void(std::size_t, const void*, std::size_t)> patch;
    std::size_t pos = 0;
    void put(const void* p, std::size_t n) {
        write(p, n);
        pos += n;
    }
};

std::size_t package_editor::write(sink& out) const {
    const auto prof = profile_of(game_);

    // ---- header: start from the original (keeps unknown fields) or a fresh one
    g::model<schema::dbpf_header> hdr;
    if (base_.valid()) {
        hdr = base_.header().own();
    } else {
        hdr.set<"major">(prof.major).set<"minor">(prof.minor);
    }
    hdr.set<"major">(prof.major).set<"minor">(prof.minor);
    if (user_version_) hdr.set<"user_major">(user_version_->first).set<"user_minor">(user_version_->second);
    if (timestamps_) hdr.set<"created">(timestamps_->first).set<"updated">(timestamps_->second);
    hdr.set<"index_position_low">(0u).set<"index_version">(3u);

    std::array<std::byte, 96> placeholder{};
    out.put(placeholder.data(), placeholder.size());

    // ---- resource data
    g::model<schema::dbpf_index> idx;
    auto& entries = idx.get<"entries">();
    std::set<std::uint32_t> types, groups, ihis;
    std::vector<entry_info> written;
    written.reserve(slots_.size());

    for (const auto& s : slots_) {
        if (s.removed) continue;
        // TS4 "deleted record" markers mean nothing to TS3/TSM: drop them on conversion.
        if (game_ != game::sims4 && !s.data && s.original && base_.entry(*s.original).is_deleted_record()) continue;
        entry_info e;
        e.key = s.key;
        e.offset = static_cast<std::uint32_t>(out.pos);
        // A clean resource whose codec the target game cannot read (zlib -> TS3) is transcoded.
        const bool transcode = !s.data && s.original && !game_reads(game_, base_.entry(*s.original).compression);
        if (!s.data && s.original && !transcode) {
            // Clean: copy stored bytes verbatim - no decompression, no recompression.
            const auto src = base_.entry(*s.original);
            byte_span r;
            if (!src.is_deleted_record()) r = base_.raw(*s.original);
            else if (src.offset <= base_.file_bytes().size() && src.file_size <= base_.file_bytes().size() - src.offset)
                r = base_.file_bytes().subspan(src.offset, src.file_size);
            out.put(r.data(), r.size());
            e.file_size = static_cast<std::uint32_t>(r.size());
            e.mem_size = src.mem_size;
            e.compression = src.compression;
            e.committed = src.committed;
        } else {
            byte_vec decoded;
            if (transcode) decoded = base_.read(*s.original).copy();
            const byte_vec& d = s.data ? *s.data : decoded;
            const bool want = transcode ? policy_ != compress_policy::never
                                        : policy_ == compress_policy::always || (policy_ == compress_policy::keep && s.compress);
            byte_vec packed;
            if (want && !d.empty())
                packed = prof.write_compression == compression::kind::zlib ? compression::zlib_compress({d.data(), d.size()})
                                                                          : compression::refpack_compress({d.data(), d.size()});
            const byte_vec& body = packed.empty() ? d : packed;
            out.put(body.data(), body.size());
            e.file_size = static_cast<std::uint32_t>(body.size());
            e.mem_size = static_cast<std::uint32_t>(d.size());
            e.compression = packed.empty() ? std::uint16_t(0) : static_cast<std::uint16_t>(prof.write_compression);
            e.committed = 1;
        }
        types.insert(e.key.type);
        groups.insert(e.key.group);
        ihis.insert(static_cast<std::uint32_t>(e.key.instance >> 32));
        written.push_back(e);
    }

    // ---- index: store type / group / instance-high once when every entry shares them
    std::uint32_t flags = 0;
    if (types.size() <= 1) flags |= schema::index_flags::const_type;
    if (groups.size() <= 1) flags |= schema::index_flags::const_group;
    if (ihis.size() <= 1) flags |= schema::index_flags::const_instance_hi;
    idx.set<"flags">(flags);
    if (flags & schema::index_flags::const_type) idx.set<"type">(std::optional<std::uint32_t>(types.empty() ? 0 : *types.begin()));
    if (flags & schema::index_flags::const_group) idx.set<"group">(std::optional<std::uint32_t>(groups.empty() ? 0 : *groups.begin()));
    if (flags & schema::index_flags::const_instance_hi)
        idx.set<"instance_hi">(std::optional<std::uint32_t>(ihis.empty() ? 0 : *ihis.begin()));
    entries.reserve(written.size());
    for (const auto& e : written) {
        g::model<schema::dbpf_index_entry> m;
        m.set<"type">(std::optional<std::uint32_t>(e.key.type));
        m.set<"group">(std::optional<std::uint32_t>(e.key.group));
        m.set<"instance_hi">(std::optional<std::uint32_t>(static_cast<std::uint32_t>(e.key.instance >> 32)));
        m.set<"instance_lo">(static_cast<std::uint32_t>(e.key.instance));
        m.set<"offset">(e.offset);
        m.set<"file_size">(e.file_size | 0x80000000u);
        m.set<"mem_size">(e.mem_size);
        m.set<"compression">(e.compression);
        m.set<"committed">(e.committed);
        entries.push_back(std::move(m));
    }
    const std::size_t index_pos = out.pos;
    const byte_vec index_bytes = g::serialize<schema::dbpf_index>(idx);
    out.put(index_bytes.data(), index_bytes.size());

    hdr.set<"index_count">(static_cast<std::uint32_t>(written.size()));
    hdr.set<"index_size">(static_cast<std::uint32_t>(index_bytes.size()));
    hdr.set<"index_position">(static_cast<std::uint32_t>(index_pos));
    const byte_vec header_bytes = g::serialize<schema::dbpf_header>(hdr);
    out.patch(0, header_bytes.data(), header_bytes.size());
    return written.size();
}

std::size_t package_editor::save_as(const std::filesystem::path& path) const {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) throw std::runtime_error("cannot create '" + path.string() + "'");
    std::vector<char> buf(1 << 20);
    f.rdbuf()->pubsetbuf(buf.data(), static_cast<std::streamsize>(buf.size()));
    sink s;
    s.write = [&](const void* p, std::size_t n) { f.write(static_cast<const char*>(p), static_cast<std::streamsize>(n)); };
    s.patch = [&](std::size_t at, const void* p, std::size_t n) {
        f.seekp(static_cast<std::streamoff>(at));
        f.write(static_cast<const char*>(p), static_cast<std::streamsize>(n));
    };
    const auto n = write(s);
    f.flush();
    if (!f) throw std::runtime_error("write failed for '" + path.string() + "'");
    return n;
}

byte_vec package_editor::save_to_memory() const {
    byte_vec out;
    sink s;
    s.write = [&](const void* p, std::size_t n) {
        const auto* b = static_cast<const std::byte*>(p);
        out.insert(out.end(), b, b + n);
    };
    s.patch = [&](std::size_t at, const void* p, std::size_t n) { std::memcpy(out.data() + at, p, n); };
    write(s);
    return out;
}

void package_editor::save_in_place() {
    if (!base_.valid() || base_.path().empty()) throw std::logic_error("save_in_place: package has no file of its own");
    const auto target = base_.path();
    auto tmp = target;
    tmp += ".dbpfx-tmp";
    save_as(tmp);
    // Release our mapping before replacing the file (required on Windows).
    base_ = package{};
    std::error_code ec;
    std::filesystem::rename(tmp, target, ec);
    if (ec) {
        std::filesystem::remove(target, ec);
        std::filesystem::rename(tmp, target);
    }
    const auto policy = policy_;
    *this = package_editor(package::open(target), game_);
    policy_ = policy;
}

}  // namespace dbpfx
