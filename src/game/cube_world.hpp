// Layout of the Cube World Alpha structures RegionReveal touches.
//
// Every constant here holds for both supported builds; only function addresses
// differ between them, which is why those are found by signature at runtime.
// docs/REVERSE_ENGINEERING.md has the evidence for each one.

#pragma once

#include <cstdint>

namespace cw {

struct WorldMap;  // cube::WorldMap
struct MapCell;   // cube::ZoneTile, one cell of the world map
struct World;     // cube::World

// cube::WorldMap keeps a sparse 1024 x 1024 grid of storage chunks at +0xB0.
// A chunk holds 64 x 64 map cells of 0x34 bytes, and a cell spans 256 x 256
// blocks, so the map is 65536 cells per axis.
inline constexpr int kGridChunks = 1024;
inline constexpr int kChunkCells = 64;
inline constexpr int kMapCells = kGridChunks * kChunkCells;
inline constexpr int kCellSize = 0x34;
inline constexpr int kBlocksPerCell = 256;
inline constexpr int kBlocksPerChunk = kChunkCells * kBlocksPerCell;

// ZoneTile+0x30: bit 0 set means the cell is revealed on the world map. Both
// label passes of the map overlay draw a cell's labels only when it is set.
inline constexpr int kCellFlags = 0x30;
inline constexpr std::uint8_t kRevealedBit = 0x01;

// WorldMap+0xAC points at the cube::World it maps. The world's name, which the
// game turns into "Save/map_<name>.db", is a std::string at World+0x94.
inline constexpr int kWorldMapWorld = 0xAC;
inline constexpr int kWorldName = 0x94;

// cube::World's named areas - "Lands of Asmi", "Damarok Ocean" - one centre per
// storage chunk, kept as a 1024 x 1024 table of pointers indexed [x * 1024 + y]
// and null until the world generator has produced that chunk's centre. The
// area lookup's signature embeds this offset, so it is checked in both builds.
inline constexpr int kWorldAreaCentres = 0x4000BC;

// ZoneTile+0x10: the cell's point of interest, 1 for a city district.
inline constexpr int kCellPoi = 0x10;
inline constexpr std::uint8_t kPoiCityDistrict = 1;

// A place record: one per 8 x 8 block of cells, after the cells of a storage
// chunk. The landmark pass draws its name; the same record carries the boss
// mission of that place, if it has one. Layout as cuwo's MissionData.
inline constexpr int kPlaceOriginX = 0x00;   // int64, fixed point, 16 bits per block
inline constexpr int kPlaceOriginY = 0x08;
inline constexpr int kPlaceMission = 0x34;   // non-zero when the place has a mission
inline constexpr int kPlaceMissionState = 0x41;
inline constexpr std::uint8_t kMissionDone = 2;  // the game kills the boss for good at 2
inline constexpr int kPlaceBlockCells = 8;          // the block of cells a record belongs to

// The WorldMap is constructed in place inside the game controller, which also
// holds the local player.
inline constexpr int kOwnerToWorldMap = 0x800D44;
inline constexpr int kOwnerToLocalPlayer = 0x8006D0;  // cube::Creature*

// The map's zoom. The game draws points of interest only above kPoiZoom.
inline constexpr int kOwnerMapZoom = 0x1C4;  // float, 1.0 at the default zoom
inline constexpr float kPoiZoom = 2.0f;

// cube::Creature position: 64-bit fixed point, 16 fractional bits per block.
inline constexpr int kCreaturePosX = 0x10;
inline constexpr int kCreaturePosY = 0x18;
inline constexpr std::int64_t kPosUnitsPerBlock = 65536;

// MSVC 2012 std::string: an inline buffer or a heap pointer at +0, the size at
// +0x10 and the capacity at +0x14; the buffer is inline while capacity < 16.
inline constexpr int kStdStringSize = 0x10;
inline constexpr int kStdStringCapacity = 0x14;
inline constexpr std::uint32_t kStdStringInlineCapacity = 16;

inline std::uint8_t* bytes_of(void* object) { return static_cast<std::uint8_t*>(object); }

inline std::uint8_t* owner_of(WorldMap* map) { return bytes_of(map) - kOwnerToWorldMap; }

inline bool cell_revealed(const MapCell* cell) {
    return (reinterpret_cast<const std::uint8_t*>(cell)[kCellFlags] & kRevealedBit) != 0;
}

// cube::WorldMap::getCell(x, y): __thiscall, the cell or null when its storage
// chunk is not loaded.
using GetCellFn = MapCell*(__thiscall*)(WorldMap*, int x, int y);

}  // namespace cw
