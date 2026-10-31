// DBPFX - declarative schemas for the DBPF container and a few core resource formats
//
// Every layout below is the single source of truth: readers, owned models and writers are
// all generated from it by grammar.hpp. Adding a format means adding a declaration here.
#pragma once

#include "grammar.hpp"

namespace dbpfx::schema {
using namespace dbpfx::g;

// ============================================================================ DBPF 2.x header
// 96 bytes. TS3 and The Sims Medieval write 2.0, The Sims 4 writes 2.1.
using dbpf_header = seq<
    field<"magic",               magic<"DBPF">>,
    field<"major",               u32>,          // 2
    field<"minor",               u32>,          // 0 = TS3/TSM, 1 = TS4
    field<"user_major",          u32>,
    field<"user_minor",          u32>,
    field<"unused1",             u32>,
    field<"created",             u32>,
    field<"updated",             u32>,
    field<"unused2",             u32>,
    field<"index_count",         u32>,
    field<"index_position_low",  u32>,          // legacy (DBPF 1.x); used when index_position == 0
    field<"index_size",          u32>,
    field<"unused3",             raw<12>>,
    field<"index_version",       u32>,          // always 3
    field<"index_position",      u32>,
    field<"unused5",             raw<28>>>;
static_assert(dbpf_header::fixed_size == 96);

// ============================================================================ DBPF 2.x index
// A flags word says which of type / group / instance-high are shared by every entry; those
// are stored once in the index header and omitted from each entry.
namespace index_flags {
inline constexpr std::uint32_t const_type = 0x1;
inline constexpr std::uint32_t const_group = 0x2;
inline constexpr std::uint32_t const_instance_hi = 0x4;
}  // namespace index_flags

using dbpf_index_entry = seq<
    field<"type",         unless<"flags", index_flags::const_type,        u32>>,
    field<"group",        unless<"flags", index_flags::const_group,       u32>>,
    field<"instance_hi",  unless<"flags", index_flags::const_instance_hi, u32>>,
    field<"instance_lo",  u32>,
    field<"offset",       u32>,
    field<"file_size",    u32>,   // bit 31 set => extended (compression) fields follow; always set in practice
    field<"mem_size",     u32>,
    field<"compression",  u16>,   // see compression::kind
    field<"committed",    u16>>;  // always 1

using dbpf_index = seq<
    field<"flags",        u32>,
    field<"type",         when<"flags", index_flags::const_type,        u32>>,
    field<"group",        when<"flags", index_flags::const_group,       u32>>,
    field<"instance_hi",  when<"flags", index_flags::const_instance_hi, u32>>,
    field<"entries",      array<uniform<dbpf_index_entry>, ref<"index_count">>>>;

// ============================================================================ STBL (0x220557DA)
// The Sims 3 / Medieval string table: version 2, UTF-16 strings, 64-bit keys.
using stbl_ts3_entry = seq<
    field<"key",    u64>,
    field<"length", count_of<"text", u32>>,
    field<"text",   utf16<ref<"length">>>>;

using stbl_ts3 = seq<
    field<"magic",    magic<"STBL">>,
    field<"version",  u8>,     // 2
    field<"unknown1", u16>,
    field<"count",    count_of<"entries", u32>>,
    field<"unknown2", u16>,
    field<"unknown3", u32>,
    field<"entries",  array<stbl_ts3_entry, ref<"count">>>>;

// The Sims 4 string table: version 5, UTF-8 strings, 32-bit FNV key hashes.
using stbl_ts4_entry = seq<
    field<"key",    u32>,
    field<"flags",  u8>,
    field<"length", count_of<"text", u16>>,
    field<"text",   utf8<ref<"length">>>>;

struct stbl_ts4_string_bytes {
    template <class M>
    static std::uint64_t compute(const M& m) {
        std::uint64_t n = 0;
        for (const auto& e : m.template get<"entries">()) n += e.template get<"text">().size() + 1;
        return n;
    }
};

using stbl_ts4 = seq<
    field<"magic",        magic<"STBL">>,
    field<"version",      u16>,   // 5
    field<"compressed",   u8>,
    field<"count",        count_of<"entries", u64>>,
    field<"reserved",     raw<2>>,
    field<"string_bytes", computed<u32, stbl_ts4_string_bytes>>,
    field<"entries",      array<stbl_ts4_entry, ref<"count">>>>;

/// Either flavour, chosen by the version byte at offset 4.
using stbl = one_of<peek<4, std::uint8_t>, case_<2, stbl_ts3>, case_<5, stbl_ts4>>;

// ============================================================================ Name map (0x0166038C)
using name_map_entry = seq<
    field<"instance", u64>,
    field<"length",   count_of<"name", u32>>,
    field<"name",     utf8<ref<"length">>>>;

using name_map = seq<
    field<"version", u32>,   // 1
    field<"count",   count_of<"entries", u32>>,
    field<"entries", array<name_map_entry, ref<"count">>>>;

}  // namespace dbpfx::schema
