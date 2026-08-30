#include "visited.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "../game/cube_world.hpp"
#include "log.hpp"

namespace rr {
namespace {

constexpr char kMagic[4] = {'R', 'R', 'V', 'S'};

// Version 2. Version 1 keyed on the 64x64-cell storage chunk, which is not what
// the game calls a region; those files describe the wrong thing and are rejected
// rather than converted.
constexpr std::uint32_t kVersion = 2;
constexpr std::uint32_t kMaxWorldName = 64;
constexpr std::uint32_t kMaxRegions = 1u << 20;  // far beyond any real playthrough

#pragma pack(push, 1)
struct Header {
    char magic[4];
    std::uint32_t version;
    std::uint32_t regionDim;
    std::uint32_t count;
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

std::uint32_t region_key(int x, int y) {
    return (static_cast<std::uint32_t>(x) << 16) | static_cast<std::uint32_t>(y);
}

void VisitedRegions::open(const std::string& world) {
    if (world == world_) return;

    flush();
    world_.clear();
    keys_.clear();
    dirty_ = false;

    if (!usable_world_name(world)) return;

    world_ = world;
    if (load()) {
        log_linef("visited: %u regions loaded for world '%s'",
                  static_cast<unsigned>(keys_.size()), world_.c_str());
    } else {
        log_linef("visited: no usable file for world '%s'; starting empty", world_.c_str());
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
        header.regionDim == cw::kRegionDim && header.count <= kMaxRegions &&
        header.worldLength > 0 && header.worldLength <= kMaxWorldName) {
        stored.resize(header.worldLength);
        if (std::fread(&stored[0], 1, header.worldLength, file) == header.worldLength &&
            stored == world_) {
            keys_.resize(header.count);
            ok = header.count == 0 ||
                 std::fread(keys_.data(), sizeof(std::uint32_t), header.count, file) ==
                     header.count;
            // A file claiming to be sorted but is not would break the lookup, so
            // it is treated as damaged rather than repaired.
            if (ok && !std::is_sorted(keys_.begin(), keys_.end())) ok = false;
        }
    }
    std::fclose(file);

    if (!ok) keys_.clear();
    return ok;
}

bool VisitedRegions::contains(int x, int y) const {
    if (x < 0 || y < 0 || x >= cw::kRegionDim || y >= cw::kRegionDim) return false;
    return std::binary_search(keys_.begin(), keys_.end(), region_key(x, y));
}

bool VisitedRegions::add(int x, int y) {
    if (world_.empty()) return false;
    if (x < 0 || y < 0 || x >= cw::kRegionDim || y >= cw::kRegionDim) return false;
    if (keys_.size() >= kMaxRegions) return false;

    const std::uint32_t key = region_key(x, y);
    const auto at = std::lower_bound(keys_.begin(), keys_.end(), key);
    if (at != keys_.end() && *at == key) return false;

    keys_.insert(at, key);
    dirty_ = true;
    return true;
}

void VisitedRegions::flush() {
    if (!dirty_ || world_.empty()) return;

    const std::wstring path = beside_game(world_, L".visited");
    const std::wstring temp = beside_game(world_, L".visited.tmp");
    if (path.empty() || temp.empty()) return;

    FILE* file = nullptr;
    if (_wfopen_s(&file, temp.c_str(), L"wb") != 0 || !file) return;

    Header header{};
    std::memcpy(header.magic, kMagic, sizeof(kMagic));
    header.version = kVersion;
    header.regionDim = cw::kRegionDim;
    header.count = static_cast<std::uint32_t>(keys_.size());
    header.worldLength = static_cast<std::uint32_t>(world_.size());

    const bool written =
        std::fwrite(&header, sizeof(header), 1, file) == 1 &&
        std::fwrite(world_.data(), 1, world_.size(), file) == world_.size() &&
        (keys_.empty() ||
         std::fwrite(keys_.data(), sizeof(std::uint32_t), keys_.size(), file) == keys_.size());
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
