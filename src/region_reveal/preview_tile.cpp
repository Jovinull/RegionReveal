#include "preview_tile.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace rr {
namespace {

constexpr int kDim = PreviewTile::kDim;
constexpr int kBlocksPerVoxel = 8;

struct Rgb {
    int r, g, b;
};

// Measured on the map, not taken from the generator: real tiles top grass in
// saturated greens around (56, 233, 91) and (0, 137, 0), and cliffs in a pale
// lavender grey around (201, 201, 217).
constexpr Rgb kGrassLow{58, 190, 66};
constexpr Rgb kGrassHigh{96, 170, 58};
constexpr Rgb kSand{226, 212, 150};
constexpr Rgb kRock{186, 184, 200};
constexpr Rgb kSnow{238, 240, 248};
constexpr Rgb kDirt{132, 100, 66};
constexpr Rgb kShallow{70, 150, 235};
constexpr Rgb kWater{40, 110, 220};
constexpr Rgb kDeep{28, 78, 196};

// Above this many blocks of rise between neighbouring columns, 8 blocks apart,
// the ground is drawn as rock rather than grass.
constexpr int kCliffRise = 12;
constexpr int kSnowLine = 380;
constexpr int kBeach = 2;

int floor_div(int value, int by) { return value >= 0 ? value / by : -((-value + by - 1) / by); }

int voxel_layer(int block) { return floor_div(block, kBlocksPerVoxel); }

std::uint32_t hash(int x, int y) {
    std::uint32_t h = static_cast<std::uint32_t>(x) * 0x8DA6B343u ^ static_cast<std::uint32_t>(y) * 0xD8163841u;
    h ^= h >> 13;
    h *= 0x85EBCA6Bu;
    h ^= h >> 16;
    return h;
}

Rgb mix(Rgb a, Rgb b, float t) {
    t = std::min(1.0f, std::max(0.0f, t));
    return {static_cast<int>(a.r + (b.r - a.r) * t), static_cast<int>(a.g + (b.g - a.g) * t),
            static_cast<int>(a.b + (b.b - a.b) * t)};
}

// Small brightness variation, so a flat expanse does not read as one slab.
Rgb jitter(Rgb c, std::uint32_t h, int amount) {
    const int d = static_cast<int>(h % static_cast<std::uint32_t>(amount * 2 + 1)) - amount;
    return {c.r + d, c.g + d, c.b + d};
}

void put(PreviewTile& tile, int x, int y, int layer, Rgb c) {
    std::uint8_t* v = &tile.voxels[((layer * kDim + y) * kDim + x) * 3];
    // Black is the empty voxel, so a colour may never collapse to it.
    v[0] = static_cast<std::uint8_t>(std::min(255, std::max(1, c.r)));
    v[1] = static_cast<std::uint8_t>(std::min(255, std::max(1, c.g)));
    v[2] = static_cast<std::uint8_t>(std::min(255, std::max(1, c.b)));
}

}  // namespace

PreviewTile synthesize_preview(const PreviewSamples& samples) {
    constexpr int kSpan = PreviewSamples::kSpan;

    int top[kSpan][kSpan];
    int layer[kSpan][kSpan];  // the column's top voxel; water sits at sea level
    for (int i = 0; i < kSpan; ++i) {
        for (int j = 0; j < kSpan; ++j) {
            top[i][j] = static_cast<int>(std::floor(samples.heights[i][j]));
            layer[i][j] = voxel_layer(std::max(top[i][j], kSeaLevel));
        }
    }

    // A column is filled from its top voxel down to just above its lowest
    // neighbour, so steps and cliffs show their sides instead of gaps. Water is
    // a single surface layer: the sea floor would be hidden under it anyway.
    int from[kDim][kDim];
    int lowest = 1 << 30;
    int highest = -(1 << 30);
    for (int i = 0; i < kDim; ++i) {
        for (int j = 0; j < kDim; ++j) {
            const int own = layer[i + 1][j + 1];
            const int neighbour = std::min(std::min(layer[i][j + 1], layer[i + 2][j + 1]),
                                           std::min(layer[i + 1][j], layer[i + 1][j + 2]));
            from[i][j] = top[i + 1][j + 1] < kSeaLevel ? own : std::min(own, neighbour + 1);
            lowest = std::min(lowest, from[i][j]);
            highest = std::max(highest, own);
        }
    }

    PreviewTile tile;
    tile.base = lowest;
    tile.depth = highest - lowest + 1;
    tile.voxels.assign(static_cast<std::size_t>(kDim) * kDim * tile.depth * 3, 0);

    for (int i = 0; i < kDim; ++i) {
        for (int j = 0; j < kDim; ++j) {
            const int t = top[i + 1][j + 1];
            const int bx = samples.cellX * kDim + i;
            const int by = samples.cellY * kDim + j;
            const std::uint32_t h = hash(bx, by);
            const int z = layer[i + 1][j + 1] - tile.base;

            if (t < kSeaLevel) {
                const int depth = kSeaLevel - t;
                const Rgb water = depth < 8 ? kShallow : depth < 24 ? kWater : kDeep;
                put(tile, i, j, z, jitter(water, h, 3));
                continue;
            }

            int rise = 0;
            const int around[4] = {top[i][j + 1], top[i + 2][j + 1], top[i + 1][j], top[i + 1][j + 2]};
            for (const int n : around) rise = std::max(rise, std::abs(t - std::max(n, kSeaLevel)));

            Rgb surface;
            Rgb side = kDirt;
            if (t >= kSnowLine) {
                surface = kSnow;
                side = kRock;
            } else if (rise >= kCliffRise) {
                surface = kRock;
                side = kRock;
            } else if (t <= kSeaLevel + kBeach) {
                surface = kSand;
            } else {
                surface = mix(kGrassLow, kGrassHigh, static_cast<float>(t) / 300.0f);
            }

            put(tile, i, j, z, jitter(surface, h, 8));
            for (int below = from[i][j] - tile.base; below < z; ++below) {
                put(tile, i, j, below, jitter(side, hash(bx + below, by), 6));
            }
        }
    }
    return tile;
}

}  // namespace rr
