#include "preview.hpp"

#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "../game/cube_world.hpp"
#include "../game/session.hpp"
#include "../game/world.hpp"
#include "areas.hpp"
#include "log.hpp"
#include "visited.hpp"

namespace rr {
namespace {

// Every second the map data worker frees every tile more than kMapTileKeep cells
// from the view centre, previews included (0x5FBED0). Building further out only
// feeds that.
constexpr int kMaxBuildRadius = cw::kMapTileKeep - 1;

// Every preview costs about 230 KB, most of it mesh: the game's mesh builder
// emits a face for every voxel side with nothing next to it, the bottom of every
// column included, exactly as it does for real tiles. 6 cells is 169 previews,
// around 40 MB; 9 is 361, around 85 MB.
constexpr int kDefaultBuildRadius = 6;

// Address space left for the process, below which previews stop being built and
// then are given back. The game's own allocations failed twice with private
// usage near 1.75 GB; what runs out is address space, not memory, so that is
// what is measured.
constexpr unsigned kPauseBelowMb = 400;
constexpr unsigned kReleaseBelowMb = 250;

// One slice per frame at most, and how long a slice may run. With the map open
// a preview takes two or three slices; with it closed only the bitmap is kept.
constexpr double kTickSpacingMs = 15.0;
constexpr double kOpenBudgetMs = 5.0;
constexpr double kClosedBudgetMs = 2.0;

constexpr DWORD kWindowRefreshMs = 2000;  // newly generated areas can resolve cells
constexpr int kWindowMove = 4;
constexpr DWORD kWorldCheckMs = 250;
constexpr DWORD kSettingsMs = 2000;
constexpr DWORD kMemoryMs = 500;
constexpr DWORD kReportMs = 10000;

// The map draws every frame while open, so a second without a draw means it was
// closed. Previews are held a few seconds longer, so closing it briefly to look
// around does not throw them away.
constexpr DWORD kMapOpenMs = 1000;
constexpr DWORD kHoldAfterCloseMs = 5000;

// Area borders are sampled where the game's tile generator samples them: every
// 4 voxels, 32 blocks, comparing each point with the next one on either axis.
constexpr int kDotSpacing = 4 * cw::kBlocksPerVoxel;
constexpr int kDotSamples = cw::kBlocksPerCell / kDotSpacing + 1;

std::uint8_t* bytes(cw::MapCell* cell) { return reinterpret_cast<std::uint8_t*>(cell); }

cw::TileImage*& tile_of(cw::MapCell* cell) {
    return *reinterpret_cast<cw::TileImage**>(bytes(cell) + cw::kCellTile);
}

// A cell the map would draw a placeholder for: loaded, no tile, none saved. A
// saved tile is the game's to load, so such a cell is left alone even while its
// tile is not in memory.
bool wants_preview(cw::MapCell* cell) {
    return cell && !tile_of(cell) && !(bytes(cell)[cw::kCellFlags] & cw::kSavedTileBit);
}

int chebyshev(int ax, int ay, int bx, int by) { return std::max(std::abs(ax - bx), std::abs(ay - by)); }

unsigned free_address_space_mb() {
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (!GlobalMemoryStatusEx(&status)) return ~0u;
    return static_cast<unsigned>(status.ullAvailVirtual >> 20);
}

unsigned private_mb() {
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                              sizeof(counters))) {
        return 0;
    }
    return static_cast<unsigned>(counters.PrivateUsage >> 20);
}

LONGLONG now_counter() {
    LARGE_INTEGER at;
    QueryPerformanceCounter(&at);
    return at.QuadPart;
}

}  // namespace

PreviewEngine::PreviewEngine(RevealedAreas& areas, RevealWindow& window) : areas_(areas), window_(window) {
    QueryPerformanceFrequency(&frequency_);
    constexpr std::size_t kCells = static_cast<std::size_t>(RevealWindow::kSpan) * RevealWindow::kSpan;
    member_.assign(kCells, false);
    next_.assign(kCells, false);
    settings_.radius = kDefaultBuildRadius;
    reportAt_ = GetTickCount();
}

bool PreviewEngine::in_window(int x, int y) const {
    const int dx = x - (winX_ - RevealWindow::kRadius);
    const int dy = y - (winY_ - RevealWindow::kRadius);
    return haveWindow_ && dx >= 0 && dy >= 0 && dx < RevealWindow::kSpan && dy < RevealWindow::kSpan &&
           member_[dx * RevealWindow::kSpan + dy];
}

void PreviewEngine::forget_world() {
    // A world change discards every storage chunk, and the previews attached to
    // them go with it.
    if (!live_.empty()) {
        log_linef("preview: world changed, %u previews released by the game", static_cast<unsigned>(live_.size()));
    }
    live_.clear();
    liveKeys_.clear();
    window_.clear();
    haveWindow_ = false;
    computing_ = false;
    building_ = false;
}

void PreviewEngine::step_window(cw::WorldMap* map, int cx, int cy, LONGLONG deadline) {
    constexpr int kSpan = RevealWindow::kSpan;
    constexpr int kRadius = RevealWindow::kRadius;
    const DWORD now = GetTickCount();

    if (!computing_) {
        const unsigned generation = areas_.generation();
        const bool needed = !haveWindow_ || generation != winGeneration_ ||
                            chebyshev(cx, cy, winX_, winY_) >= kWindowMove || now - winAt_ >= kWindowRefreshMs;
        if (!needed) return;
        std::string seedsWorld;
        seeds_ = areas_.snapshot(&seedsWorld);
        if (seedsWorld != world_) return;  // the session has not opened this world yet
        computing_ = true;
        nextX_ = cx;
        nextY_ = cy;
        nextGeneration_ = generation;
        nextRow_ = 0;
        nextRevealed_ = 0;
    }

    while (nextRow_ < kSpan && now_counter() < deadline) {
        const int x = nextX_ - kRadius + nextRow_;
        for (int dy = 0; dy < kSpan; ++dy) {
            const cw::Area area = cw::area_at_cell(map, x, nextY_ - kRadius + dy);
            const bool in = area.valid() && std::binary_search(seeds_.begin(), seeds_.end(), area.seed);
            next_[nextRow_ * kSpan + dy] = in;
            nextRevealed_ += in;
        }
        ++nextRow_;
    }
    if (nextRow_ < kSpan) return;

    window_.publish(nextX_, nextY_, next_);
    member_.swap(next_);
    if (nextGeneration_ != winGeneration_) {
        log_linef("preview: %u areas revealed, %u cells of them around (%d,%d)",
                  static_cast<unsigned>(seeds_.size()), nextRevealed_, nextX_, nextY_);
    }
    haveWindow_ = true;
    winX_ = nextX_;
    winY_ = nextY_;
    winGeneration_ = nextGeneration_;
    winAt_ = now;
    revealedCells_ = nextRevealed_;
    computing_ = false;
}

// Forgets previews the game has already freed or replaced, so the cell can be
// built again if the view comes back to it. The game frees tiles beyond its own
// radius and replaces a preview the moment it generates the real tile, through
// the image's destructor in both cases; the saved-tile bit rules out a recycled
// pointer, since the game sets it on every real tile.
void PreviewEngine::validate(cw::WorldMap* map) {
    if (live_.empty()) return;
    CRITICAL_SECTION* lock = cw::cell_lock(map);
    EnterCriticalSection(lock);
    for (std::size_t k = 0; k < live_.size();) {
        cw::MapCell* cell = cw::real_cell(map, live_[k].x, live_[k].y);
        const bool ours = cell && tile_of(cell) == live_[k].image && !(bytes(cell)[cw::kCellFlags] & cw::kSavedTileBit);
        if (ours) {
            ++k;
            continue;
        }
        liveKeys_.erase(cell_key(live_[k].x, live_[k].y));
        live_[k] = live_.back();
        live_.pop_back();
    }
    LeaveCriticalSection(lock);
}

// Takes back every preview further than keepRadius from the centre; a negative
// radius takes back all of them. Ownership is checked again under this lock: the
// game may have freed the chunk since validate() let go of it.
void PreviewEngine::release(cw::WorldMap* map, int cx, int cy, int keepRadius) {
    if (live_.empty()) return;
    CRITICAL_SECTION* lock = cw::cell_lock(map);
    EnterCriticalSection(lock);
    for (std::size_t k = 0; k < live_.size();) {
        const Preview p = live_[k];
        if (keepRadius >= 0 && chebyshev(p.x, p.y, cx, cy) <= keepRadius) {
            ++k;
            continue;
        }
        cw::MapCell* cell = cw::real_cell(map, p.x, p.y);
        if (cell && tile_of(cell) == p.image && !(bytes(cell)[cw::kCellFlags] & cw::kSavedTileBit)) {
            tile_of(cell) = nullptr;
            *reinterpret_cast<std::int32_t*>(bytes(cell) + cw::kCellTileFade) = 0;
            cw::clear_border_dots(cell);
            cw::destroy_tile_image(p.image);
        }
        liveKeys_.erase(cell_key(p.x, p.y));
        live_[k] = live_.back();
        live_.pop_back();
    }
    LeaveCriticalSection(lock);
}

void PreviewEngine::step_build(cw::WorldMap* map, int cx, int cy, int radius, LONGLONG deadline) {
    constexpr int kSpan = PreviewSamples::kSpan;
    CRITICAL_SECTION* lock = cw::cell_lock(map);

    if (!building_) {
        // Nearest the centre first, so the middle of the view fills in first.
        bool found = false;
        EnterCriticalSection(lock);
        for (int r = 0; r <= radius && !found; ++r) {
            for (int dx = -r; dx <= r && !found; ++dx) {
                for (int dy = -r; dy <= r && !found; ++dy) {
                    if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                    const int x = cx + dx;
                    const int y = cy + dy;
                    if (x < 0 || y < 0 || x >= cw::kMapDim || y >= cw::kMapDim) continue;
                    if (!in_window(x, y) || liveKeys_.count(cell_key(x, y))) continue;
                    if (!wants_preview(cw::real_cell(map, x, y))) continue;
                    found = true;
                    buildX_ = x;
                    buildY_ = y;
                }
            }
        }
        LeaveCriticalSection(lock);
        if (!found) return;
        building_ = true;
        buildRow_ = 0;
        samples_.cellX = buildX_;
        samples_.cellY = buildY_;
    }

    const LONGLONG started = now_counter();
    const int x0 = buildX_ * cw::kBlocksPerCell + cw::kBlocksPerVoxel / 2;
    const int y0 = buildY_ * cw::kBlocksPerCell + cw::kBlocksPerVoxel / 2;
    while (buildRow_ < kSpan && now_counter() < deadline) {
        for (int j = 0; j < kSpan; ++j) {
            samples_.heights[buildRow_][j] = cw::terrain_height(map, x0 + (buildRow_ - 1) * cw::kBlocksPerVoxel,
                                                                y0 + (j - 1) * cw::kBlocksPerVoxel);
        }
        ++buildRow_;
    }
    buildMs_ += (now_counter() - started) * 1000.0 / frequency_.QuadPart;
    if (buildRow_ < kSpan) return;

    // Sampled: everything else fits in one slice.
    const LONGLONG finishing = now_counter();
    building_ = false;
    const PreviewTile tile = synthesize_preview(samples_);

    std::vector<cw::BorderDot> dots;
    const void* area[kDotSamples][kDotSamples];
    const int bx = buildX_ * cw::kBlocksPerCell;
    const int by = buildY_ * cw::kBlocksPerCell;
    for (int a = 0; a < kDotSamples; ++a) {
        for (int b = 0; b < kDotSamples; ++b) {
            area[a][b] = cw::area_at_block(map, bx + a * kDotSpacing, by + b * kDotSpacing).id;
        }
    }
    for (int a = 0; a + 1 < kDotSamples; ++a) {
        for (int b = 0; b + 1 < kDotSamples; ++b) {
            if (area[a][b] == area[a + 1][b] && area[a][b] == area[a][b + 1]) continue;
            const float h = samples_.heights[a * 4 + 1][b * 4 + 1];
            const int z = std::max(static_cast<int>(std::floor(h)), kSeaLevel);
            dots.push_back({bx + a * kDotSpacing, by + b * kDotSpacing, z});
        }
    }

    cw::TileImage* image = cw::new_tile_image(map, PreviewTile::kDim, PreviewTile::kDim, tile.depth);
    if (!image) return;
    std::memcpy(cw::tile_voxels(image), tile.voxels.data(), tile.voxels.size());
    cw::build_tile_image(image);

    // The cell may have gained a tile, or left memory, while it was sampled.
    EnterCriticalSection(lock);
    cw::MapCell* cell = cw::real_cell(map, buildX_, buildY_);
    const bool ok = wants_preview(cell);
    if (ok) {
        *reinterpret_cast<std::int32_t*>(bytes(cell) + cw::kCellTileBase) = tile.base;
        tile_of(cell) = image;
        *reinterpret_cast<std::int32_t*>(bytes(cell) + cw::kCellTileFade) = cw::kTileFadeStart;
        cw::clear_border_dots(cell);
        for (const cw::BorderDot& dot : dots) cw::push_border_dot(cell, dot);
    }
    LeaveCriticalSection(lock);
    if (!ok) {
        cw::destroy_tile_image(image);
        return;
    }
    live_.push_back({buildX_, buildY_, image});
    liveKeys_.insert(cell_key(buildX_, buildY_));
    ++built_;
    buildMs_ += (now_counter() - finishing) * 1000.0 / frequency_.QuadPart;
}

void PreviewEngine::report(DWORD now) {
    if (now - reportAt_ < kReportMs) return;
    if (!built_ && live_.size() == reportedLive_) return;
    if (built_) {
        log_linef("preview: %u live, %u built in the last %lu s (%.1f ms of work each), %u revealed cells in view, "
                  "%u MB private, %u MB address space left",
                  static_cast<unsigned>(live_.size()), built_, (now - reportAt_ + 500) / 1000, buildMs_ / built_,
                  revealedCells_, private_mb(), freeMb_);
    } else {
        log_linef("preview: %u live, %u MB private, %u MB address space left", static_cast<unsigned>(live_.size()),
                  private_mb(), freeMb_);
    }
    built_ = 0;
    buildMs_ = 0;
    reportedLive_ = live_.size();
    reportAt_ = now;
}

void PreviewEngine::tick(cw::WorldMap* map) {
    const LONGLONG start = now_counter();
    if ((start - lastTick_) * 1000.0 / frequency_.QuadPart < kTickSpacingMs) return;
    lastTick_ = start;
    const DWORD now = GetTickCount();

    if (now - worldCheckedAt_ >= kWorldCheckMs) {
        worldCheckedAt_ = now;
        const std::string current = cw::world_name(map);
        if (current != world_) {
            forget_world();
            world_ = current;
        }
    }
    if (world_.empty()) return;

    if (now - settingsAt_ >= kSettingsMs) {
        settingsAt_ = now;
        const PreviewSettings next = read_preview_settings(kMaxBuildRadius, kDefaultBuildRadius);
        if (next.enabled != settings_.enabled || next.radius != settings_.radius) {
            log_linef("preview: %s, radius %d", next.enabled ? "enabled" : "disabled", next.radius);
        }
        settings_ = next;
    }
    if (now - memoryAt_ >= kMemoryMs) {
        memoryAt_ = now;
        freeMb_ = free_address_space_mb();
        const bool pause = freeMb_ < kPauseBelowMb;
        if (pause != paused_) {
            log_linef("preview: %s, %u MB of address space left", pause ? "paused" : "resumed", freeMb_);
            paused_ = pause;
        }
    }

    const cw::Cell centre = cw::view_centre(map);
    if (!centre.valid()) return;

    const DWORD sinceDraw = now - lastDraw_;
    const bool open = sinceDraw < kMapOpenMs;
    if (!open) building_ = false;  // its samples belong to a view that is gone
    const LONGLONG deadline =
        start + static_cast<LONGLONG>((open ? kOpenBudgetMs : kClosedBudgetMs) * frequency_.QuadPart / 1000.0);

    step_window(map, centre.x, centre.y, deadline);

    validate(map);
    const bool hold = settings_.enabled && freeMb_ >= kReleaseBelowMb && sinceDraw < kHoldAfterCloseMs;
    const int buildRadius = settings_.enabled && !paused_ && open ? settings_.radius : -1;
    release(map, centre.x, centre.y, hold ? std::max(buildRadius, settings_.radius) + 1 : -1);
    if (buildRadius >= 0 && now_counter() < deadline) step_build(map, centre.x, centre.y, buildRadius, deadline);

    report(now);
}

}  // namespace rr
