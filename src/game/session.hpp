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

// A map cell coordinate pair.
struct Cell {
    int x = -1;
    int y = -1;

    bool valid() const { return x >= 0 && y >= 0 && x < kMapDim && y < kMapDim; }
    Region region() const { return {region_of(x), region_of(y)}; }
};

// Reads the local player's position out of the object that owns `map` and
// converts it to the map cell under them. Returns an invalid Cell whenever the
// chain cannot be trusted. A valid answer is not proof the player is in a world:
// the title screen runs an unnamed one with a player at a placeholder position.
Cell local_player_cell(WorldMap* map);

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

// What a gameplay region's 0x68 record says its landmark is.
enum class LandmarkLookup { Ok, NoChunk, Unreadable };

struct Landmark {
    LandmarkLookup status = LandmarkLookup::NoChunk;
    unsigned raw = 0;
};

// Reads the landmark type for an arbitrary region. A region whose storage chunk
// is not resident reports NoChunk rather than inventing a value.
Landmark landmark_at(WorldMap* map, int regionX, int regionY);

}  // namespace cw
