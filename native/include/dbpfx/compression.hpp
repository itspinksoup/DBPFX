// DBPFX - resource compression (RefPack for TS3/TSM/TS4, zlib for TS4)
#pragma once

#include "platform.hpp"

namespace dbpfx::compression {

/// Value of the 16-bit "compression" field of an index entry.
enum class kind : std::uint16_t {
    none = 0x0000,
    zlib = 0x5A42,        // "ZB": what The Sims 4 writes
    streamable = 0xFFFE,  // TS4 streamable RefPack
    refpack = 0xFFFF,     // what TS3 / Medieval write; TS4 reads it too
    deleted = 0xFFE0,     // TS4 deleted-record marker
};

/// What the payload actually contains, detected from its first bytes.
enum class payload { raw, refpack, zlib, unknown };
[[nodiscard]] payload sniff(byte_span data) noexcept;

/// Uncompressed size announced inside a RefPack stream header.
[[nodiscard]] std::size_t refpack_size(byte_span data);

/// Decompress into a caller-provided buffer of exactly the expected size.
/// Returns the number of bytes produced (== out.size() on success).
std::size_t refpack_decompress(byte_span in, std::span<std::byte> out);
std::size_t zlib_decompress(byte_span in, std::span<std::byte> out);

/// Pick the right decoder from the payload bytes (falling back to the index entry's compression
/// field when the bytes are ambiguous) and fill `out` (size = index mem_size).
void decompress(byte_span in, std::span<std::byte> out, std::uint16_t method = 0);

/// Compress. Return an empty vector when compression would not make the data smaller.
[[nodiscard]] byte_vec refpack_compress(byte_span in, int level = 2);
[[nodiscard]] byte_vec zlib_compress(byte_span in, int level = 6);

}  // namespace dbpfx::compression
