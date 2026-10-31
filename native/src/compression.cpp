#include "dbpfx/compression.hpp"

#include <zlib.h>

#include <limits>

namespace dbpfx::compression {

namespace {
inline std::uint8_t b(const std::byte* p, std::size_t i) { return static_cast<std::uint8_t>(p[i]); }
}  // namespace

payload sniff(byte_span d) noexcept {
    if (d.size() >= 2) {
        const auto b0 = static_cast<std::uint8_t>(d[0]), b1 = static_cast<std::uint8_t>(d[1]);
        // zlib: CMF 0x78 and a valid FCHECK. Never clashes with RefPack, because 0x78FB fails FCHECK.
        if (b0 == 0x78 && ((b0 << 8) | b1) % 31 == 0) return payload::zlib;
        // RefPack: [flags][0xFB]. Only flags 0x80 (4-byte sizes) and 0x01 (compressed-size field) change
        // the layout; the game's own packages also use other flag bits, so any flags byte is accepted.
        if (b1 == 0xFB) return payload::refpack;
    }
    return d.empty() ? payload::raw : payload::unknown;
}

// ============================================================================ RefPack
// Header: [flags][0xFB][size: 3 or 4 bytes big-endian]
//   flags & 0x80 -> sizes are 4 bytes      flags & 0x01 -> a compressed-size field precedes
static std::size_t refpack_header(byte_span in, std::size_t& size) {
    if (in.size() < 5) throw format_error("RefPack: truncated header");
    const std::uint8_t flags = b(in.data(), 0);
    if (b(in.data(), 1) != 0xFB) throw format_error("RefPack: bad signature");
    const std::size_t w = (flags & 0x80) ? 4 : 3;
    std::size_t pos = 2 + ((flags & 0x01) ? w : 0);
    if (in.size() < pos + w) throw format_error("RefPack: truncated header");
    size = 0;
    for (std::size_t i = 0; i < w; ++i) size = (size << 8) | b(in.data(), pos + i);
    return pos + w;
}

std::size_t refpack_size(byte_span in) {
    std::size_t s = 0;
    refpack_header(in, s);
    return s;
}

std::size_t refpack_decompress(byte_span in, std::span<std::byte> out) {
    std::size_t expected = 0;
    std::size_t ip = refpack_header(in, expected);
    if (expected != out.size())
        throw format_error("RefPack: header size " + std::to_string(expected) + " != expected " + std::to_string(out.size()));

    const std::byte* src = in.data();
    const std::size_t n = in.size();
    std::byte* dst = out.data();
    const std::size_t cap = out.size();
    std::size_t op = 0;

    auto literal = [&](std::size_t k) {
        if (ip + k > n || op + k > cap) throw format_error("RefPack: literal overruns buffer");
        std::memcpy(dst + op, src + ip, k);
        ip += k;
        op += k;
    };
    auto copy = [&](std::size_t len, std::size_t off) {
        if (off > op || op + len > cap) throw format_error("RefPack: back-reference out of range");
        std::byte* d = dst + op;
        const std::byte* s = d - off;
        if (off >= len) {
            std::memcpy(d, s, len);
        } else {
            for (std::size_t i = 0; i < len; ++i) d[i] = s[i];  // overlapping run
        }
        op += len;
    };

    while (op < cap) {
        if (ip >= n) throw format_error("RefPack: unexpected end of stream");
        const std::uint8_t b0 = b(src, ip);
        if (b0 < 0x80) {
            if (ip + 2 > n) throw format_error("RefPack: truncated command");
            const std::uint8_t b1 = b(src, ip + 1);
            ip += 2;
            literal(b0 & 0x03);
            copy(((b0 & 0x1C) >> 2) + 3, ((b0 & 0x60) << 3) + b1 + 1);
        } else if (b0 < 0xC0) {
            if (ip + 3 > n) throw format_error("RefPack: truncated command");
            const std::uint8_t b1 = b(src, ip + 1), b2 = b(src, ip + 2);
            ip += 3;
            literal((b1 >> 6) & 0x03);
            copy((b0 & 0x3F) + 4, ((b1 & 0x3F) << 8) + b2 + 1);
        } else if (b0 < 0xE0) {
            if (ip + 4 > n) throw format_error("RefPack: truncated command");
            const std::uint8_t b1 = b(src, ip + 1), b2 = b(src, ip + 2), b3 = b(src, ip + 3);
            ip += 4;
            literal(b0 & 0x03);
            copy(((b0 & 0x0C) << 6) + b3 + 5, ((b0 & 0x10) << 12) + (b1 << 8) + b2 + 1);
        } else if (b0 < 0xFC) {
            ip += 1;
            literal(((b0 & 0x1F) << 2) + 4);
        } else {
            ip += 1;
            literal(b0 & 0x03);
            break;  // end of stream
        }
    }
    if (op != cap) throw format_error("RefPack: stream ended early");
    return op;
}

namespace {

class refpack_encoder {
public:
    refpack_encoder(byte_span in, int level) : in_(in), out_w_(out_) {
        depth_ = level <= 1 ? 8 : level == 2 ? 48 : 256;
        lazy_ = level >= 2;
        hash_bits_ = 10;
        while (hash_bits_ < 16 && (std::size_t(1) << hash_bits_) < in.size()) ++hash_bits_;
        ring_ = std::min(window, std::bit_ceil(std::max<std::size_t>(in.size(), 1)));
        head_.assign(std::size_t(1) << hash_bits_, -1);
        prev_.assign(ring_, -1);
    }

    byte_vec run() {
        const std::size_t n = in_.size();
        // header
        if (n < (1u << 24)) {
            out_w_.put<std::uint8_t>(0x10);
            out_w_.put<std::uint8_t>(0xFB);
            for (int s = 16; s >= 0; s -= 8) out_w_.put<std::uint8_t>(static_cast<std::uint8_t>(n >> s));
        } else if (n <= 0xFFFFFFFFu) {
            out_w_.put<std::uint8_t>(0x90);
            out_w_.put<std::uint8_t>(0xFB);
            for (int s = 24; s >= 0; s -= 8) out_w_.put<std::uint8_t>(static_cast<std::uint8_t>(n >> s));
        } else {
            return {};
        }

        std::size_t i = 0, lit_start = 0;
        while (i < n) {
            std::size_t len = 0, off = 0;
            find(i, len, off);
            if (len && lazy_ && i + 1 < n) {
                insert(i);
                std::size_t len2 = 0, off2 = 0;
                find(i + 1, len2, off2);
                if (len2 > len + 1) {  // a better match starts one byte later
                    ++i;
                    len = len2;
                    off = off2;
                } else {
                    // undo nothing: position i already inserted; mark so we don't insert twice
                    emit(lit_start, i, len, off);
                    for (std::size_t k = 1; k < len; ++k) insert(i + k);
                    i += len;
                    lit_start = i;
                    continue;
                }
            }
            if (len) {
                emit(lit_start, i, len, off);
                for (std::size_t k = 0; k < len; ++k) insert(i + k);
                i += len;
                lit_start = i;
            } else {
                insert(i);
                ++i;
            }
            if (out_.size() >= n + 16) return {};  // not worth it
        }
        // trailing literals: blocks of 4..112, then the 0..3 remainder in the stop command
        std::size_t lits = n - lit_start;
        std::size_t p = lit_start;
        while (lits >= 4) {
            const std::size_t k = std::min<std::size_t>(lits & ~std::size_t(3), 112);
            out_w_.put<std::uint8_t>(static_cast<std::uint8_t>(0xE0 + ((k - 4) >> 2)));
            out_w_.put_bytes(in_.data() + p, k);
            p += k;
            lits -= k;
        }
        out_w_.put<std::uint8_t>(static_cast<std::uint8_t>(0xFC + lits));
        out_w_.put_bytes(in_.data() + p, lits);
        if (out_.size() >= n) return {};
        return std::move(out_);
    }

private:
    static constexpr std::size_t window = 131072;
    static constexpr std::size_t max_len = 1028;

    [[nodiscard]] std::uint32_t hash(std::size_t i) const {
        const auto* p = in_.data() + i;
        const std::uint32_t v = b(p, 0) | (b(p, 1) << 8) | (b(p, 2) << 16);
        return (v * 2654435761u) >> (32 - hash_bits_);
    }
    void insert(std::size_t i) {
        if (i + 3 > in_.size()) return;
        const auto h = hash(i);
        prev_[i & (ring_ - 1)] = head_[h];
        head_[h] = static_cast<std::int64_t>(i);
    }
    static bool encodable(std::size_t len, std::size_t off) {
        return (len >= 3 && off <= 1024) || (len >= 4 && off <= 16384) || (len >= 5 && off <= window);
    }
    void find(std::size_t i, std::size_t& best_len, std::size_t& best_off) const {
        best_len = 0;
        const std::size_t n = in_.size();
        if (i + 3 > n) return;
        const std::size_t limit = std::min(max_len, n - i);
        const auto* base = in_.data();
        std::int64_t cand = head_[hash(i)];
        for (int d = 0; d < depth_ && cand >= 0; ++d) {
            const std::size_t c = static_cast<std::size_t>(cand);
            const std::size_t off = i - c;
            if (off == 0 || off > window || off > ring_) break;
            if (base[c + best_len] == base[i + best_len]) {
                std::size_t l = 0;
                while (l < limit && base[c + l] == base[i + l]) ++l;
                if (l > best_len && encodable(l, off)) {
                    best_len = l;
                    best_off = off;
                    if (l == limit) break;
                }
            }
            const std::int64_t nx = prev_[c & (ring_ - 1)];
            if (nx >= cand) break;  // ring slot was overwritten by a newer position
            cand = nx;
        }
    }
    void emit(std::size_t lit_start, std::size_t at, std::size_t len, std::size_t off) {
        std::size_t lits = at - lit_start;
        std::size_t p = lit_start;
        while (lits > 3) {
            const std::size_t k = std::min<std::size_t>(lits & ~std::size_t(3), 112);
            out_w_.put<std::uint8_t>(static_cast<std::uint8_t>(0xE0 + ((k - 4) >> 2)));
            out_w_.put_bytes(in_.data() + p, k);
            p += k;
            lits -= k;
        }
        const auto plain = static_cast<std::uint8_t>(lits);
        const std::size_t o = off - 1;
        if (len <= 10 && off <= 1024) {
            out_w_.put<std::uint8_t>(static_cast<std::uint8_t>(((o >> 3) & 0x60) | ((len - 3) << 2) | plain));
            out_w_.put<std::uint8_t>(static_cast<std::uint8_t>(o & 0xFF));
        } else if (len <= 67 && off <= 16384) {
            out_w_.put<std::uint8_t>(static_cast<std::uint8_t>(0x80 | (len - 4)));
            out_w_.put<std::uint8_t>(static_cast<std::uint8_t>((plain << 6) | (o >> 8)));
            out_w_.put<std::uint8_t>(static_cast<std::uint8_t>(o & 0xFF));
        } else {
            out_w_.put<std::uint8_t>(static_cast<std::uint8_t>(0xC0 | ((o >> 12) & 0x10) | (((len - 5) >> 6) & 0x0C) | plain));
            out_w_.put<std::uint8_t>(static_cast<std::uint8_t>((o >> 8) & 0xFF));
            out_w_.put<std::uint8_t>(static_cast<std::uint8_t>(o & 0xFF));
            out_w_.put<std::uint8_t>(static_cast<std::uint8_t>((len - 5) & 0xFF));
        }
        out_w_.put_bytes(in_.data() + p, lits);
    }

    byte_span in_;
    byte_vec out_;
    writer out_w_;
    int depth_ = 48;
    unsigned hash_bits_ = 16;
    std::size_t ring_ = window;
    bool lazy_ = true;
    std::vector<std::int64_t> head_, prev_;
};

}  // namespace

byte_vec refpack_compress(byte_span in, int level) {
    if (in.size() < 16) return {};
    return refpack_encoder(in, level).run();
}

// ============================================================================ zlib
std::size_t zlib_decompress(byte_span in, std::span<std::byte> out) {
    z_stream zs{};
    if (inflateInit(&zs) != Z_OK) throw format_error("zlib: inflateInit failed");
    struct closer {
        z_stream* z;
        ~closer() { inflateEnd(z); }
    } close_on_exit{&zs};

    const std::size_t chunk = std::numeric_limits<uInt>::max();
    std::size_t in_pos = 0, done = 0;
    int rc = Z_OK;
    while (rc == Z_OK && done < out.size()) {
        zs.next_in = reinterpret_cast<Bytef*>(const_cast<std::byte*>(in.data() + in_pos));
        zs.avail_in = static_cast<uInt>(std::min(in.size() - in_pos, chunk));
        zs.next_out = reinterpret_cast<Bytef*>(out.data() + done);
        zs.avail_out = static_cast<uInt>(std::min(out.size() - done, chunk));
        const uInt ai = zs.avail_in, ao = zs.avail_out;
        rc = inflate(&zs, Z_NO_FLUSH);
        in_pos += ai - zs.avail_in;
        done += ao - zs.avail_out;
        if (rc == Z_OK && ai == zs.avail_in && ao == zs.avail_out) break;  // no progress: truncated input
    }
    if (rc == Z_OK && done == out.size()) {
        // Output is full; the stream must end here (only the checksum may remain).
        std::byte probe{};
        zs.next_in = reinterpret_cast<Bytef*>(const_cast<std::byte*>(in.data() + in_pos));
        zs.avail_in = static_cast<uInt>(std::min(in.size() - in_pos, chunk));
        zs.next_out = reinterpret_cast<Bytef*>(&probe);
        zs.avail_out = 1;
        rc = inflate(&zs, Z_NO_FLUSH);
        if (zs.avail_out == 0) throw format_error("zlib: stream holds more data than the index says (mem_size too small)");
    }
    if (rc != Z_STREAM_END) throw format_error("zlib: corrupt or truncated stream (" + std::to_string(rc) + ")");
    if (done != out.size())
        throw format_error("zlib: produced " + std::to_string(done) + " bytes, expected " + std::to_string(out.size()));
    return done;
}

byte_vec zlib_compress(byte_span in, int level) {
    if (in.empty()) return {};
    uLongf cap = compressBound(static_cast<uLong>(in.size()));
    byte_vec out(cap);
    if (compress2(reinterpret_cast<Bytef*>(out.data()), &cap, reinterpret_cast<const Bytef*>(in.data()),
                  static_cast<uLong>(in.size()), level) != Z_OK)
        throw format_error("zlib: compress2 failed");
    if (cap >= in.size()) return {};
    out.resize(cap);
    return out;
}

static std::string describe(byte_span in, std::uint16_t method) {
    static const char* hex = "0123456789ABCDEF";
    std::string s = "compression field 0x";
    for (int shift = 12; shift >= 0; shift -= 4) s += hex[(method >> shift) & 0xF];
    s += ", " + std::to_string(in.size()) + " stored bytes starting";
    for (std::size_t i = 0; i < in.size() && i < 12; ++i) {
        const auto v = static_cast<std::uint8_t>(in[i]);
        s += ' ';
        s += hex[v >> 4];
        s += hex[v & 0xF];
    }
    return s;
}

void decompress(byte_span in, std::span<std::byte> out, std::uint16_t method) {
    const payload p = sniff(in);
    // The bytes decide when they are unambiguous; otherwise trust the index's compression field.
    if (p == payload::zlib) {
        zlib_decompress(in, out);
        return;
    }
    if (p == payload::refpack) {
        refpack_decompress(in, out);
        return;
    }
    if (method == static_cast<std::uint16_t>(kind::zlib)) {
        zlib_decompress(in, out);
        return;
    }
    throw format_error("unrecognized compression format (" + describe(in, method) + ")");
}

}  // namespace dbpfx::compression
