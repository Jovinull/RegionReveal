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
// Each chunk covers 64x64 map cells of 0x34 bytes, starting 8 bytes into it.
inline constexpr int kGridOffset = 0xB0;
inline constexpr int kGridDim = 1024;
inline constexpr int kChunkDim = 64;
inline constexpr int kChunkHeader = 8;
inline constexpr int kCellStride = 0x34;
inline constexpr int kMapDim = kGridDim * kChunkDim;  // 65536 cells per axis

// Offsets inside a map cell.
inline constexpr int kCellKind = 0x10;   // zero for cells the renderer skips
inline constexpr int kCellFlags = 0x30;  // bit 0 = revealed on the world map
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

inline std::uint8_t* cell_kind(MapCell* cell) {
    return reinterpret_cast<std::uint8_t*>(cell) + kCellKind;
}

inline void** chunk_grid(WorldMap* map) {
    return reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(map) + kGridOffset);
}

inline void* chunk_at(WorldMap* map, int cx, int cy) {
    if (cx < 0 || cy < 0 || cx >= kGridDim || cy >= kGridDim) return nullptr;
    return chunk_grid(map)[cx * kGridDim + cy];
}

inline MapCell* cell_in_chunk(void* chunk, int ix, int iy) {
    auto* base = static_cast<std::uint8_t*>(chunk) + kChunkHeader;
    return reinterpret_cast<MapCell*>(base + (ix * kChunkDim + iy) * kCellStride);
}

// __thiscall with stack arguments; the detours below mirror it using __fastcall,
// which puts `self` in ECX and leaves the integer arguments on the stack.
using GetCellFn = MapCell*(__thiscall*)(WorldMap*, int x, int y);
using DiscoverFn = void(__thiscall*)(WorldMap*, int x, int y);

}  // namespace cw
