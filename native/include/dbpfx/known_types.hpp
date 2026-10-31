// DBPFX - resource type names for The Sims 3, The Sims Medieval and The Sims 4
// The table is generated from data/resource-types.tsv by tools/gen_resource_types.py.
#pragma once

#include "package.hpp"

#include <span>

namespace dbpfx::known_types {

struct type_info {
    std::uint32_t id;
    std::uint8_t games;  // 1 = sims3, 2 = medieval, 4 = sims4
    const char* tag;     // may be empty
    const char* name;
    const char* category;

    [[nodiscard]] const char* short_name() const { return tag[0] ? tag : name; }
};

inline constexpr type_info table[] = {
#include "known_types.inc"
};

[[nodiscard]] constexpr std::uint8_t mask_of(game g) { return static_cast<std::uint8_t>(1u << static_cast<unsigned>(g)); }

/// Meaning of a type ID in one game, or nullptr when unknown. The table is sorted by ID.
[[nodiscard]] inline const type_info* find(std::uint32_t id, game g) {
    const auto* first = std::lower_bound(std::begin(table), std::end(table), id,
                                         [](const type_info& t, std::uint32_t v) { return t.id < v; });
    for (auto* it = first; it != std::end(table) && it->id == id; ++it)
        if (it->games & mask_of(g)) return it;
    return nullptr;
}

[[nodiscard]] inline std::span<const type_info> all() { return table; }

}  // namespace dbpfx::known_types
