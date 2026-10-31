// DBPFX command-line tool
//
//   dbpfx-cli info    <file.package>
//   dbpfx-cli list    <file.package> [type-hex]
//   dbpfx-cli extract <file.package> <T-G-I> <out-file>
//   dbpfx-cli stbl    <file.package> [max]          dump string tables (TS3 or TS4 layout)
//   dbpfx-cli repack  <in.package> <out.package> [sims3|medieval|sims4] [keep|always|never]
//   dbpfx-cli verify  <file.package>                decompress every resource
//   dbpfx-cli types   <file.package> [more.package ...]
//                       resource types present, with names, and any unknown to the type table
#include "dbpfx/known_types.hpp"
#include "dbpfx/package.hpp"
#include "dbpfx/requirements.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <map>
#include <vector>

#include <cinttypes>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

using namespace dbpfx;

static tgi parse_tgi(const std::string& s) {
    tgi k{};
    unsigned t = 0, g = 0;
    unsigned long long i = 0;
    std::string norm = s;
    for (auto& c : norm)
        if (c == '-') c = ':';
    if (std::sscanf(norm.c_str(), "%x:%x:%llx", &t, &g, &i) != 3)
        throw std::invalid_argument("bad key, expected TTTTTTTT:GGGGGGGG:IIIIIIIIIIIIIIII");
    k.type = t;
    k.group = g;
    k.instance = i;
    return k;
}

static const char* comp_name(std::uint16_t c) {
    switch (c) {
        case 0x0000: return "none";
        case 0x5A42: return "zlib";
        case 0xFFFF: return "refpack";
        case 0xFFFE: return "stream";
        case 0xFFE0: return "deleted";
        default: return "?";
    }
}

static int info(const package& p) {
    const auto h = p.header();
    std::printf("DBPF %u.%u  (%s)\n", h.get<"major">(), h.get<"minor">(), game_name(p.detected_game()));
    std::printf("user version  %u.%u\n", h.get<"user_major">(), h.get<"user_minor">());
    std::printf("entries       %zu\n", p.size());
    std::printf("index         @0x%08X, %u bytes, flags 0x%X\n", h.get<"index_position">(), h.get<"index_size">(),
                p.size() ? p.index_flags() : 0u);
    std::size_t compressed = 0, deleted = 0;
    std::uint64_t stored = 0, unpacked = 0;
    for (std::size_t i = 0; i < p.size(); ++i) {
        const auto e = p.entry(i);
        if (e.is_deleted_record()) { ++deleted; continue; }
        compressed += e.file_size != e.mem_size;
        stored += e.file_size;
        unpacked += e.mem_size;
    }
    std::printf("compressed    %zu, deleted records %zu\n", compressed, deleted);
    std::printf("data          %" PRIu64 " bytes stored, %" PRIu64 " bytes uncompressed\n", stored, unpacked);
    return 0;
}

static int list(const package& p, std::optional<std::uint32_t> type) {
    for (std::size_t i = 0; i < p.size(); ++i) {
        const auto e = p.entry(i);
        if (type && e.key.type != *type) continue;
        const auto* t = known_types::find(e.key.type, p.detected_game());
        std::printf("%6zu  %s  off=0x%08X  file=%9u  mem=%9u  %-7s  %s\n", i, e.key.str().c_str(), e.offset, e.file_size,
                    e.mem_size, comp_name(e.compression), t ? t->name : "(unknown type)");
    }
    return 0;
}

static int stbl(const package& p, std::size_t max) {
    for (const auto i : p.find_type(0x220557DA)) {
        const auto data = p.read(i);
        const auto v = g::parse<schema::stbl>(data.bytes());
        std::printf("== %s\n", p.key(i).str().c_str());
        std::visit(
            [&](const auto& t) {
                std::size_t n = 0;
                for (const auto e : t.template get<"entries">()) {
                    if (n++ >= max) break;
                    const auto text = e.template get<"text">();
                    if constexpr (std::is_same_v<std::decay_t<decltype(text)>, g::u16_view>)
                        std::printf("  0x%016llX  %s\n", static_cast<unsigned long long>(e.template get<"key">()), text.utf8().c_str());
                    else
                        std::printf("  0x%08X  %.*s\n", static_cast<unsigned>(e.template get<"key">()), static_cast<int>(text.size()), text.data());
                }
            },
            v);
    }
    return 0;
}

// Best guess at what a blob is, from its first bytes: used to identify unknown resource types.
static std::string content_kind(byte_span d) {
    if (d.empty()) return "empty (0 bytes)";
    // signature length comes from the literal itself, so signatures may contain zero bytes
    auto starts = [&]<std::size_t N>(const char (&sig)[N]) {
        constexpr std::size_t n = N - 1;
        return d.size() >= n && std::memcmp(d.data(), sig, n) == 0;
    };
    if (starts("\x89PNG")) return "PNG image";
    if (starts("\xFF\xD8\xFF")) return "JPEG image";
    if (starts("DDS ")) return "DDS texture";
    if (starts("DST")) return "DST texture";
    if (starts("GIF8")) return "GIF image";
    if (starts("BM")) return "bitmap image (BMP)";
    if (starts("RIFF")) return d.size() >= 12 && std::memcmp(d.data() + 8, "AVI ", 4) == 0 ? "AVI video" : "RIFF container (WAV/...)";
    if (starts("OggS")) return "Ogg audio";
    if (starts("FWS") || starts("CWS") || starts("GFX") || starts("CFX")) return "Flash / Scaleform movie";
    if (starts("MZ")) return "Windows executable / .NET assembly";
    if (starts("PK\x03\x04")) return "ZIP archive";
    if (starts("\x1F\x8B")) return "gzip data";
    if (starts("OTTO")) return "OpenType font";
    if (starts("\x00\x01\x00\x00")) return "TrueType font";
    if (starts("DATA")) return "SimData";
    if (starts("STBL")) return "string table";
    if (starts("_pilC3S_")) return "animation clip";
    // XML, possibly after a UTF-8 byte order mark or whitespace
    std::size_t i = starts("\xEF\xBB\xBF") ? 3 : 0;
    while (i < d.size() && (d[i] == std::byte{' '} || d[i] == std::byte{'\r'} || d[i] == std::byte{'\n'} || d[i] == std::byte{'\t'})) ++i;
    if (i < d.size() && d[i] == std::byte{'<'}) return "XML / markup text";
    // mostly printable => text
    std::size_t printable = 0, n = std::min<std::size_t>(d.size(), 512);
    for (std::size_t k = 0; k < n; ++k) {
        const auto c = static_cast<unsigned char>(d[k]);
        printable += (c >= 0x20 && c < 0x7F) || c == '\r' || c == '\n' || c == '\t';
    }
    if (printable * 100 >= n * 95) return "plain text";
    // chunked binaries name their parts with FourCCs (GEOM, MLOD, VRTF, ...): list the first ones seen
    std::string tags;
    for (std::size_t k = 0; k + 4 <= std::min<std::size_t>(d.size(), 256) && tags.size() < 40; ++k) {
        bool tag = true;
        for (std::size_t m = 0; m < 4; ++m) {
            const auto c = static_cast<unsigned char>(d[k + m]);
            tag &= (c >= 'A' && c <= 'Z') || c == '_' || (c >= '0' && c <= '9');
        }
        const auto c0 = static_cast<unsigned char>(d[k]);
        if (tag && c0 >= 'A' && c0 <= 'Z') {
            tags += (tags.empty() ? "" : ",") + std::string(reinterpret_cast<const char*>(d.data() + k), 4);
            k += 3;
        }
    }
    return tags.empty() ? "binary" : "binary, FourCC tags: " + tags;
}

static std::string hex_preview(byte_span d) {
    std::string h, a;
    char buf[4];
    for (std::size_t k = 0; k < d.size() && k < 16; ++k) {
        const auto c = static_cast<unsigned char>(d[k]);
        std::snprintf(buf, sizeof buf, "%02X ", c);
        h += buf;
        a += (c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : '.';
    }
    return h + " |" + a + "|";
}

// Which types occur, how often, and which of them the table does not know - over one or many packages.
static int types(int count, char** paths) {
    struct stats {
        std::uint64_t resources = 0, stored = 0, size = 0;
        std::uint64_t min_size = ~0ull, max_size = 0;
        std::string sample_key, sample_package, sample_kind, sample_bytes;
    };
    std::map<std::pair<game, std::uint32_t>, stats> seen;
    std::size_t packages = 0;
    for (int k = 0; k < count; ++k) {
        try {
            const auto p = package::open(paths[k]);
            const game g = p.detected_game();
            ++packages;
            for (std::size_t i = 0; i < p.size(); ++i) {
                const auto e = p.entry(i);
                if (e.is_deleted_record()) continue;
                auto& s = seen[{g, e.key.type}];
                ++s.resources;
                s.stored += e.file_size;
                s.size += e.mem_size;
                s.min_size = std::min<std::uint64_t>(s.min_size, e.mem_size);
                s.max_size = std::max<std::uint64_t>(s.max_size, e.mem_size);
                // keep one decoded sample (the largest) of every unknown type
                if (!known_types::find(e.key.type, g) && (s.sample_key.empty() || e.mem_size > s.max_size - 1)) {
                    try {
                        const auto b = p.read(i);
                        s.sample_key = e.key.str();
                        s.sample_package = std::filesystem::path(paths[k]).filename().string();
                        s.sample_kind = content_kind(b.bytes());
                        s.sample_bytes = hex_preview(b.bytes());
                    } catch (const std::exception& ex) {
                        s.sample_kind = std::string("could not decode: ") + ex.what();
                    }
                }
            }
        } catch (const std::exception& ex) {
            std::fprintf(stderr, "%s: %s\n", paths[k], ex.what());
        }
    }
    std::vector<std::pair<std::pair<game, std::uint32_t>, stats>> rows(seen.begin(), seen.end());
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.second.resources > b.second.resources; });
    std::vector<std::pair<game, std::uint32_t>> unknown;
    std::printf("%-8s  %-10s %9s %14s  %s\n", "game", "type", "count", "bytes", "name");
    for (const auto& [key, s] : rows) {
        const auto* t = known_types::find(key.second, key.first);
        if (!t) unknown.push_back(key);
        std::printf("%-8s  %08X  %9llu %14llu  %s%s%s\n", key.first == game::sims4 ? "TS4" : "TS3/TSM", key.second,
                    static_cast<unsigned long long>(s.resources), static_cast<unsigned long long>(s.size),
                    t ? t->name : "** UNKNOWN **", t && t->tag[0] ? "  " : "", t ? t->tag : "");
    }
    std::printf("\n%zu package(s), %zu resource types, %zu unknown", packages, rows.size(), unknown.size());
    for (const auto& key : unknown) std::printf("%s %08X", &key == &unknown.front() ? ":" : ",", key.second);
    std::printf("\n");
    if (!unknown.empty()) {
        std::printf("\nunknown types - largest sample of each:\n");
        for (const auto& key : unknown) {
            const auto& s = seen[key];
            std::printf("\n%s %08X  %llu resources, %llu..%llu bytes each\n", key.first == game::sims4 ? "TS4" : "TS3/TSM", key.second,
                        static_cast<unsigned long long>(s.resources), static_cast<unsigned long long>(s.min_size),
                        static_cast<unsigned long long>(s.max_size));
            std::printf("  sample   %s in %s\n", s.sample_key.c_str(), s.sample_package.c_str());
            std::printf("  content  %s\n", s.sample_kind.c_str());
            if (!s.sample_bytes.empty() && s.sample_bytes != " ||") std::printf("  bytes    %s\n", s.sample_bytes.c_str());
        }
    }
    return 0;
}

int main(int argc, char** argv) {
    try {
        if (argc == 2 && (std::string(argv[1]) == "version" || std::string(argv[1]) == "--version")) {
            std::printf("DBPFX %s\n", DBPFX_VERSION);
            namespace rq = dbpfx::requirements;
            std::printf("platform: %s (%s)\n", rq::current_platform().c_str(),
                        !rq::platform_supported() ? "NOT supported"
                        : rq::platform_recommended() ? "supported, recommended" : "supported, older than recommended");
            return 0;
        }
        if (dbpfx::requirements::platform_supported() && !dbpfx::requirements::platform_recommended())
            std::fprintf(stderr, "note: %s\n", dbpfx::requirements::advisory().c_str());
        if (argc < 3) {
            std::fprintf(stderr,
                         "usage: dbpfx-cli info|list|extract|stbl|repack|verify|types <package> ...  |  dbpfx-cli version\n");
            return 2;
        }
        const std::string cmd = argv[1];
        if (cmd == "version" || cmd == "--version") {
            std::printf("DBPFX %s\n", DBPFX_VERSION);
            namespace rq = dbpfx::requirements;
            std::printf("platform: %s (%s)\n", rq::current_platform().c_str(),
                        !rq::platform_supported() ? "NOT supported"
                        : rq::platform_recommended() ? "supported, recommended" : "supported, older than recommended");
            return 0;
        }
        if (cmd == "types") return types(argc - 2, argv + 2);
        const auto p = package::open(argv[2]);
        if (cmd == "info") return info(p);
        if (cmd == "list") return list(p, argc > 3 ? std::optional<std::uint32_t>(std::stoul(argv[3], nullptr, 16)) : std::nullopt);
        if (cmd == "stbl") return stbl(p, argc > 3 ? std::stoul(argv[3]) : 20);
        if (cmd == "extract" && argc >= 5) {
            const auto i = p.find(parse_tgi(argv[3]));
            if (!i) { std::fprintf(stderr, "not found\n"); return 1; }
            const auto b = p.read(*i);
            std::ofstream(argv[4], std::ios::binary).write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
            std::printf("%zu bytes%s\n", b.size(), b.zero_copy() ? " (zero-copy)" : "");
            return 0;
        }
        if (cmd == "repack" && argc >= 4) {
            std::optional<game> gm;
            if (argc > 4) gm = std::string(argv[4]) == "sims4" ? game::sims4 : std::string(argv[4]) == "medieval" ? game::medieval : game::sims3;
            package_editor ed(p, gm);
            if (argc > 5) {
                const std::string pol = argv[5];
                ed.set_policy(pol == "always" ? package_editor::compress_policy::always
                              : pol == "never" ? package_editor::compress_policy::never
                                               : package_editor::compress_policy::keep);
                if (pol != "keep")
                    for (std::size_t i = 0; i < ed.size(); ++i) ed.set_compression(i, pol == "always");
            }
            std::printf("wrote %zu resources\n", ed.save_as(argv[3]));
            return 0;
        }
        if (cmd == "verify") {
            std::size_t bad = 0;
            for (std::size_t i = 0; i < p.size(); ++i) {
                try {
                    (void)p.read(i);
                } catch (const std::exception& e) {
                    ++bad;
                    std::printf("%s: %s\n", p.key(i).str().c_str(), e.what());
                }
            }
            std::printf("%zu resources, %zu failed\n", p.size(), bad);
            return bad ? 1 : 0;
        }
        std::fprintf(stderr, "unknown command\n");
        return 2;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
