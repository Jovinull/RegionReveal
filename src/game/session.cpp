#include "session.hpp"

#include <windows.h>

#include <cstdint>

namespace cw {
namespace {

// A pointer that claims to be a game object must at least have its vftable
// inside the game's image.
bool in_game_image(const void* address) {
    static const std::uint8_t* const base = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
    static const std::size_t size = [] {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
        return static_cast<std::size_t>(nt->OptionalHeader.SizeOfImage);
    }();
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
// fixed point to blocks, then blocks to cells, truncating as compiled.
int position_to_cell(std::int64_t position) {
    return static_cast<int>(position / kPosUnitsPerBlock / kBlocksPerCell);
}

}  // namespace

bool readable(const void* address, std::size_t size) {
    if (!address) return false;
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(address, &info, sizeof(info)) != sizeof(info)) return false;
    if (info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;

    const auto* end = static_cast<const std::uint8_t*>(info.BaseAddress) + info.RegionSize;
    return static_cast<const std::uint8_t*>(address) + size <= end;
}

Cell local_player_cell(WorldMap* map) {
    if (!map) return {};

    std::uint8_t* player = nullptr;
    if (!read(owner_of(map) + kOwnerToLocalPlayer, &player)) return {};

    const void* vftable = nullptr;
    if (!read(player, &vftable) || !in_game_image(vftable)) return {};

    std::int64_t x = 0;
    std::int64_t y = 0;
    if (!read(player + kCreaturePosX, &x) || !read(player + kCreaturePosY, &y)) return {};

    const Cell cell{position_to_cell(x), position_to_cell(y)};
    return cell.valid() ? cell : Cell{};
}

std::string world_name(WorldMap* map) {
    if (!map) return {};

    std::uint8_t* world = nullptr;
    if (!read(bytes_of(map) + kWorldMapWorld, &world)) return {};

    const std::uint8_t* text = world + kWorldName;
    std::uint32_t size = 0;
    std::uint32_t capacity = 0;
    if (!read(text + kStdStringSize, &size) || !read(text + kStdStringCapacity, &capacity)) return {};
    if (size == 0 || size > 256 || size > capacity) return {};

    const char* chars = reinterpret_cast<const char*>(text);
    if (capacity >= kStdStringInlineCapacity && !read(text, &chars)) return {};
    if (!readable(chars, size)) return {};
    return std::string(chars, size);
}

}  // namespace cw
