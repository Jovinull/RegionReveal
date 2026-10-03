#include "world.hpp"

#include <algorithm>

namespace cw {
namespace {

// cube::World's area lookup: (block x, block y) -> the area object, __thiscall.
using AreaAtFn = const void*(__thiscall*)(World*, int blockX, int blockY);

AreaAtFn g_area_at = nullptr;

// As the lookup computes it - cdq, and edx 0x3FFF, add, sar 14 - which rounds
// toward zero exactly like integer division.
int chunk_of_block(int block) { return block / kBlocksPerChunk; }

World* world_of(WorldMap* map) { return *reinterpret_cast<World**>(bytes_of(map) + kWorldMapWorld); }

const void* area_centre(World* world, int chunkX, int chunkY) {
    const auto* table = reinterpret_cast<const void* const*>(bytes_of(world) + kWorldAreaCentres);
    return table[chunkX * kGridChunks + chunkY];
}

}  // namespace

ChunkSpan chunks_consulted(int blockX, int blockY) {
    // The lookup walks the chunks holding position - 16384 to position + 16384
    // on each axis and skips any outside the table.
    return {std::max(0, chunk_of_block(blockX - kBlocksPerChunk)),
            std::max(0, chunk_of_block(blockY - kBlocksPerChunk)),
            std::min(kGridChunks - 1, chunk_of_block(blockX + kBlocksPerChunk)),
            std::min(kGridChunks - 1, chunk_of_block(blockY + kBlocksPerChunk))};
}

bool resolve_world_api(const ModuleRange& text) {
    g_area_at = reinterpret_cast<AreaAtFn>(find_unique(text, kSigWorldAreaAt));
    return g_area_at != nullptr;
}

AreaLookup area_of_cell(WorldMap* map, int cellX, int cellY) {
    World* world = world_of(map);
    if (!world || !g_area_at) return {};

    // The same sample point the game uses for "the area the player is in".
    const int blockX = cellX * kBlocksPerCell + kBlocksPerCell / 2;
    const int blockY = cellY * kBlocksPerCell + kBlocksPerCell / 2;

    const ChunkSpan span = chunks_consulted(blockX, blockY);
    for (int x = span.x0; x <= span.x1; ++x) {
        for (int y = span.y0; y <= span.y1; ++y) {
            if (!area_centre(world, x, y)) return {};
        }
    }

    // Every candidate exists, so the result is one of them; naming it by its
    // chunk needs no knowledge of the area object's layout.
    const void* area = g_area_at(world, blockX, blockY);
    for (int x = span.x0; x <= span.x1; ++x) {
        for (int y = span.y0; y <= span.y1; ++y) {
            if (area_centre(world, x, y) == area) return {true, area_id(x, y)};
        }
    }
    return {};
}

}  // namespace cw
