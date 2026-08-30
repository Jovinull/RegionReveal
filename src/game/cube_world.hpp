// Layout of the Cube World Alpha structures RegionReveal touches.
//
// Every constant here was recovered statically from Cube.exe and holds for both
// supported builds; only function addresses differ, which is why those are
// resolved by signature at runtime instead.
// See docs/REVERSE_ENGINEERING.md and docs/AUDIT.md for the evidence.

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

// After the cell array a chunk holds 64 records of 0x68, an 8x8 grid over the
// chunk's cells. The marker pass reads them; cube::Region builds an identical
// set at its own +0x14018, from the same constructor.
inline constexpr int kChunkRecords = kChunkDim * kChunkDim * kCellStride;  // 0x34000
inline constexpr int kRecordStride = 0x68;

// Offsets inside a map cell. The cell is cube::ZoneTile: its constructor
// writes that class's RTTI vftable.
inline constexpr int kCellContent = 0x10;  // non-zero for cells the terrain pass draws
inline constexpr int kCellFlags = 0x30;    // bit 0 = revealed on the world map
inline constexpr std::uint8_t kRevealedBit = 0x01;

// Offsets inside cube::WorldMap.
inline constexpr int kWorldMapLock = 0x8000C0;         // CRITICAL_SECTION
inline constexpr int kWorldMapRevealCount = 0x8000BC;  // persisted as key "discovered"
inline constexpr int kWorldMapOwnerInfo = 0xAC;        // ctor arg 2; carries the world name

// The WorldMap is constructed in place inside its owner, which the map widget
// also reaches through `widget + 0x160`: `lea ecx, [ebx + 0x800D44]` sits
// directly before the constructor call at 0x45A5B2. Subtracting gets back from
// a WorldMap to that owner without touching any global.
inline constexpr int kOwnerToWorldMap = 0x800D44;
inline constexpr int kOwnerToLocalPlayer = 0x8006D0;  // -> cube::Creature*

// cube::Creature. Position is 64-bit fixed point with 16 fractional bits.
inline constexpr int kPlayerPosX = 0x10;
inline constexpr int kPlayerPosY = 0x18;
inline constexpr std::int64_t kPosFractionalDivisor = 65536;
inline constexpr int kBlocksPerCell = 256;

// The world name is a std::string at owner-info + 0x94; 0x5FBC90 concatenates
// "Save/map_" + that + ".db" to reach the map database.
inline constexpr int kWorldInfoName = 0x94;

// MSVC 2012 std::string: inline buffer or heap pointer at +0, size at +0x10,
// capacity at +0x14. Short strings live in the buffer while capacity < 16.
inline constexpr int kStdStringSize = 0x10;
inline constexpr int kStdStringCapacity = 0x14;
inline constexpr std::uint32_t kStdStringSsoCapacity = 16;

struct WorldMap;
struct MapCell;

inline int region_of(int cell) { return cell >> 6; }

inline std::uint8_t* cell_flags(MapCell* cell) {
    return reinterpret_cast<std::uint8_t*>(cell) + kCellFlags;
}

inline std::uint8_t* cell_content(MapCell* cell) {
    return reinterpret_cast<std::uint8_t*>(cell) + kCellContent;
}

inline void* chunk_at(WorldMap* map, int cx, int cy) {
    if (cx < 0 || cy < 0 || cx >= kGridDim || cy >= kGridDim) return nullptr;
    auto** grid = reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(map) + kGridOffset);
    return grid[cx * kGridDim + cy];
}

inline std::uint8_t* owner_of(WorldMap* map) {
    return reinterpret_cast<std::uint8_t*>(map) - kOwnerToWorldMap;
}

// __thiscall with stack arguments; the detour mirrors it using __fastcall,
// which puts `self` in ECX and leaves the integer arguments on the stack.
using GetCellFn = MapCell*(__thiscall*)(WorldMap*, int x, int y);

}  // namespace cw
