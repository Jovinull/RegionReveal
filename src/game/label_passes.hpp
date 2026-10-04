#pragma once

#include <cstddef>
#include <cstdint>

namespace cw {

// The map overlay's two label passes walk the cells within a radius of the
// map's centre: kLabelRadius in the game, which is less than a named area is
// wide (45 to 80 cells). The radius is an 8-bit value in the code, so it can be
// raised to kMaxLabelRadius at most.
inline constexpr int kLabelRadius = 32;
inline constexpr int kMaxLabelRadius = 127;

// Enough to reach across a whole area from its border.
inline constexpr int kDefaultLabelRadius = 96;

struct LabelPassOptions {
    // Draw points of interest - city districts, dungeon entrances - at every
    // zoom, not only when the map is zoomed in.
    bool anyZoom = true;
    // Cells walked each way from the map's centre.
    int radius = kDefaultLabelRadius;
};

// `radius` brought into kLabelRadius..kMaxLabelRadius.
int clamp_label_radius(int radius);

// Whether the draw method holds exactly the bytes the options replace. Both
// supported builds do; tests/signature_test.cpp checks them on disk.
bool label_passes_match(const std::uint8_t* draw, std::size_t drawSize);

// Rewrites the bytes of the map overlay's draw method that implement those two
// limits, only after checking every byte to be replaced; nothing is written
// unless all of them match. Must run before the game draws its first frame.
bool patch_label_passes(std::uint8_t* draw, std::size_t drawSize, const LabelPassOptions& options);

}  // namespace cw
