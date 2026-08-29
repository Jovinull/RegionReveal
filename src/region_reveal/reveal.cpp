#include "reveal.hpp"

#include <windows.h>
#include <intrin.h>

#include <atomic>
#include <cstring>

#include "../game/cube_world.hpp"
#include "../game/signatures.hpp"
#include "../hooks.hpp"
#include "log.hpp"

namespace rr {
namespace {

InlineHook g_get_cell;
InlineHook g_discover;

// Chunk the player was last seen discovering; -1 until the game reports one.
std::atomic<int> g_region_x{-1};
std::atomic<int> g_region_y{-1};

// Bounds of cube::MapOverlayWidget's draw method. Cells are only reported as
// revealed to callers inside this range, so gameplay code that asks the same
// question keeps seeing the unmodified map.
std::uint8_t* g_draw_begin = nullptr;
std::uint8_t* g_draw_end = nullptr;

bool from_map_renderer(const void* return_address) {
    const auto* at = static_cast<const std::uint8_t*>(return_address);
    return at >= g_draw_begin && at < g_draw_end;
}

// The renderer reads a cell and uses it immediately, but it holds a couple of
// them alive at once, so hand out copies from a small per-thread ring instead
// of a single scratch cell.
cw::MapCell* shadow_of(cw::MapCell* cell) {
    constexpr int kSlots = 16;
    thread_local std::uint8_t ring[kSlots][cw::kCellStride];
    thread_local int next = 0;

    std::uint8_t* slot = ring[next];
    next = (next + 1) % kSlots;
    std::memcpy(slot, cell, cw::kCellStride);
    slot[cw::kCellFlags] |= cw::kRevealedBit;
    return reinterpret_cast<cw::MapCell*>(slot);
}

cw::MapCell* __fastcall get_cell_detour(cw::WorldMap* self, void*, int x, int y) {
    const void* caller = _ReturnAddress();
    cw::MapCell* cell = g_get_cell.original<cw::GetCellFn>()(self, x, y);
    if (!cell || !from_map_renderer(caller)) return cell;

    if (cw::chunk_of(x) != g_region_x.load(std::memory_order_relaxed) ||
        cw::chunk_of(y) != g_region_y.load(std::memory_order_relaxed)) {
        return cell;
    }
    if (*cw::cell_kind(cell) == 0) return cell;
    if (*cw::cell_flags(cell) & cw::kRevealedBit) return cell;

    return shadow_of(cell);
}

void __fastcall discover_detour(cw::WorldMap* self, void*, int x, int y) {
    g_region_x.store(cw::chunk_of(x), std::memory_order_relaxed);
    g_region_y.store(cw::chunk_of(y), std::memory_order_relaxed);
    g_discover.original<cw::DiscoverFn>()(self, x, y);
}

// Proves the trampoline executes: getCell rejects a negative coordinate before
// it ever touches `this`, so calling it through the trampoline with a null
// instance is side-effect free and must return null. A malformed trampoline
// faults here instead of somewhere unattributable mid-game.
bool trampoline_works() {
    __try {
        return g_get_cell.original<cw::GetCellFn>()(nullptr, -1, -1) == nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// MSVC pads between functions with int3; the first long run after the entry
// marks the end of the draw method.
std::uint8_t* end_of_function(std::uint8_t* begin, std::uint8_t* limit) {
    for (std::uint8_t* at = begin + 0x100; at + 3 < limit; ++at) {
        if (at[0] == 0xCC && at[1] == 0xCC && at[2] == 0xCC) return at;
    }
    return nullptr;
}

}  // namespace

bool initialize() {
    cw::ModuleRange text{};
    if (!cw::module_text(&text)) {
        log_line("could not locate the .text section of Cube.exe");
        return false;
    }

    std::uint8_t* get_cell = cw::find_unique(text, cw::kSigWorldMapGetCell);
    std::uint8_t* discover = cw::find_unique(text, cw::kSigWorldMapDiscover);
    std::uint8_t* draw = cw::find_unique(text, cw::kSigMapOverlayDraw);
    if (!get_cell || !discover || !draw) {
        log_line("unsupported Cube World Alpha build - no hooks installed");
        return false;
    }

    g_draw_begin = draw;
    g_draw_end = end_of_function(draw, text.text_end);
    if (!g_draw_end) {
        log_line("could not bound the map draw function - no hooks installed");
        return false;
    }

    if (!g_get_cell.install(get_cell, &get_cell_detour)) {
        log_line("failed to hook WorldMap::getCell");
        return false;
    }
    if (!trampoline_works()) {
        g_get_cell.remove();
        log_line("getCell trampoline did not behave - hooks backed out");
        return false;
    }
    if (!g_discover.install(discover, &discover_detour)) {
        g_get_cell.remove();
        log_line("failed to hook WorldMap::discover");
        return false;
    }

    log_line("supported build detected; RegionReveal active");
    return true;
}

void shutdown() {
    g_discover.remove();
    g_get_cell.remove();
}

}  // namespace rr
