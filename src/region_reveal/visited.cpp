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

// Version 3 stores visited cells, one per named area entered. Version 2 stored
// the centres of 8 x 8-cell gameplay regions; each converts to the cell at its
// middle, which lies inside the area the player was in, so no history is lost.
// Version 1 keyed on 64 x 64-cell storage chunks, which described the wrong
// thing, and is still rejected rather than converted.
constexpr std::uint32_t kVersion = 3;
constexpr std::uint32_t kVersionRegions = 2;
constexpr std::uint32_t kMaxWorldName = 64;
constexpr std::uint32_t kMaxCells = 1u << 20;

#pragma pack(push, 1)
struct Header {
    char magic[4];
    std::uint32_t version;
    std::uint32_t dim;  // cells per axis for v3, regions per axis for v2
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

std::uint32_t cell_key(int x, int y) {
    return (static_cast<std::uint32_t>(x) << 16) | static_cast<std::uint32_t>(y);
}

void VisitedAreas::open(const std::string& world) {
    if (world == world_) return;

    flush();
    world_.clear();
    cells_.clear();
    dirty_ = false;

    if (!usable_world_name(world)) return;

    world_ = world;
    const bool loaded = load();
    log_linef("visited: %u areas recorded, world '%s'%s", static_cast<unsigned>(cells_.size()),
              world_.c_str(), loaded ? "" : " (no usable file; starting empty)");
}

bool VisitedAreas::load() {
    const std::wstring path = beside_game(world_, L".visited");
    if (path.empty()) return false;

    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"rb") != 0 || !file) return false;

    bool ok = false;
    Header header{};
    std::string stored;
    std::vector<std::uint32_t> keys;
    const bool v3 = std::fread(&header, sizeof(header), 1, file) == 1 &&
                    std::memcmp(header.magic, kMagic, sizeof(kMagic)) == 0 &&
                    header.version == kVersion && header.dim == cw::kMapDim;
    const bool v2 = !v3 && std::memcmp(header.magic, kMagic, sizeof(kMagic)) == 0 &&
                    header.version == kVersionRegions && header.dim == cw::kRegionDim;
    if ((v3 || v2) && header.count <= kMaxCells && header.worldLength > 0 &&
        header.worldLength <= kMaxWorldName) {
        stored.resize(header.worldLength);
        if (std::fread(&stored[0], 1, header.worldLength, file) == header.worldLength &&
            stored == world_) {
            keys.resize(header.count);
            ok = header.count == 0 ||
                 std::fread(keys.data(), sizeof(std::uint32_t), header.count, file) == header.count;
            // Unsorted content would break the lookup, so it counts as damaged
            // rather than something to repair.
            if (ok && !std::is_sorted(keys.begin(), keys.end())) ok = false;
        }
    }
    std::fclose(file);
    if (!ok) return false;

    if (v2) {
        // A v2 key is a region; the cell in its middle is where the player was.
        for (std::uint32_t& key : keys) {
            key = cell_key(key_x(key) * cw::kRegionCells + cw::kRegionCells / 2,
                           key_y(key) * cw::kRegionCells + cw::kRegionCells / 2);
        }
        std::sort(keys.begin(), keys.end());
        dirty_ = true;  // rewrite as v3 at the next flush
        log_linef("visited: converted %u version 2 regions", static_cast<unsigned>(keys.size()));
    }
    cells_.swap(keys);
    return true;
}

bool VisitedAreas::add(int x, int y) {
    if (world_.empty()) return false;
    if (x < 0 || y < 0 || x >= cw::kMapDim || y >= cw::kMapDim) return false;
    if (cells_.size() >= kMaxCells) return false;

    const std::uint32_t key = cell_key(x, y);
    const auto at = std::lower_bound(cells_.begin(), cells_.end(), key);
    if (at != cells_.end() && *at == key) return false;

    cells_.insert(at, key);
    dirty_ = true;
    return true;
}

void VisitedAreas::flush() {
    if (!dirty_ || world_.empty()) return;

    const std::wstring path = beside_game(world_, L".visited");
    const std::wstring temp = beside_game(world_, L".visited.tmp");
    if (path.empty() || temp.empty()) return;

    FILE* file = nullptr;
    if (_wfopen_s(&file, temp.c_str(), L"wb") != 0 || !file) return;

    Header header{};
    std::memcpy(header.magic, kMagic, sizeof(kMagic));
    header.version = kVersion;
    header.dim = cw::kMapDim;
    header.count = static_cast<std::uint32_t>(cells_.size());
    header.worldLength = static_cast<std::uint32_t>(world_.size());

    const bool written =
        std::fwrite(&header, sizeof(header), 1, file) == 1 &&
        std::fwrite(world_.data(), 1, world_.size(), file) == world_.size() &&
        (cells_.empty() ||
         std::fwrite(cells_.data(), sizeof(std::uint32_t), cells_.size(), file) == cells_.size());
    std::fclose(file);

    if (!written) {
        DeleteFileW(temp.c_str());
        return;
    }
    if (MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        dirty_ = false;
    } else {
        DeleteFileW(temp.c_str());
    }
}

}  // namespace rr
