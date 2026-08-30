#pragma once

#include <string>

#include "cube_world.hpp"

namespace cw {

struct Region {
    int x = -1;
    int y = -1;

    // Regions, not storage chunks: the world is kRegionDim per axis. Validating
    // against kGridDim rejected every real region once the granularity changed,
    // which left the mod silently inert.
    bool valid() const { return x >= 0 && y >= 0 && x < kRegionDim && y < kRegionDim; }
    bool operator==(const Region& other) const { return x == other.x && y == other.y; }
    bool operator!=(const Region& other) const { return !(*this == other); }
};

// Reads the local player's position out of the object that owns `map` and
// converts it to a region. Returns an invalid Region whenever the chain cannot
// be trusted - on the title screen the player slot is not populated yet.
Region local_player_region(WorldMap* map);

// The world's name, as the game itself uses it to build "Save/map_<name>.db".
// Empty when it cannot be read.
std::string world_name(WorldMap* map);

// True when `address` can be read for `size` bytes without faulting.
bool readable(const void* address, std::size_t size);

// Raw state used to work out which spatial unit the game calls a region.
// Everything is derived, nothing is assumed to be the answer.
struct Probe {
    bool valid = false;
    long long blockX = 0, blockY = 0;  // player position in blocks
    int cellX = 0, cellY = 0;          // WorldMap cell (block / 256)
    int chunkX = 0, chunkY = 0;        // storage chunk (cell / 64)
    int subX = 0, subY = 0;            // 8x8 subdivision of the chunk (cell / 8)
    const void* record = nullptr;      // the 0x68 record covering that subdivision
    unsigned field[8] = {};            // its first dwords, if readable
    std::string landscape;             // the name the HUD shows for the current area
    std::string detail;
    // Raw bytes of both name globals. The first attempt to interpret them
    // produced empty strings with no way to tell why, so the bytes are carried
    // through and logged rather than being silently discarded.
    unsigned char nameRaw[24] = {};
    unsigned char detailRaw[24] = {};
};

Probe probe(WorldMap* map);

}  // namespace cw
