#include "session.hpp"

#include <windows.h>

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

Region local_player_region(WorldMap* map) {
    if (!map) return {};

    std::uint8_t* player = nullptr;
    if (!read(owner_of(map) + kOwnerToLocalPlayer, &player)) return {};

    // Not populated on the title screen, and a live Creature has its vftable in
    // the image, so a pointer that fails either test is not a player.
    const void* vftable = nullptr;
    if (!read(player, &vftable) || !in_module(vftable)) return {};

    std::int64_t x = 0;
    std::int64_t y = 0;
    if (!read(player + kPlayerPosX, &x) || !read(player + kPlayerPosY, &y)) return {};

    Region region{region_of(position_to_cell(x)), region_of(position_to_cell(y))};
    return region.valid() ? region : Region{};
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
