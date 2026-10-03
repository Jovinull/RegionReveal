#pragma once

#include <cstdint>
#include <vector>

namespace rr {

// A preview of one map cell's terrain, in the layout the game's tile image uses:
// 32 x 32 columns of voxels 8 blocks on a side, RGB, black for empty.
//
// It is built from the world generator's own height function, so the relief is
// the real one; the colours are a palette, not the generator's materials, and
// there are no trees or buildings. The game swaps it for a real tile the moment
// it generates one.
struct PreviewTile {
    static constexpr int kDim = 32;

    int base = 0;   // lowest voxel layer, in 8-block units - what the cell stores at +0x04
    int depth = 0;  // voxel layers
    std::vector<std::uint8_t> voxels;  // ((z * kDim + y) * kDim + x) * 3

    const std::uint8_t* at(int x, int y, int z) const { return &voxels[((z * kDim + y) * kDim + x) * 3]; }
};

// Terrain heights in blocks at the centres of the cell's 32 x 32 voxel columns,
// plus one ring from the neighbouring cells: heights[i + 1][j + 1] is column
// (i, j). The ring is what lets a column's side reach down to its neighbour, so
// slopes have no gaps, including at the cell's edge.
struct PreviewSamples {
    static constexpr int kSpan = PreviewTile::kDim + 2;

    float heights[kSpan][kSpan] = {};
    int cellX = 0;  // seeds the colour variation, so a cell always looks the same
    int cellY = 0;
};

// Water fills every block at or below this height, as it does in generated zones.
inline constexpr int kSeaLevel = 0;

PreviewTile synthesize_preview(const PreviewSamples& samples);

}  // namespace rr
