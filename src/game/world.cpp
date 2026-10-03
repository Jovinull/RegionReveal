#include "world.hpp"

#include <cstring>

namespace cw {
namespace {

struct World;

using AreaAtFn = const std::uint8_t*(__thiscall*)(World*, int blockX, int blockY);
using HeightFn = float(__thiscall*)(World*, int blockX, int blockY, void* zone);
using ImageCtorFn = TileImage*(__thiscall*)(TileImage*, void* renderer, int flag);
using ImageResizeFn = void(__thiscall*)(TileImage*, int w, int h, int d);
using ImageBuildFn = void(__thiscall*)(TileImage*);
using DotPushFn = void(__thiscall*)(void* list, const BorderDot* dot);
using ListClearFn = void(__thiscall*)(void* list);
using OperatorNewFn = void*(__cdecl*)(std::size_t);

struct Api {
    GetCellFn get_cell = nullptr;
    AreaAtFn area_at = nullptr;
    HeightFn height = nullptr;
    ImageCtorFn image_ctor = nullptr;
    ImageResizeFn image_resize = nullptr;
    ImageBuildFn image_build = nullptr;
    DotPushFn dot_push = nullptr;
    ListClearFn list_clear = nullptr;
    OperatorNewFn op_new = nullptr;
};

Api g_api;

template <typename Fn>
bool find(const ModuleRange& text, const char* pattern, Fn* out) {
    *out = reinterpret_cast<Fn>(find_unique(text, pattern));
    return *out != nullptr;
}

World* world_of(WorldMap* map) {
    return *reinterpret_cast<World**>(reinterpret_cast<std::uint8_t*>(map) + kWorldMapWorld);
}

Area to_area(const std::uint8_t* area) {
    if (!area) return {};
    return {area, *reinterpret_cast<const std::int32_t*>(area + kAreaSeed),
            *reinterpret_cast<const std::int32_t*>(area + kAreaKind)};
}

}  // namespace

bool resolve_world_api(const ModuleRange& text, GetCellFn realGetCell) {
    Api api;
    api.get_cell = realGetCell;

    // operator new is a thunk to MSVCR110's, which the game ships and loads, so
    // the export is the very allocator the game frees these images with.
    HMODULE crt = GetModuleHandleW(L"msvcr110.dll");
    if (!crt) return false;
    api.op_new = reinterpret_cast<OperatorNewFn>(GetProcAddress(crt, "??2@YAPAXI@Z"));

    const bool ok = api.get_cell && api.op_new && find(text, kSigWorldAreaAt, &api.area_at) &&
                    find(text, kSigWorldTerrainHeight, &api.height) &&
                    find(text, kSigVoxelImageCtor, &api.image_ctor) &&
                    find(text, kSigVoxelImageResize, &api.image_resize) &&
                    find(text, kSigVoxelImageBuild, &api.image_build) &&
                    find(text, kSigDotListPushBack, &api.dot_push) &&
                    find(text, kSigListClear, &api.list_clear);
    if (!ok) return false;
    g_api = api;
    return true;
}

Area area_at_block(WorldMap* map, int blockX, int blockY) {
    World* world = world_of(map);
    if (!world) return {};
    return to_area(g_api.area_at(world, blockX, blockY));
}

Area area_at_cell(WorldMap* map, int cellX, int cellY) {
    return area_at_block(map, cellX * kBlocksPerCell + kBlocksPerCell / 2,
                         cellY * kBlocksPerCell + kBlocksPerCell / 2);
}

float terrain_height(WorldMap* map, int blockX, int blockY) {
    return g_api.height(world_of(map), blockX, blockY, nullptr);
}

Cell view_centre(WorldMap* map) {
    const std::uint8_t* owner = owner_of(map);
    std::int32_t cell[2] = {};
    float pan[2] = {};
    if (!readable(owner + kOwnerViewCell, sizeof(cell)) || !readable(owner + kOwnerViewPan, sizeof(pan))) {
        return {};
    }
    std::memcpy(cell, owner + kOwnerViewCell, sizeof(cell));
    std::memcpy(pan, owner + kOwnerViewPan, sizeof(pan));
    // The map data worker's own arithmetic: float(cell) + pan / 256, truncated.
    Cell out{static_cast<int>(static_cast<float>(cell[0]) + pan[0] / kBlocksPerCell),
             static_cast<int>(static_cast<float>(cell[1]) + pan[1] / kBlocksPerCell)};
    return out.valid() ? out : Cell{};
}

MapCell* real_cell(WorldMap* map, int cellX, int cellY) {
    return g_api.get_cell(map, cellX, cellY);
}

CRITICAL_SECTION* cell_lock(WorldMap* map) {
    return reinterpret_cast<CRITICAL_SECTION*>(reinterpret_cast<std::uint8_t*>(map) + kWorldMapCellLock);
}

TileImage* new_tile_image(WorldMap* map, int width, int height, int depth) {
    void* memory = g_api.op_new(kTileImageSize);
    if (!memory) return nullptr;
    void* renderer = *reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(map) + kWorldMapRenderer);
    TileImage* image = g_api.image_ctor(static_cast<TileImage*>(memory), renderer, 0);
    g_api.image_resize(image, width, height, depth);
    if (!tile_voxels(image)) {
        destroy_tile_image(image);
        return nullptr;
    }
    return image;
}

std::uint8_t* tile_voxels(TileImage* image) {
    return *reinterpret_cast<std::uint8_t**>(reinterpret_cast<std::uint8_t*>(image) + kTileImageVoxels);
}

void build_tile_image(TileImage* image) { g_api.image_build(image); }

void destroy_tile_image(TileImage* image) {
    using DeletingDtorFn = void(__thiscall*)(TileImage*, int);
    (*reinterpret_cast<DeletingDtorFn**>(image))[0](image, 1);
}

void push_border_dot(MapCell* cell, const BorderDot& dot) {
    g_api.dot_push(reinterpret_cast<std::uint8_t*>(cell) + kCellBorderDots, &dot);
}

void clear_border_dots(MapCell* cell) {
    g_api.list_clear(reinterpret_cast<std::uint8_t*>(cell) + kCellBorderDots);
}

}  // namespace cw
