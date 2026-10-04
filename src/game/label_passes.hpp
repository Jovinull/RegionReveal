#pragma once

#include <cstddef>
#include <cstdint>

namespace cw {

// The map overlay's two label passes walk the cells within kLabelRadius of the
// map's centre. With the wider range they walk kWideLabelRadius instead, which
// covers a whole named area (45 to 80 cells across) around the centre.
inline constexpr int kLabelRadius = 0x20;
inline constexpr int kWideLabelRadius = 0x40;

struct LabelPassOptions {
    // Draw points of interest - city districts, dungeon entrances - at every
    // zoom, not only when the map is zoomed in.
    bool anyZoom = true;
    // Walk kWideLabelRadius cells around the map's centre instead of 32.
    bool wideRange = true;
};

// Whether the draw method holds exactly the bytes both changes replace. Both
// supported builds do; tests/signature_test.cpp checks them on disk.
bool label_passes_match(const std::uint8_t* draw, std::size_t drawSize);

// Rewrites the bytes of the map overlay's draw method that implement those two
// limits, only after checking every byte to be replaced; nothing is written
// unless all of them match. Must run before the game draws its first frame.
bool patch_label_passes(std::uint8_t* draw, std::size_t drawSize, const LabelPassOptions& options);

}  // namespace cw
