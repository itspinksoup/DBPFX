// DBPFX unit tests (no framework needed)
// usage: dbpfx-tests [fixture-dir]
#include "dbpfx/known_types.hpp"
#include "dbpfx/package.hpp"

#include <array>
#include <cstdio>
#include <fstream>
#include <random>
#include <regex>
#include <sstream>

using namespace dbpfx;

static int g_fail = 0, g_pass = 0;
#define CHECK(c)                                                                 \
    do {                                                                         \
        if (c) ++g_pass;                                                         \
        else {                                                                   \
            ++g_fail;                                                            \
            std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);           \
        }                                                                        \
    } while (0)
#define CHECK_THROWS(expr)                                                       \
    do {                                                                         \
        bool thrown_ = false;                                                    \
        try { (void)(expr); } catch (const std::exception&) { thrown_ = true; }  \
        CHECK(thrown_);                                                          \
    } while (0)

static byte_vec bv(std::string_view s) {
    byte_vec v(s.size());
    std::memcpy(v.data(), s.data(), s.size());
    return v;
}
static byte_span sp(const byte_vec& v) { return {v.data(), v.size()}; }

// ---------------------------------------------------------------- tiny SHA-256 (for fixture manifests)
static std::string sha256(byte_span d) {
    static const std::uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
        0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
        0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
        0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
        0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
        0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    std::uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::vector<std::uint8_t> m(d.size());
    if (!d.empty()) std::memcpy(m.data(), d.data(), d.size());
    const std::uint64_t bits = std::uint64_t(d.size()) * 8;
    m.push_back(0x80);
    while (m.size() % 64 != 56) m.push_back(0);
    for (int i = 7; i >= 0; --i) m.push_back(static_cast<std::uint8_t>(bits >> (i * 8)));
    auto rr = [](std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
    for (std::size_t c = 0; c < m.size(); c += 64) {
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (std::uint32_t(m[c + 4 * i]) << 24) | (m[c + 4 * i + 1] << 16) | (m[c + 4 * i + 2] << 8) | m[c + 4 * i + 3];
        for (int i = 16; i < 64; ++i) {
            const auto s0 = rr(w[i - 15], 7) ^ rr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const auto s1 = rr(w[i - 2], 17) ^ rr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = h[0], b = h[1], cc = h[2], dd = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            const auto t1 = hh + (rr(e, 6) ^ rr(e, 11) ^ rr(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
            const auto t2 = (rr(a, 2) ^ rr(a, 13) ^ rr(a, 22)) + ((a & b) ^ (a & cc) ^ (b & cc));
            hh = g; g = f; f = e; e = dd + t1; dd = cc; cc = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += cc; h[3] += dd; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    char out[65];
    for (int i = 0; i < 8; ++i) std::snprintf(out + 8 * i, 9, "%08x", h[i]);
    return out;
}

// ---------------------------------------------------------------- data generators
static byte_vec gen(std::size_t n, int kind, std::uint32_t seed) {
    std::mt19937 r(seed);
    byte_vec v(n);
    static const char* words[] = {"sim ", "plumbob ", "lot ", "career ", "simoleon ", "llama "};
    std::size_t i = 0;
    while (i < n) {
        if (kind == 0) v[i++] = static_cast<std::byte>(r());                     // random
        else if (kind == 1) {                                                     // text
            const char* w = words[r() % 6];
            for (; *w && i < n; ++w) v[i++] = static_cast<std::byte>(*w);
        } else if (kind == 2) v[i++] = static_cast<std::byte>((i / 7) % 3);      // runs (overlapping copies)
        else v[i++] = static_cast<std::byte>(r() % 4 == 0 ? r() : 'A');         // mostly repetitive
    }
    return v;
}

// ---------------------------------------------------------------- tests
static void test_refpack() {
    std::printf("refpack\n");
    for (std::size_t n : {16u, 17u, 31u, 100u, 1023u, 1024u, 1025u, 4096u, 70000u, 300000u}) {
        for (int kind = 0; kind < 4; ++kind) {
            const auto src = gen(n, kind, static_cast<std::uint32_t>(n * 7 + kind));
            for (int level : {1, 2, 3}) {
                const auto c = compression::refpack_compress(sp(src), level);
                if (c.empty()) {
                    CHECK(kind == 0 || n < 64);  // random data, or too small to beat the 5-byte header
                    continue;
                }
                CHECK(c.size() < src.size());
                CHECK(compression::sniff(sp(c)) == compression::payload::refpack);
                CHECK(compression::refpack_size(sp(c)) == n);
                byte_vec out(n);
                compression::refpack_decompress(sp(c), {out.data(), out.size()});
                CHECK(out == src);
            }
        }
    }
    // > 16 MiB: 4-byte size header (0x90 0xFB)
    const auto big = gen(17u << 20, 1, 99);
    const auto c = compression::refpack_compress(sp(big), 1);
    CHECK(!c.empty() && static_cast<std::uint8_t>(c[0]) == 0x90);
    byte_vec out(big.size());
    compression::refpack_decompress(sp(c), {out.data(), out.size()});
    CHECK(out == big);
    // flag bytes other than 0x10/0x11/0x90/0x91 occur in the games' own packages: only bits 0x80 and
    // 0x01 change the header layout, so any other value must decode
    {
        const auto src = gen(20000, 1, 77);
        const auto packed = compression::refpack_compress(sp(src));
        for (int flags : {0x00, 0x12, 0x30, 0x40, 0x50, 0x70, 0x7E}) {
            auto variant = packed;
            variant[0] = static_cast<std::byte>(flags);
            CHECK(compression::sniff(sp(variant)) == compression::payload::refpack);
            byte_vec back(src.size());
            compression::decompress(sp(variant), {back.data(), back.size()}, 0xFFFF);
            CHECK(back == src);
        }
        // unknown data names the index's compression field and the first bytes in the error
        byte_vec junk(64, std::byte{0x42});
        byte_vec out(100);
        try {
            compression::decompress(sp(junk), {out.data(), out.size()}, 0xFFFF);
            CHECK(false);
        } catch (const format_error& e) {
            const std::string m = e.what();
            CHECK(m.find("0xFFFF") != std::string::npos && m.find("42 42") != std::string::npos);
        }
    }
    // corrupt streams must throw, not crash
    auto bad = compression::refpack_compress(sp(gen(5000, 1, 5)));
    bad.resize(bad.size() / 2);
    byte_vec o2(5000);
    CHECK_THROWS(compression::refpack_decompress(sp(bad), {o2.data(), o2.size()}));
    byte_vec wrong(10);
    CHECK_THROWS(compression::refpack_decompress(sp(compression::refpack_compress(sp(gen(5000, 1, 5)))), {wrong.data(), wrong.size()}));
}

static void test_zlib() {
    std::printf("zlib\n");
    const auto src = gen(100000, 1, 3);
    const auto c = compression::zlib_compress(sp(src));
    CHECK(!c.empty() && compression::sniff(sp(c)) == compression::payload::zlib);
    byte_vec out(src.size());
    compression::decompress(sp(c), {out.data(), out.size()});
    CHECK(out == src);
    byte_vec small(10);
    CHECK_THROWS(compression::zlib_decompress(sp(c), {small.data(), small.size()}));
}

static void test_grammar() {
    std::printf("grammar\n");
    // TS3 string table: build a model, serialize, parse lazily, compare
    g::model<schema::stbl_ts3> m;
    m.set<"version">(std::uint8_t(2));
    auto& es = m.get<"entries">();
    for (int i = 0; i < 3; ++i) {
        g::model<schema::stbl_ts3_entry> e;
        e.set<"key">(0x1000ull + i);
        e.set<"text">(utf8_to_utf16(i == 1 ? "Ñandú ✓" : "plumbob"));
        es.push_back(e);
    }
    const auto bytes = g::serialize<schema::stbl_ts3>(m);
    const auto v = g::parse<schema::stbl_ts3>(sp(bytes));
    CHECK(v.get<"count">() == 3);
    CHECK(v.get<"entries">().size() == 3);
    CHECK(v.get<"entries">()[1].get<"text">().utf8() == "Ñandú ✓");
    CHECK(v.get<"entries">()[2].get<"key">() == 0x1002);
    CHECK(v.size() == bytes.size());
    CHECK(g::serialize<schema::stbl_ts3>(v.own()) == bytes);  // lossless round trip

    // TS4 string table: computed string_bytes, UTF-8 views point into the buffer
    g::model<schema::stbl_ts4> m4;
    m4.set<"version">(std::uint16_t(5));
    for (const char* s : {"Hello", "Ñandú", ""}) {
        g::model<schema::stbl_ts4_entry> e;
        e.set<"key">(0xABCDu).set<"text">(std::string(s));
        m4.get<"entries">().push_back(e);
    }
    const auto b4 = g::serialize<schema::stbl_ts4>(m4);
    const auto v4 = g::parse<schema::stbl_ts4>(sp(b4));
    CHECK(v4.get<"count">() == 3);
    CHECK(v4.get<"string_bytes">() == (5 + 1) + (std::string("Ñandú").size() + 1) + 1);
    const auto t0 = v4.get<"entries">()[0].get<"text">();
    CHECK(t0 == "Hello");
    CHECK(reinterpret_cast<const std::byte*>(t0.data()) >= b4.data() &&
          reinterpret_cast<const std::byte*>(t0.data()) < b4.data() + b4.size());  // zero-copy

    // one_of picks the layout from the version byte
    const auto any3 = g::parse<schema::stbl>(sp(bytes));
    const auto any4 = g::parse<schema::stbl>(sp(b4));
    CHECK(any3.index() == 0 && any4.index() == 1);
    CHECK(g::serialize<schema::stbl>(g::parse<schema::stbl>(sp(b4)).index() == 1
                                         ? schema::stbl::owned_type(std::in_place_index<1>, std::get<1>(any4).own())
                                         : schema::stbl::owned_type{}) == b4);

    // validation
    auto broken = bytes;
    broken[0] = std::byte{'X'};
    CHECK_THROWS(g::parse<schema::stbl_ts3>(sp(broken)).get<"magic">());
    auto trunc = bytes;
    trunc.resize(trunc.size() - 3);
    CHECK_THROWS(g::parse<schema::stbl_ts3>(sp(trunc)).get<"entries">()[2].get<"text">());
    // header layout is folded at compile time
    static_assert(schema::dbpf_header::static_offset<schema::dbpf_header::index_of<"index_position">()>() == 0x40);
    static_assert(schema::dbpf_header::static_offset<schema::dbpf_header::index_of<"index_count">()>() == 0x24);
}

static void test_editor_roundtrip(game gm) {
    std::printf("editor round-trip (%s)\n", game_name(gm));
    package_editor ed(gm);
    ed.set_policy(package_editor::compress_policy::always);
    std::vector<std::pair<tgi, byte_vec>> want;
    for (int i = 0; i < 50; ++i) {
        const tgi k{0x220557DAu + (i % 3), static_cast<std::uint32_t>(i % 2), 0x1122334455667700ull + i};
        auto d = gen(100 + i * 97, i % 4, i);
        CHECK(ed.add(k, d).has_value());
        want.emplace_back(k, std::move(d));
    }
    CHECK(!ed.add(want[0].first, bv("dup"), true).has_value());
    const auto bytes = ed.save_to_memory();
    const auto p = package::from_memory(bytes);
    CHECK(p.size() == want.size());
    CHECK(p.minor() == (gm == game::sims4 ? 1u : 0u));
    CHECK(p.index_flags() == schema::index_flags::const_instance_hi);
    std::size_t compressed = 0;
    for (const auto& [k, d] : want) {
        const auto i = p.find(k);
        CHECK(i.has_value());
        if (!i) continue;
        const auto e = p.entry(*i);
        const auto b = p.read(*i);
        CHECK(b.copy() == d);
        if (e.file_size != e.mem_size) {
            ++compressed;
            CHECK(e.compression == static_cast<std::uint16_t>(profile_of(gm).write_compression));
            CHECK(!b.zero_copy());
        } else {
            CHECK(b.zero_copy());
        }
    }
    CHECK(compressed > 20);
    CHECK(!p.find({1, 2, 3}).has_value());

    // edit the reopened package: unchanged resources must be copied byte-for-byte
    package_editor ed2(p);
    ed2.remove(0);
    ed2.replace(1, bv("replaced"));
    ed2.set_key(2, {0xDEAD, 0xBEEF, 0xCAFE});
    const auto p2 = package::from_memory(ed2.save_to_memory());
    CHECK(p2.size() == want.size() - 1);
    CHECK(!p2.find(want[0].first));
    CHECK(p2.read(*p2.find(want[1].first)).copy() == bv("replaced"));
    CHECK(p2.read(*p2.find({0xDEAD, 0xBEEF, 0xCAFE})).copy() == want[2].second);
    CHECK(p2.index_flags() == 0);  // keys now differ in instance-high too
    for (std::size_t j = 3; j < want.size(); ++j) {
        const auto a = p.raw(*p.find(want[j].first));
        const auto b = p2.raw(*p2.find(want[j].first));
        CHECK(a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size()) == 0);
    }
}

struct manifest_entry {
    tgi key;
    std::string sha;
};
static std::vector<manifest_entry> load_manifest(const std::string& path) {
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string s = ss.str();
    std::vector<manifest_entry> r;
    const std::regex re(R"re("key": "([0-9A-F]{8})-([0-9A-F]{8})-([0-9A-F]{16})"[\s\S]*?"sha256": "([0-9a-f]{64})")re");
    for (std::sregex_iterator it(s.begin(), s.end(), re), end; it != end; ++it) {
        manifest_entry m;
        m.key.type = static_cast<std::uint32_t>(std::stoul((*it)[1], nullptr, 16));
        m.key.group = static_cast<std::uint32_t>(std::stoul((*it)[2], nullptr, 16));
        m.key.instance = std::stoull((*it)[3], nullptr, 16);
        m.sha = (*it)[4];
        r.push_back(m);
    }
    return r;
}

static void test_fixture(const std::string& dir, const char* name, game expect) {
    std::printf("fixture %s\n", name);
    const auto p = package::open(dir + "/" + name + ".package");
    const auto man = load_manifest(dir + "/" + name + ".json");
    CHECK(!man.empty());
    CHECK(p.detected_game() == expect);
    for (const auto& m : man) {
        const auto i = p.find(m.key);
        CHECK(i.has_value());
        if (i) CHECK(sha256(p.read(*i).bytes()) == m.sha);
    }
    // plain conversion with default policy: TS3 output must never contain zlib
    {
        const auto q = package::from_memory(package_editor(p, game::sims3).save_to_memory());
        for (std::size_t i = 0; i < q.size(); ++i) CHECK(game_reads(game::sims3, q.entry(i).compression));
        for (const auto& m : man) CHECK(q.find(m.key) && sha256(q.read(*q.find(m.key)).bytes()) == m.sha);
    }
    // repack in both directions and re-verify against the manifest
    for (game target : {game::sims3, game::sims4}) {
        package_editor ed(p, target);
        ed.set_policy(package_editor::compress_policy::always);
        for (std::size_t i = 0; i < ed.size(); ++i) ed.set_compression(i, true);
        const auto q = package::from_memory(ed.save_to_memory());
        for (const auto& m : man) {
            const auto i = q.find(m.key);
            CHECK(i && sha256(q.read(*i).bytes()) == m.sha);
            if (i) CHECK(game_reads(target, q.entry(*i).compression));
            if (i && target != game::sims4 && q.entry(*i).file_size != q.entry(*i).mem_size)
                CHECK(q.entry(*i).compression == 0xFFFF);  // TS3/TSM: RefPack only
        }
    }
}

static void test_typed_fixture(const std::string& dir) {
    std::printf("typed resources from fixtures\n");
    const auto p3 = package::open(dir + "/ts3.package");
    const auto i3 = p3.find_type(0x220557DA);
    CHECK(i3.size() == 1);
    const auto d3 = p3.read(i3[0]);
    const auto s3 = g::parse<schema::stbl>(d3.bytes());
    CHECK(s3.index() == 0);
    CHECK(std::get<0>(s3).get<"entries">()[1].get<"text">().utf8() == "Ñandú – ünïcødé ✓");
    const auto nm = p3.read(p3.find_type(0x0166038C).at(0));
    CHECK(nm.zero_copy());
    CHECK(g::parse<schema::name_map>(nm.bytes()).get<"entries">()[1].get<"name">() == "llama_hat");

    const auto p4 = package::open(dir + "/ts4.package");
    const auto d4 = p4.read(p4.find_type(0x220557DA).at(0));  // views borrow from the blob: keep it alive
    const auto s4 = g::parse<schema::stbl>(d4.bytes());
    CHECK(s4.index() == 1);
    CHECK(std::get<1>(s4).get<"entries">()[1].get<"text">() == "Ñandú ✓");
    // deleted record is listed but not findable and reads as empty
    std::size_t deleted = 0;
    for (std::size_t i = 0; i < p4.size(); ++i) deleted += p4.entry(i).is_deleted_record();
    CHECK(deleted == 1);
    CHECK(!p4.find({0x545AC67A, 0x80000000, 0x00F7A2C3F2E55B99}));

    const auto empty = package::open(dir + "/empty.package");
    CHECK(empty.size() == 0);
    CHECK(package::from_memory(package_editor(empty).save_to_memory()).size() == 0);
}

static void test_malformed(const std::string& dir) {
    std::printf("malformed input & fuzzing\n");
    CHECK_THROWS(package::from_memory(bv("DBPF")));
    CHECK_THROWS(package::from_memory(byte_vec(96)));
    std::ifstream f(dir + "/ts3.package", std::ios::binary);
    std::string s((std::istreambuf_iterator<char>(f)), {});
    const auto good = bv(s);
    auto bad_idx = good;
    store_le<std::uint32_t>(bad_idx.data() + 0x40, 0xFFFFFF00u);
    CHECK_THROWS(package::from_memory(bad_idx));
    auto bad_count = good;
    store_le<std::uint32_t>(bad_count.data() + 0x24, 0x10000000u);
    CHECK_THROWS(package::from_memory(bad_count));

    // random mutations: must never crash, only throw
    std::mt19937 r(7);
    std::size_t opened = 0, threw = 0;
    for (int it = 0; it < 3000; ++it) {
        auto m = good;
        const int flips = 1 + static_cast<int>(r() % 8);
        for (int k = 0; k < flips; ++k) m[r() % m.size()] = static_cast<std::byte>(r());
        try {
            const auto p = package::from_memory(std::move(m));
            ++opened;
            for (std::size_t i = 0; i < p.size(); ++i) {
                try {
                    (void)p.read(i);
                    (void)p.find(p.key(i));
                } catch (const std::exception&) { ++threw; }
            }
        } catch (const std::exception&) { ++threw; }
    }
    CHECK(opened > 0);
    std::printf("  %zu mutated packages opened, %zu errors raised cleanly\n", opened, threw);
}

static void test_editor_key_index(const std::string* dir) {
    std::printf("editor key index\n");
    package_editor ed(game::sims4);
    const tgi a{1, 0, 10}, b{1, 0, 20}, c{2, 0, 30};
    CHECK(ed.add(a, bv("a1")) == 0u);
    CHECK(ed.add(b, bv("b")) == 1u);
    CHECK(ed.add(a, bv("a2")) == 2u);          // duplicates allowed unless rejected
    CHECK(ed.find(a) == 0u);                    // first live slot wins
    ed.remove(0);
    CHECK(ed.find(a) == 2u);
    ed.restore(0);
    CHECK(ed.find(a) == 0u);
    ed.set_key(0, c);
    CHECK(ed.find(c) == 0u && ed.find(a) == 2u);
    ed.remove(2);
    CHECK(!ed.find(a));
    CHECK(ed.live_count() == 2);
    ed.remove(1);
    ed.set_key(1, a);                           // re-keying a removed slot keeps it removed
    CHECK(!ed.find(a));

    // 100k keys: lookups must stay O(1)
    package_editor big(game::sims3);
    for (std::uint64_t i = 0; i < 100000; ++i) big.add({0x220557DA, 0, i}, byte_vec{});
    std::size_t hits = 0;
    for (std::uint64_t i = 0; i < 100000; i += 7) hits += big.find({0x220557DA, 0, i}) == i;
    CHECK(hits == (100000 + 6) / 7);

    if (dir) {  // markers from a TS4 package are kept but never found
        package_editor m(package::open(*dir + "/ts4.package"));
        std::size_t markers = 0;
        for (std::size_t i = 0; i < m.size(); ++i) markers += m.marker(i);
        CHECK(markers == 1);
        CHECK(!m.find({0x545AC67A, 0x80000000, 0x00F7A2C3F2E55B99}));
        CHECK(m.live_count() == m.size() - 1);
    }
}

static void test_known_types() {
    std::printf("known types\n");
    const auto* stbl3 = known_types::find(0x220557DA, game::sims3);
    const auto* stbl4 = known_types::find(0x220557DA, game::sims4);
    CHECK(stbl3 && std::string(stbl3->tag) == "STBL");
    CHECK(stbl4 && std::string(stbl4->name) == "String table");
    CHECK(known_types::find(0x220557DA, game::medieval) == stbl3);  // Medieval shares the TS3 meanings
    // same ID, different meaning per game
    CHECK(std::string(known_types::find(0x0333406C, game::sims3)->name) == "XML resource");
    CHECK(std::string(known_types::find(0x0333406C, game::sims4)->name) == "Font configuration");
    CHECK(std::string(known_types::find(0x6017E896, game::sims4)->name) == "Buff tuning");
    CHECK(known_types::find(0x6017E896, game::sims3) == nullptr);
    CHECK(known_types::find(0x12345678, game::sims4) == nullptr);
    // table sorted by id and free of duplicate (id, game) rows - find() relies on both
    bool sorted = true, unique = true;
    const auto all = known_types::all();
    for (std::size_t i = 1; i < all.size(); ++i) {
        sorted &= all[i - 1].id <= all[i].id;
        unique &= !(all[i - 1].id == all[i].id && (all[i - 1].games & all[i].games));
    }
    CHECK(sorted && unique);
    CHECK(all.size() > 400);
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    test_known_types();
    test_refpack();
    test_zlib();
    test_grammar();
    test_editor_roundtrip(game::sims3);
    test_editor_roundtrip(game::sims4);
    const std::string fixture_dir = argc > 1 ? argv[1] : "";
    test_editor_key_index(argc > 1 ? &fixture_dir : nullptr);
    if (argc > 1) {
        const std::string dir = argv[1];
        test_fixture(dir, "ts3", game::sims3);
        test_fixture(dir, "ts4", game::sims4);
        test_fixture(dir, "single", game::sims3);
        test_typed_fixture(dir);
        test_malformed(dir);
    } else {
        std::printf("(fixture tests skipped: pass the fixture directory)\n");
    }
    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
