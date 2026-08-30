#include "visited.hpp"

#include <windows.h>

#include <cstdio>
#include <cstring>

#include "../game/cube_world.hpp"
#include "log.hpp"

namespace rr {
namespace {

constexpr char kMagic[4] = {'R', 'R', 'V', 'S'};
constexpr std::uint32_t kVersion = 1;
constexpr std::size_t kBitsBytes = (cw::kGridDim * cw::kGridDim) / 8;  // 128 KiB
constexpr std::uint32_t kMaxWorldName = 64;

#pragma pack(push, 1)
struct Header {
    char magic[4];
    std::uint32_t version;
    std::uint32_t gridDim;
    std::uint32_t bitsBytes;
    std::uint32_t worldLength;
};
#pragma pack(pop)

// World names come from the game, which already uses them as filenames for
// Save/map_<name>.db, but a path separator here would write outside the game
// folder, so anything but a plain name is rejected rather than sanitised.
bool usable_world_name(const std::string& world) {
    if (world.empty() || world.size() > kMaxWorldName) return false;
    for (const char c : world) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (!ok) return false;
    }
    return true;
}

std::wstring beside_game(const std::string& world, const wchar_t* suffix) {
    wchar_t path[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) return {};
    wchar_t* slash = wcsrchr(path, L'\\');
    if (!slash) return {};
    *(slash + 1) = L'\0';

    std::wstring out(path);
    out += L"RegionReveal_";
    out.append(world.begin(), world.end());
    out += suffix;
    return out;
}

}  // namespace

void VisitedRegions::open(const std::string& world) {
    if (world == world_) return;

    flush();
    world_.clear();
    bits_.clear();
    dirty_ = false;

    if (!usable_world_name(world)) return;

    world_ = world;
    bits_.assign(kBitsBytes, 0);
    if (load()) {
        log_linef("visited regions loaded for world '%s'", world_.c_str());
    } else {
        log_linef("no usable visited-region file for world '%s'; starting empty",
                  world_.c_str());
    }
}

bool VisitedRegions::load() {
    const std::wstring path = beside_game(world_, L".visited");
    if (path.empty()) return false;

    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"rb") != 0 || !file) return false;

    bool ok = false;
    Header header{};
    std::string stored;
    if (std::fread(&header, sizeof(header), 1, file) == 1 &&
        std::memcmp(header.magic, kMagic, sizeof(kMagic)) == 0 && header.version == kVersion &&
        header.gridDim == cw::kGridDim && header.bitsBytes == kBitsBytes &&
        header.worldLength > 0 && header.worldLength <= kMaxWorldName) {
        stored.resize(header.worldLength);
        if (std::fread(&stored[0], 1, header.worldLength, file) == header.worldLength &&
            stored == world_) {
            ok = std::fread(bits_.data(), 1, kBitsBytes, file) == kBitsBytes;
        }
    }
    std::fclose(file);

    // Every rejection path leaves the set empty rather than partly filled, so a
    // damaged file can only lose knowledge, never invent it.
    if (!ok) bits_.assign(kBitsBytes, 0);
    return ok;
}

bool VisitedRegions::contains(int x, int y) const {
    if (bits_.empty()) return false;
    if (x < 0 || y < 0 || x >= cw::kGridDim || y >= cw::kGridDim) return false;
    const std::size_t bit = static_cast<std::size_t>(x) * cw::kGridDim + y;
    return (bits_[bit >> 3] >> (bit & 7)) & 1;
}

bool VisitedRegions::add(int x, int y) {
    if (bits_.empty()) return false;
    if (x < 0 || y < 0 || x >= cw::kGridDim || y >= cw::kGridDim) return false;
    const std::size_t bit = static_cast<std::size_t>(x) * cw::kGridDim + y;
    const std::uint8_t mask = static_cast<std::uint8_t>(1u << (bit & 7));
    if (bits_[bit >> 3] & mask) return false;

    bits_[bit >> 3] |= mask;
    dirty_ = true;
    return true;
}

void VisitedRegions::flush() {
    if (!dirty_ || bits_.empty() || world_.empty()) return;

    const std::wstring path = beside_game(world_, L".visited");
    const std::wstring temp = beside_game(world_, L".visited.tmp");
    if (path.empty() || temp.empty()) return;

    FILE* file = nullptr;
    if (_wfopen_s(&file, temp.c_str(), L"wb") != 0 || !file) return;

    Header header{};
    std::memcpy(header.magic, kMagic, sizeof(kMagic));
    header.version = kVersion;
    header.gridDim = cw::kGridDim;
    header.bitsBytes = kBitsBytes;
    header.worldLength = static_cast<std::uint32_t>(world_.size());

    const bool written = std::fwrite(&header, sizeof(header), 1, file) == 1 &&
                         std::fwrite(world_.data(), 1, world_.size(), file) == world_.size() &&
                         std::fwrite(bits_.data(), 1, kBitsBytes, file) == kBitsBytes;
    std::fclose(file);

    if (!written) {
        DeleteFileW(temp.c_str());
        return;
    }
    if (MoveFileExW(temp.c_str(), path.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        dirty_ = false;
    } else {
        DeleteFileW(temp.c_str());
    }
}

}  // namespace rr
