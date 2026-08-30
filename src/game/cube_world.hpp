// Layout of the Cube World Alpha structures RegionReveal touches.
//
// Every constant here was recovered statically from Cube.exe and holds for both
// supported builds (2013-07-02 and 2013-07-20); only function addresses differ,
// which is why they are resolved by signature at runtime instead.
// See docs/REVERSE_ENGINEERING.md for the evidence behind each field.

#pragma once

#include <cstdint>

namespace cw {

// cube::WorldMap owns a sparse 1024x1024 grid of chunk pointers at +0xB0.
// A chunk is one region: 64x64 cells of 0x34 bytes at offset 0, followed by
// 64 objects of 0x68. The save file calls these chunks "reg<x>_<y>", which is
// where the claim that a chunk is a region comes from.
inline constexpr int kGridOffset = 0xB0;
inline constexpr int kGridDim = 1024;
inline constexpr int kChunkDim = 64;
inline constexpr int kCellStride = 0x34;
inline constexpr int kMapDim = kGridDim * kChunkDim;  // 65536 cells per axis

// Offsets inside a map cell. The cell is cube::ZoneTile: its constructor
// writes that class's RTTI vftable.
inline constexpr int kCellUnknown10 = 0x10;  // zero for cells the renderer skips
inline constexpr int kCellFlags = 0x30;      // bit 0 = revealed on the world map
inline constexpr std::uint8_t kRevealedBit = 0x01;

// Offsets inside cube::WorldMap.
inline constexpr int kWorldMapLock = 0x8000C0;      // CRITICAL_SECTION
inline constexpr int kWorldMapRevealCount = 0x8000BC;  // persisted as key "discovered"

// cube::MapOverlayWidget reaches its WorldMap through this chain.
inline constexpr int kOverlayGameObject = 0x160;
inline constexpr int kGameObjectWorldMap = 0x800D44;

struct WorldMap;
struct MapCell;

inline int chunk_of(int cell) { return cell >> 6; }

inline std::uint8_t* cell_flags(MapCell* cell) {
    return reinterpret_cast<std::uint8_t*>(cell) + kCellFlags;
}

inline std::uint8_t* cell_unknown10(MapCell* cell) {
    return reinterpret_cast<std::uint8_t*>(cell) + kCellUnknown10;
}

inline void** chunk_grid(WorldMap* map) {
    return reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(map) + kGridOffset);
}

inline void* chunk_at(WorldMap* map, int cx, int cy) {
    if (cx < 0 || cy < 0 || cx >= kGridDim || cy >= kGridDim) return nullptr;
    return chunk_grid(map)[cx * kGridDim + cy];
}

// Mirrors cube::WorldMap::getCell: the cell array starts at the chunk itself,
// with no header. An earlier version of this file assumed 8 bytes of header,
// which was wrong; getCell computes chunk + index * 0x34.
inline MapCell* cell_in_chunk(void* chunk, int ix, int iy) {
    auto* base = static_cast<std::uint8_t*>(chunk);
    return reinterpret_cast<MapCell*>(base + (ix * kChunkDim + iy) * kCellStride);
}

// __thiscall with stack arguments; the detours below mirror it using __fastcall,
// which puts `self` in ECX and leaves the integer arguments on the stack.
using GetCellFn = MapCell*(__thiscall*)(WorldMap*, int x, int y);
using DiscoverFn = void(__thiscall*)(WorldMap*, int x, int y);

}  // namespace cw
