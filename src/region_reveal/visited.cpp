#include "visited.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "../game/cube_world.hpp"
#include "log.hpp"
#include "paths.hpp"

namespace rr {
namespace {

constexpr char kMagic[4] = {'R', 'R', 'V', 'S'};

// Version 3 stores one cell per named area entered. Version 2 stored the
// 8 x 8-cell blocks the player entered; each converts to the cell at its
// middle, which lies inside the area the player was in, so that history
// carries over. Version 1 keyed on 64 x 64-cell storage chunks, too coarse to
// say where the player was, and is rejected rather than converted.
constexpr std::uint32_t kVersion = 3;
constexpr std::uint32_t kVersionBlocks = 2;
constexpr int kBlockCells = 8;
constexpr std::uint32_t kBlocksPerAxis = cw::kMapCells / kBlockCells;

constexpr std::uint32_t kMaxWorldName = 64;
constexpr std::uint32_t kMaxCells = 1u << 20;

#pragma pack(push, 1)
struct Header {
    char magic[4];
    std::uint32_t version;
    std::uint32_t dim;  // cells per axis; 8-cell blocks per axis in version 2
    std::uint32_t count;
    std::uint32_t worldLength;
};
#pragma pack(pop)

// The game already uses world names as file names, for Save/map_<name>.db.
// Anything that could leave the game folder or is not valid in a file name is
// refused rather than sanitised: path separators, a drive colon, wildcards,
// control characters and anything outside printable ASCII.
bool usable_world_name(const std::string& world) {
    if (world.empty() || world.size() > kMaxWorldName) return false;
    return std::all_of(world.begin(), world.end(), [](char c) {
        const auto code = static_cast<unsigned char>(c);
        return code >= 0x20 && code < 0x7F && std::strchr("\\/:*?\"<>|", c) == nullptr;
    });
}

std::wstring file_for(const std::string& world, const wchar_t* suffix) {
    return beside_game(L"RegionReveal_" + std::wstring(world.begin(), world.end()) + suffix);
}

}  // namespace

void VisitedAreas::open(const std::string& world) {
    if (world == world_) return;

    flush();
    world_.clear();
    cells_.clear();
    dirty_ = false;
    if (!usable_world_name(world)) return;

    world_ = world;
    const bool loaded = load();
    log_linef("visited: %u areas recorded in world '%s'%s", static_cast<unsigned>(cells_.size()), world_.c_str(),
              loaded ? "" : " (no usable file, starting empty)");
    flush();  // a converted file is rewritten in the current format at once
}

bool VisitedAreas::load() {
    const std::wstring path = file_for(world_, L".visited");
    if (path.empty()) return false;

    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"rb") != 0 || !file) return false;

    Header header{};
    std::string stored;
    std::vector<std::uint32_t> keys;
    bool ok = std::fread(&header, sizeof(header), 1, file) == 1 &&
              std::memcmp(header.magic, kMagic, sizeof(kMagic)) == 0;
    const bool current = ok && header.version == kVersion && header.dim == cw::kMapCells;
    const bool blocks = ok && header.version == kVersionBlocks && header.dim == kBlocksPerAxis;
    ok = (current || blocks) && header.count <= kMaxCells && header.worldLength > 0 &&
         header.worldLength <= kMaxWorldName;
    if (ok) {
        stored.resize(header.worldLength);
        ok = std::fread(&stored[0], 1, stored.size(), file) == stored.size() && stored == world_;
    }
    if (ok) {
        keys.resize(header.count);
        ok = keys.empty() || std::fread(keys.data(), sizeof(std::uint32_t), keys.size(), file) == keys.size();
    }
    // Trailing bytes or unsorted keys mean the file is not what this code
    // wrote; it counts as damaged rather than something to repair.
    ok = ok && std::fgetc(file) == EOF && std::is_sorted(keys.begin(), keys.end());
    std::fclose(file);
    if (!ok) return false;

    if (blocks) {
        for (std::uint32_t& key : keys) {
            if (static_cast<std::uint32_t>(key_x(key)) >= kBlocksPerAxis ||
                static_cast<std::uint32_t>(key_y(key)) >= kBlocksPerAxis) {
                return false;
            }
            key = cell_key(key_x(key) * kBlockCells + kBlockCells / 2, key_y(key) * kBlockCells + kBlockCells / 2);
        }
        std::sort(keys.begin(), keys.end());
        dirty_ = true;
        log_linef("visited: converted %u entries from version 2", static_cast<unsigned>(keys.size()));
    }
    cells_.swap(keys);
    return true;
}

bool VisitedAreas::add(int x, int y) {
    if (world_.empty()) return false;
    if (x < 0 || y < 0 || x >= cw::kMapCells || y >= cw::kMapCells) return false;
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

    const std::wstring path = file_for(world_, L".visited");
    const std::wstring temp = file_for(world_, L".visited.tmp");
    if (path.empty() || temp.empty()) return;

    FILE* file = nullptr;
    if (_wfopen_s(&file, temp.c_str(), L"wb") != 0 || !file) return;

    Header header{};
    std::memcpy(header.magic, kMagic, sizeof(kMagic));
    header.version = kVersion;
    header.dim = cw::kMapCells;
    header.count = static_cast<std::uint32_t>(cells_.size());
    header.worldLength = static_cast<std::uint32_t>(world_.size());

    bool written = std::fwrite(&header, sizeof(header), 1, file) == 1 &&
                   std::fwrite(world_.data(), 1, world_.size(), file) == world_.size() &&
                   (cells_.empty() ||
                    std::fwrite(cells_.data(), sizeof(std::uint32_t), cells_.size(), file) == cells_.size());
    written = std::fclose(file) == 0 && written;

    if (written && MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        dirty_ = false;
        return;
    }
    DeleteFileW(temp.c_str());
    log_linef("visited: could not write the file for world '%s'", world_.c_str());
}

}  // namespace rr
