#pragma once

#include <windows.h>

#include <cstdint>

#include "cube_world.hpp"
#include "session.hpp"
#include "signatures.hpp"

namespace cw {

// A named area as cube::World's lookup returns it. `seed` is what identifies an
// area across sessions: the pointer is only valid while the area's storage chunk
// is resident, and the generator rebuilds the area with the same seed.
struct Area {
    const void* id = nullptr;
    std::int32_t seed = 0;
    std::int32_t kind = 0;

    bool valid() const { return id != nullptr; }
    bool ocean() const { return kind < 0; }
};

// The game functions RegionReveal calls rather than hooks. All are found by
// signature in both supported builds; resolve() refuses unless every one is.
bool resolve_world_api(const ModuleRange& text, GetCellFn realGetCell);

// The area a cell belongs to, sampled at the cell's centre - the same rule the
// game's HUD uses for the area the player is standing in. Invalid when the
// area's storage chunk has not been generated yet.
Area area_at_cell(WorldMap* map, int cellX, int cellY);

// The area at an arbitrary block position.
Area area_at_block(WorldMap* map, int blockX, int blockY);

// Terrain height in blocks at a block position. Computed from noise by the
// world generator itself, so it holds for ground no zone has been built for.
float terrain_height(WorldMap* map, int blockX, int blockY);

// The cell the map is centred on: the game's view cell plus the player's pan.
Cell view_centre(WorldMap* map);

// The real cell, read through the getCell trampoline rather than the detour.
MapCell* real_cell(WorldMap* map, int cellX, int cellY);

// The lock WorldMap::render and the tile loader hold while touching cells. A
// storage chunk is only freed under it, so a cell found while holding it stays
// valid until it is released.
CRITICAL_SECTION* cell_lock(WorldMap* map);

// A tile image, allocated by the game's own allocator so the game can destroy it
// with its virtual destructor when a real tile replaces it or its chunk unloads.
struct TileImage;

// Allocates a w x h x depth image, all voxels empty. Null on failure.
TileImage* new_tile_image(WorldMap* map, int width, int height, int depth);

// The image's RGB voxel buffer, ((z * h + y) * w + x) * 3.
std::uint8_t* tile_voxels(TileImage* image);

// Builds the mesh from the voxels.
void build_tile_image(TileImage* image);

// Runs the image's virtual destructor, freeing it through the game's allocator.
void destroy_tile_image(TileImage* image);

// A dot of an area border, in blocks, as the tile loader stores them at +0x20.
struct BorderDot {
    std::int32_t x;
    std::int32_t y;
    std::int32_t z;
};

// Both must be called holding cell_lock().
void push_border_dot(MapCell* cell, const BorderDot& dot);
void clear_border_dots(MapCell* cell);

}  // namespace cw
