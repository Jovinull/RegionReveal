#pragma once

#include <cstdint>

#include "cube_world.hpp"
#include "signatures.hpp"

namespace cw {

// A named area, identified by the storage chunk that holds its centre: the
// game keeps exactly one centre per chunk, so the chunk is a unique and stable
// name for the area across sessions.
using AreaId = std::uint32_t;

inline AreaId area_id(int chunkX, int chunkY) {
    return (static_cast<AreaId>(chunkX) << 16) | static_cast<AreaId>(chunkY);
}
inline int area_chunk_x(AreaId id) { return static_cast<int>(id >> 16); }
inline int area_chunk_y(AreaId id) { return static_cast<int>(id & 0xFFFF); }

// The storage chunks whose area centres the game's lookup compares for a block
// position: the chunk holding the position and its eight neighbours, clipped
// to the world. Pure arithmetic, kept apart so it can be tested.
struct ChunkSpan {
    int x0, y0, x1, y1;  // inclusive
};
ChunkSpan chunks_consulted(int blockX, int blockY);

// Which area a cell belongs to, decided the way the game decides it for the
// cell's centre - only when that answer is final.
//
// The game's lookup returns the nearest of the centres it has generated so
// far. Near ground the world generator has not reached, a missing neighbour
// would make it return the wrong area, so an answer is only given once every
// centre the lookup compares exists. Centres are never freed while a world is
// loaded, so a known answer stays right.
struct AreaLookup {
    bool known = false;
    AreaId area = 0;
};

// Finds the area lookup by signature. False when it is missing or ambiguous.
bool resolve_world_api(const ModuleRange& text);

AreaLookup area_of_cell(WorldMap* map, int cellX, int cellY);

}  // namespace cw
