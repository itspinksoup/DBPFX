// DBPFX benchmark: lazy mmap engine vs. a conventional eager, stream-based reader.
// usage: dbpfx-bench <file.package> [lookups]
//
// The baseline is the classic way to read a package: pull the index through a stream into one
// heap object (with its own byte array) per entry, find keys with a linear scan, and copy every
// resource into a fresh buffer.
#include "dbpfx/package.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <memory>
#include <random>

using namespace dbpfx;
using clk = std::chrono::steady_clock;
static double ms(clk::time_point a, clk::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); }

namespace baseline {
struct entry {  // owns a 36-byte copy of the record, fields decoded on access
    std::vector<std::uint8_t> raw;
    std::uint32_t u32(int at) const { std::uint32_t v; std::memcpy(&v, raw.data() + at, 4); return v; }
    std::uint32_t type() const { return u32(4); }
    std::uint32_t group() const { return u32(8); }
    std::uint64_t instance() const { return (std::uint64_t(u32(12)) << 32) | u32(16); }
};
struct pkg {
    std::ifstream f;
    std::vector<std::unique_ptr<entry>> index;
    explicit pkg(const std::string& path) : f(path, std::ios::binary) {
        std::uint8_t h[96];
        f.read(reinterpret_cast<char*>(h), 96);
        std::uint32_t count, pos, flags;
        std::memcpy(&count, h + 0x24, 4);
        std::memcpy(&pos, h + 0x40, 4);
        f.seekg(pos);
        f.read(reinterpret_cast<char*>(&flags), 4);
        std::uint32_t hdr[3] = {};
        for (int b = 0; b < 3; ++b) if (flags & (1u << b)) f.read(reinterpret_cast<char*>(&hdr[b]), 4);
        for (std::uint32_t i = 0; i < count; ++i) {
            auto e = std::make_unique<entry>();
            e->raw.resize(36);
            std::uint32_t w[9];
            w[0] = flags;
            for (int b = 0; b < 3; ++b) {
                if (flags & (1u << b)) w[1 + b] = hdr[b];
                else f.read(reinterpret_cast<char*>(&w[1 + b]), 4);
            }
            f.read(reinterpret_cast<char*>(&w[4]), 20);
            std::memcpy(e->raw.data(), w, 36);
            index.push_back(std::move(e));
        }
    }
    const entry* find(std::uint32_t t, std::uint32_t g, std::uint64_t i) const {
        for (const auto& e : index)
            if (e->type() == t && e->group() == g && e->instance() == i) return e.get();
        return nullptr;
    }
    std::vector<std::byte> read(const entry& e) {
        const std::uint32_t off = e.u32(20), fs = e.u32(24) & 0x7FFFFFFF, mem = e.u32(28);
        std::vector<std::byte> data(fs);
        f.seekg(off);
        f.read(reinterpret_cast<char*>(data.data()), fs);
        if (fs == mem) return data;
        std::vector<std::byte> out(mem);
        compression::decompress({data.data(), data.size()}, {out.data(), out.size()});
        return out;
    }
};
}  // namespace baseline

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: dbpfx-bench <file.package> [lookups]\n");
        return 2;
    }
    const std::string path = argv[1];
    const std::size_t lookups = argc > 2 ? std::stoul(argv[2]) : 2000;

    // ---------------- DBPFX
    auto t0 = clk::now();
    const auto p = package::open(path);
    auto t1 = clk::now();
    std::mt19937 r(1);
    std::vector<tgi> keys;
    for (std::size_t k = 0; k < lookups; ++k) keys.push_back(p.key(r() % p.size()));
    auto t2 = clk::now();
    std::size_t found = 0;
    for (const auto& k : keys) found += p.find(k).has_value();
    auto t3 = clk::now();
    std::uint64_t bytes = 0, zero_copy = 0;
    for (std::size_t i = 0; i < p.size(); ++i) {
        const auto b = p.read(i);
        bytes += b.size();
        zero_copy += b.zero_copy();
    }
    auto t4 = clk::now();
    std::printf("DBPFX   open %8.2f ms | %zu lookups %9.2f ms | read all %8.1f ms (%llu MB, %llu zero-copy)\n", ms(t0, t1),
                lookups, ms(t2, t3), ms(t3, t4), static_cast<unsigned long long>(bytes >> 20),
                static_cast<unsigned long long>(zero_copy));

    // ---------------- eager stream-based baseline
    t0 = clk::now();
    baseline::pkg b(path);
    t1 = clk::now();
    std::size_t found_b = 0;
    for (const auto& k : keys) found_b += b.find(k.type, k.group, k.instance) != nullptr;
    t2 = clk::now();
    std::uint64_t bytes_b = 0;
    for (const auto& e : b.index) bytes_b += b.read(*e).size();
    t3 = clk::now();
    std::printf("baseline  open %8.2f ms | %zu lookups %9.2f ms | read all %8.1f ms (%llu MB)\n", ms(t0, t1), lookups,
                ms(t1, t2), ms(t2, t3), static_cast<unsigned long long>(bytes_b >> 20));
    return (found == lookups && found_b == lookups && bytes == bytes_b) ? 0 : 1;
}
