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
inline constexpr int kRecordLandmark = 0x18;  // the type the marker pass draws

// A gameplay region is the 8x8 block of cells that one 0x68 record covers:
// 8 * 256 = 2048 blocks per axis. The record carries the marker type the map's
// marker pass draws, and runtime probing showed the record changes exactly when
// this unit changes and not when the cell alone does.
inline constexpr int kRegionCells = 8;
inline constexpr int kRegionDim = kMapDim / kRegionCells;  // 8192 regions per axis

// Offsets inside a map cell. The cell is cube::ZoneTile: its constructor
// writes that class's RTTI vftable.
//
// +0x10..0x1F is the cell's point of interest, copied from the world generator:
// a type byte the label pass tests and draws (1 = city, drawn white) and a level
// at +0x18 it colours against the player's. It is not terrain - the terrain is
// the tile image at +0x08, drawn by WorldMap::render whether or not the reveal
// bit is set; that bit only tints the placeholder drawn where no tile exists.
inline constexpr int kCellTileBase = 0x04;    // tile's lowest voxel layer, in 8-block units
inline constexpr int kCellTile = 0x08;        // tile image, or null
inline constexpr int kCellContent = 0x10;     // point-of-interest type, 0 = none
inline constexpr int kCellBorderDots = 0x20;  // std::list of {x, y, z} area-border dots
inline constexpr int kCellTileFade = 0x2C;    // fade-in countdown, 250 when a tile appears
inline constexpr int kCellFlags = 0x30;       // bit 0 = revealed on the world map
inline constexpr std::uint8_t kRevealedBit = 0x01;
inline constexpr std::uint8_t kSavedTileBit = 0x02;  // a tile record exists in the save
inline constexpr int kTileFadeStart = 250;

// The tile image the map draws for a cell: a 32 x 32 x depth grid of voxels,
// each the average colour of an 8 x 8 x 8 block cube. RGB, three bytes a voxel
// at ((z * h + y) * w + x) * 3, with black meaning empty. The map scales voxels
// by a fixed 8 blocks, so a cell always needs the full 32 x 32.
inline constexpr int kTileImageSize = 0x60;
inline constexpr int kTileImageVoxels = 0x30;
inline constexpr int kTileDim = 32;
inline constexpr int kBlocksPerVoxel = 8;

// Offsets inside cube::WorldMap beyond the cell grid.
inline constexpr int kWorldMapRenderer = 0xA4;  // ctor arg 1, handed to every tile image
inline constexpr int kWorldMapWorld = 0xAC;     // cube::World*, ctor arg 2
inline constexpr int kWorldMapCellLock = 0x8000D8;

// WorldMap::render draws cells within kMapTileRadius of the view centre - the
// map overlay passes it as a literal, push 0x10 - but the map data worker frees
// every tile further than kMapTileKeep from that centre once a second
// (0x5FBED0, cmp eax, 0xA). So terrain shows within 10 cells, placeholders from
// there to 16, and coarse per-chunk landscape beyond.
inline constexpr int kMapTileRadius = 16;
inline constexpr int kMapTileKeep = 10;

// A named area - "Lands of Asmi", "Damarok Ocean" - as cube::World's area lookup
// returns it: the nearest of one centre per storage chunk, after warping the
// position with noise. The map's dotted lines are the borders between areas.
inline constexpr int kAreaSeed = 0x14;  // name seed; distinct per area and stable
inline constexpr int kAreaKind = 0x18;  // negative for ocean, named "... Ocean"

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

// Where the map is looking: a cell the game updates every frame plus the
// player's pan in blocks. The map data worker reads the same pair the same way,
// pan / 256 added to the cell, to decide which tiles to load.
inline constexpr int kOwnerViewCell = 0x2BC;      // int x, int y
inline constexpr int kOwnerViewPan = 0x1000E4C;   // float x, float y, in blocks

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

inline int region_of(int cell) { return cell >> 3; }

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
