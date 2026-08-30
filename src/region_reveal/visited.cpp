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

// Version 2 stores visited centres. Version 1 keyed on the 64x64-cell storage
// chunk, which is not what the game calls a region, so those files describe the
// wrong thing and are rejected rather than converted. The survey radius is not
// part of the format: coverage is derived, so changing the radius does not
// invalidate a file.
constexpr std::uint32_t kVersion = 2;
constexpr std::uint32_t kMaxWorldName = 64;
constexpr std::uint32_t kMaxCentres = 1u << 20;

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

std::size_t round_up_pow2(std::size_t n) {
    std::size_t size = 16;
    while (size < n) size <<= 1;
    return size;
}

}  // namespace

std::uint32_t region_key(int x, int y) {
    return (static_cast<std::uint32_t>(x) << 16) | static_cast<std::uint32_t>(y);
}

void RegionSet::reset(std::size_t expected) {
    // Kept at most half full so linear probing stays short.
    slots_.assign(round_up_pow2(expected * 2 + 16), kEmpty);
    mask_ = slots_.size() - 1;
    count_ = 0;
}

void RegionSet::grow() {
    std::vector<std::uint32_t> old;
    old.swap(slots_);
    slots_.assign(old.empty() ? 32 : old.size() * 2, kEmpty);
    mask_ = slots_.size() - 1;
    count_ = 0;
    for (const std::uint32_t key : old) {
        if (key != kEmpty) insert(key);
    }
}

void RegionSet::insert(std::uint32_t key) {
    if (key == kEmpty) return;
    // Kept under half full: probing stays short, and an insert can never be
    // dropped because the table happened to be sized for a smaller set.
    if (slots_.empty() || (count_ + 1) * 2 >= slots_.size()) grow();

    std::size_t at = key & mask_;
    for (std::size_t i = 0; i <= mask_; ++i) {
        if (slots_[at] == key) return;
        if (slots_[at] == kEmpty) {
            slots_[at] = key;
            ++count_;
            return;
        }
        at = (at + 1) & mask_;
    }
}

bool RegionSet::contains(std::uint32_t key) const {
    if (slots_.empty()) return false;
    std::size_t at = key & mask_;
    for (std::size_t i = 0; i <= mask_; ++i) {
        if (slots_[at] == key) return true;
        if (slots_[at] == kEmpty) return false;
        at = (at + 1) & mask_;
    }
    return false;
}

void VisitedRegions::open(const std::string& world) {
    if (world == world_) return;

    flush();
    world_.clear();
    centres_.clear();
    revealed_.reset(0);
    dirty_ = false;

    if (!usable_world_name(world)) return;

    world_ = world;
    const bool loaded = load();
    rebuild();
    log_linef("visited: %u centres, %u regions covered, world '%s'%s",
              static_cast<unsigned>(centres_.size()), static_cast<unsigned>(revealed_.size()),
              world_.c_str(), loaded ? "" : " (no usable file; starting empty)");
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
        header.regionDim == cw::kRegionDim && header.count <= kMaxCentres &&
        header.worldLength > 0 && header.worldLength <= kMaxWorldName) {
        stored.resize(header.worldLength);
        if (std::fread(&stored[0], 1, header.worldLength, file) == header.worldLength &&
            stored == world_) {
            centres_.resize(header.count);
            ok = header.count == 0 ||
                 std::fread(centres_.data(), sizeof(std::uint32_t), header.count, file) ==
                     header.count;
            // Unsorted content would break the lookup, so it counts as damaged
            // rather than something to repair.
            if (ok && !std::is_sorted(centres_.begin(), centres_.end())) ok = false;
        }
    }
    std::fclose(file);

    if (!ok) centres_.clear();
    return ok;
}

void VisitedRegions::survey(int x, int y) {
    for (int dx = -kSurveyRadius; dx <= kSurveyRadius; ++dx) {
        for (int dy = -kSurveyRadius; dy <= kSurveyRadius; ++dy) {
            const int rx = x + dx;
            const int ry = y + dy;
            // Clamped rather than wrapped: a region near an edge surveys a
            // smaller area instead of reaching around the world.
            if (rx < 0 || ry < 0 || rx >= cw::kRegionDim || ry >= cw::kRegionDim) continue;
            revealed_.insert(region_key(rx, ry));
        }
    }
}

void VisitedRegions::rebuild() {
    const int span = kSurveyRadius * 2 + 1;
    revealed_.reset(centres_.size() * static_cast<std::size_t>(span) * span);
    for (const std::uint32_t key : centres_) {
        survey(static_cast<int>(key >> 16), static_cast<int>(key & 0xFFFF));
    }
}

bool VisitedRegions::revealed(int x, int y) const {
    if (x < 0 || y < 0 || x >= cw::kRegionDim || y >= cw::kRegionDim) return false;
    return revealed_.contains(region_key(x, y));
}

bool VisitedRegions::visit(int x, int y) {
    if (world_.empty()) return false;
    if (x < 0 || y < 0 || x >= cw::kRegionDim || y >= cw::kRegionDim) return false;
    if (centres_.size() >= kMaxCentres) return false;

    const std::uint32_t key = region_key(x, y);
    const auto at = std::lower_bound(centres_.begin(), centres_.end(), key);
    if (at != centres_.end() && *at == key) return false;

    centres_.insert(at, key);
    survey(x, y);
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
    header.count = static_cast<std::uint32_t>(centres_.size());
    header.worldLength = static_cast<std::uint32_t>(world_.size());

    const bool written =
        std::fwrite(&header, sizeof(header), 1, file) == 1 &&
        std::fwrite(world_.data(), 1, world_.size(), file) == world_.size() &&
        (centres_.empty() || std::fwrite(centres_.data(), sizeof(std::uint32_t), centres_.size(),
                                         file) == centres_.size());
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
