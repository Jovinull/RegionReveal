#include "session.hpp"

#include <windows.h>

#include <cstring>

namespace cw {
namespace {

// The image the game was loaded from. A pointer that claims to be a game object
// must at least have a vftable inside it.
bool in_module(const void* address) {
    static const std::uint8_t* base = nullptr;
    static std::size_t size = 0;
    if (!base) {
        base = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
        if (!base) return false;
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
        size = nt->OptionalHeader.SizeOfImage;
    }
    const auto* at = static_cast<const std::uint8_t*>(address);
    return at >= base && at < base + size;
}

template <typename T>
bool read(const void* address, T* out) {
    if (!readable(address, sizeof(T))) return false;
    *out = *static_cast<const T*>(address);
    return true;
}

// The game's own conversion, from the code that feeds WorldMap::discover:
// divide the fixed-point position by 65536 to get blocks, then by 256 to get a
// map cell, truncating toward zero exactly as the compiler emitted it.
int position_to_cell(std::int64_t position) {
    const std::int64_t blocks = position / kPosFractionalDivisor;
    return static_cast<int>(blocks / kBlocksPerCell);
}

}  // namespace

bool readable(const void* address, std::size_t size) {
    if (!address) return false;
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(address, &info, sizeof(info)) != sizeof(info)) return false;
    if (info.State != MEM_COMMIT) return false;
    if (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;

    const auto* start = static_cast<const std::uint8_t*>(info.BaseAddress);
    const auto* end = start + info.RegionSize;
    const auto* wanted = static_cast<const std::uint8_t*>(address);
    return wanted + size <= end;
}

Cell local_player_cell(WorldMap* map) {
    if (!map) return {};

    std::uint8_t* player = nullptr;
    if (!read(owner_of(map) + kOwnerToLocalPlayer, &player)) return {};

    // A live Creature has its vftable in the image, so a pointer that fails
    // either test is not a player.
    const void* vftable = nullptr;
    if (!read(player, &vftable) || !in_module(vftable)) return {};

    std::int64_t x = 0;
    std::int64_t y = 0;
    if (!read(player + kPlayerPosX, &x) || !read(player + kPlayerPosY, &y)) return {};

    Cell cell{position_to_cell(x), position_to_cell(y)};
    return cell.valid() ? cell : Cell{};
}

// The HUD shows the current area as two attribute strings, and the game caches
// the pair in globals so it can notice when they change (0x494355, 0x49436E).
// Reading them is diagnostic only: whichever coordinate unit changes at the same
// moment as these strings is the unit the player calls a region.
//
// These are 2013-07-20 addresses at the preferred ImageBase. Cube.exe is built
// with DYNAMIC_BASE and does get relocated - 0xE60000 was observed - so they are
// rebased before use; read unrebased, they pointed at unrelated memory, which is
// why every probe logged the name globals as zeros.
constexpr std::uintptr_t kLandscapeName = 0x0076B104;
constexpr std::uintptr_t kLandscapeDetail = 0x0076B11C;
constexpr std::uintptr_t kPreferredImageBase = 0x00400000;

std::uintptr_t rebase(std::uintptr_t va) {
    return va - kPreferredImageBase + reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
}

std::string read_msvc_string(std::uintptr_t at) {
    auto* text = reinterpret_cast<std::uint8_t*>(at);
    std::uint32_t size = 0;
    std::uint32_t capacity = 0;
    if (!read(text + kStdStringSize, &size) || !read(text + kStdStringCapacity, &capacity)) {
        return {};
    }
    if (size == 0 || size > 128 || size > capacity) return {};

    // The attribute layer stores wide strings, so try that first and fall back.
    const bool wide = capacity >= kStdStringSsoCapacity ? false : true;
    const void* chars = text;
    if (capacity >= kStdStringSsoCapacity && !read(text, &chars)) return {};
    if (!readable(chars, size * 2)) {
        if (!readable(chars, size)) return {};
        return std::string(static_cast<const char*>(chars), size);
    }
    (void)wide;
    const auto* w = static_cast<const wchar_t*>(chars);
    std::string out;
    for (std::uint32_t i = 0; i < size && w[i]; ++i) {
        out.push_back(w[i] < 128 ? static_cast<char>(w[i]) : '?');
    }
    return out;
}

Probe probe(WorldMap* map) {
    Probe out;
    if (!map) return out;

    std::uint8_t* player = nullptr;
    if (!read(owner_of(map) + kOwnerToLocalPlayer, &player)) return out;
    const void* vftable = nullptr;
    if (!read(player, &vftable) || !in_module(vftable)) return out;

    std::int64_t x = 0;
    std::int64_t y = 0;
    if (!read(player + kPlayerPosX, &x) || !read(player + kPlayerPosY, &y)) return out;

    out.blockX = x / kPosFractionalDivisor;
    out.blockY = y / kPosFractionalDivisor;
    out.cellX = static_cast<int>(out.blockX / kBlocksPerCell);
    out.cellY = static_cast<int>(out.blockY / kBlocksPerCell);
    out.chunkX = out.cellX >> 6;
    out.chunkY = out.cellY >> 6;
    out.subX = out.cellX >> 3;
    out.subY = out.cellY >> 3;
    out.valid = true;

    // The marker pass indexes an 8x8 grid of 0x68-byte records that sits after
    // the cell array; cube::Region builds the same 64 records at its own +0x14018.
    void* chunk = chunk_at(map, out.chunkX, out.chunkY);
    if (!chunk) return out;
    const int index = ((out.subX & 7) * 8 + (out.subY & 7)) * kRecordStride;
    auto* record = static_cast<std::uint8_t*>(chunk) + kChunkRecords + index;
    if (!readable(record, sizeof(out.field))) return out;

    out.record = record;
    std::memcpy(out.field, record, sizeof(out.field));
    const auto* name = reinterpret_cast<const void*>(rebase(kLandscapeName));
    const auto* detail = reinterpret_cast<const void*>(rebase(kLandscapeDetail));
    out.landscape = read_msvc_string(rebase(kLandscapeName));
    out.detail = read_msvc_string(rebase(kLandscapeDetail));
    if (readable(name, sizeof(out.nameRaw))) {
        std::memcpy(out.nameRaw, name, sizeof(out.nameRaw));
    }
    if (readable(detail, sizeof(out.detailRaw))) {
        std::memcpy(out.detailRaw, detail, sizeof(out.detailRaw));
    }
    return out;
}

Landmark landmark_at(WorldMap* map, int regionX, int regionY) {
    Landmark out;
    if (!map) return out;
    if (regionX < 0 || regionY < 0 || regionX >= kRegionDim || regionY >= kRegionDim) return out;

    // A region is 8 cells; the chunk holding it is 64 cells wide.
    const int cellX = regionX * kRegionCells;
    const int cellY = regionY * kRegionCells;
    void* chunk = chunk_at(map, cellX >> 6, cellY >> 6);
    if (!chunk) return out;

    const int index = ((regionX & 7) * 8 + (regionY & 7)) * kRecordStride;
    auto* record = static_cast<std::uint8_t*>(chunk) + kChunkRecords + index;
    unsigned raw = 0;
    if (!read(record + kRecordLandmark, &raw)) {
        out.status = LandmarkLookup::Unreadable;
        return out;
    }
    out.status = LandmarkLookup::Ok;
    out.raw = raw;
    return out;
}

std::string world_name(WorldMap* map) {
    if (!map) return {};

    std::uint8_t* info = nullptr;
    if (!read(reinterpret_cast<std::uint8_t*>(map) + kWorldMapOwnerInfo, &info)) return {};

    std::uint8_t* text = info + kWorldInfoName;
    std::uint32_t size = 0;
    std::uint32_t capacity = 0;
    if (!read(text + kStdStringSize, &size) || !read(text + kStdStringCapacity, &capacity)) {
        return {};
    }
    if (size == 0 || size > 256 || size > capacity) return {};

    const char* chars = reinterpret_cast<const char*>(text);
    if (capacity >= kStdStringSsoCapacity) {
        if (!read(text, &chars)) return {};
    }
    if (!readable(chars, size)) return {};

    return std::string(chars, size);
}

}  // namespace cw
