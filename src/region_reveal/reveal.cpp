#include "reveal.hpp"

#include <windows.h>
#include <intrin.h>

#include <climits>
#include <cstring>
#include <string>

#include "../game/cube_world.hpp"
#include "../game/session.hpp"
#include "../game/signatures.hpp"
#include "../hooks.hpp"
#include "log.hpp"
#include "visited.hpp"

namespace rr {
namespace {

InlineHook g_get_cell;
VisitedRegions g_visited;
CRITICAL_SECTION g_lock;
bool g_lock_ready = false;

cw::Region g_region;
DWORD g_checked = 0;

// Bounds of cube::MapOverlayWidget's draw method. Cells are only reported as
// revealed to callers inside this range, so gameplay code asking the same
// question keeps seeing the unmodified map.
std::uint8_t* g_draw_begin = nullptr;
std::uint8_t* g_draw_end = nullptr;

// A region spans 64 cells of 256 blocks, so the player cannot cross one in a
// quarter second. Re-reading the pointer chain per getCell call would cost
// millions of dereferences per map frame for an answer that cannot have changed.
constexpr DWORD kRecheckMs = 250;

#ifdef REGIONREVEAL_DIAGNOSTICS
// Counts what the detour decided, so one play session can settle the A/B and the
// cost question at once instead of needing a run each.
struct Counters {
    unsigned calls;
    unsigned rendering;
    unsigned outside_visited;
    unsigned already_lit;
    unsigned no_content;
    unsigned revealed;
    unsigned long long cycles;
};
Counters g_diag{};
DWORD g_reported = 0;

void report() {
    const DWORD now = GetTickCount();
    if (now - g_reported < 5000) return;
    g_reported = now;
    const unsigned long long per_call = g_diag.calls ? g_diag.cycles / g_diag.calls : 0;
    log_linef("map draw: calls=%u rendering=%u | outsideVisited=%u alreadyLit=%u noContent=%u REVEALED=%u",
              g_diag.calls, g_diag.rendering, g_diag.outside_visited, g_diag.already_lit,
              g_diag.no_content, g_diag.revealed);
    log_linef("  cost=%llu cycles over %u calls = %llu/call | region=(%d,%d) world='%s'",
              g_diag.cycles, g_diag.calls, per_call, g_region.x, g_region.y,
              g_visited.world().c_str());
}
#define DIAG(expr) (expr)
#else
#define DIAG(expr) ((void)0)
#endif

bool from_map_renderer(const void* return_address) {
    const auto* at = static_cast<const std::uint8_t*>(return_address);
    return at >= g_draw_begin && at < g_draw_end;
}

// Everything here runs on the render thread only, which is what makes the
// unlocked reads in the detour safe; the lock exists for the shutdown flush.
#ifdef REGIONREVEAL_DIAGNOSTICS
// Logs a line whenever any candidate granularity changes, so walking until the
// on-screen region name changes shows which unit moved with it.
void probe_granularity(cw::WorldMap* map) {
    static int lastSubX = INT_MIN, lastSubY = INT_MIN, lastChunkX = INT_MIN, lastChunkY = INT_MIN;
    static unsigned lastField[8] = {};
    static DWORD lastPeriodic = 0;
    static std::string lastName;

    const cw::Probe p = cw::probe(map);
    if (!p.valid) return;

    const bool moved = p.subX != lastSubX || p.subY != lastSubY ||
                       p.chunkX != lastChunkX || p.chunkY != lastChunkY;
    const bool changed = std::memcmp(lastField, p.field, sizeof(lastField)) != 0;
    const std::string name = p.landscape + "/" + p.detail;
    const bool renamed = name != lastName;
    const DWORD now = GetTickCount();
    const bool periodic = now - lastPeriodic >= 10000;
    if (!moved && !changed && !renamed && !periodic) return;

    lastSubX = p.subX; lastSubY = p.subY;
    lastChunkX = p.chunkX; lastChunkY = p.chunkY;
    std::memcpy(lastField, p.field, sizeof(lastField));
    lastPeriodic = now;
    lastName = name;

    log_linef("probe block=(%lld,%lld) cell=(%d,%d) chunk=(%d,%d) sub8=(%d,%d) name='%s'%s%s%s",
              p.blockX, p.blockY, p.cellX, p.cellY, p.chunkX, p.chunkY, p.subX, p.subY,
              name.c_str(), moved ? "  <-- UNIT CHANGED" : "",
              changed ? "  <-- RECORD CHANGED" : "", renamed ? "  <== NAME CHANGED" : "");
    if (p.record) {
        log_linef("  record@%p = %08x %08x %08x %08x %08x %08x %08x %08x",
                  p.record, p.field[0], p.field[1], p.field[2], p.field[3],
                  p.field[4], p.field[5], p.field[6], p.field[7]);
    }
}
#endif

void refresh_session(cw::WorldMap* map) {
    const DWORD now = GetTickCount();
    if (now - g_checked < kRecheckMs) return;
    g_checked = now;
    DIAG(probe_granularity(map));

    const std::string world = cw::world_name(map);
    if (world != g_visited.world()) {
        EnterCriticalSection(&g_lock);
        g_visited.open(world);
        LeaveCriticalSection(&g_lock);
        g_region = {};
    }

    const cw::Region region = cw::local_player_region(map);
    if (!region.valid() || region == g_region) return;

    g_region = region;
    if (g_visited.add(region.x, region.y)) {
        log_linef("entered region (%d,%d) in world '%s'", region.x, region.y,
                  g_visited.world().c_str());
        EnterCriticalSection(&g_lock);
        g_visited.flush();
        LeaveCriticalSection(&g_lock);
    }
}

// The renderer reads a cell and uses it immediately, but holds a couple alive at
// once, so hand out copies from a small per-thread ring rather than one scratch
// cell. The game's own data is never written to.
cw::MapCell* shadow_of(cw::MapCell* cell) {
    constexpr int kSlots = 64;
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
#ifdef REGIONREVEAL_DIAGNOSTICS
    const unsigned long long entered = __rdtsc();
#endif
    cw::MapCell* cell = g_get_cell.original<cw::GetCellFn>()(self, x, y);
    const bool rendering = from_map_renderer(caller);
#ifdef REGIONREVEAL_DIAGNOSTICS
    ++g_diag.calls;
    g_diag.cycles += __rdtsc() - entered;
    if (rendering) {
        ++g_diag.rendering;
        report();
    }
#endif
    if (!cell || !rendering) return cell;

    refresh_session(self);

    if (!g_visited.contains(cw::region_of(x), cw::region_of(y))) {
        DIAG(++g_diag.outside_visited);
        return cell;
    }
    if (*cw::cell_flags(cell) & cw::kRevealedBit) {
        DIAG(++g_diag.already_lit);
        return cell;
    }
    // Only cells the game itself considers to have content.
    //
    // Static reading says the marker pass at 0x4CA4FB tests the reveal bit alone,
    // so this filter looked unnecessary and was dropped. Reporting every cell of
    // a region as revealed then crashed the game: 0xC0000409 (stack buffer
    // overrun, /GS) inside the draw after ~7 minutes and 82 million shadowed
    // cells, preceded by a RADAR_PRE_LEAK memory-growth event. The build that
    // kept the filter shadowed 38 625 cells over a comparable session and did
    // not crash.
    //
    // The draw has a bounded appetite that this filter was holding it under.
    // Until that bound is located and respected explicitly, the filter stays:
    // showing fewer markers is a limitation, crashing is a defect.
    if (*cw::cell_content(cell) == 0) {
        DIAG(++g_diag.no_content);
        return cell;
    }

    DIAG(++g_diag.revealed);
    return shadow_of(cell);
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
    std::uint8_t* draw = cw::find_unique(text, cw::kSigMapOverlayDraw);
    if (!get_cell || !draw) {
        log_line("unsupported Cube World Alpha build - no hooks installed");
        return false;
    }

    g_draw_begin = draw;
    g_draw_end = end_of_function(draw, text.text_end);
    if (!g_draw_end) {
        log_line("could not bound the map draw function - no hooks installed");
        return false;
    }

    InitializeCriticalSection(&g_lock);
    g_lock_ready = true;

    if (!g_get_cell.install(get_cell, &get_cell_detour)) {
        log_line("failed to hook WorldMap::getCell");
        return false;
    }
    if (!trampoline_works()) {
        g_get_cell.remove();
        log_line("getCell trampoline did not behave - hook backed out");
        return false;
    }

    log_linef("supported build detected; RegionReveal active (map draw spans %u bytes)",
              static_cast<unsigned>(g_draw_end - g_draw_begin));
    return true;
}

void shutdown() {
    g_get_cell.remove();
    if (!g_lock_ready) return;

    EnterCriticalSection(&g_lock);
    g_visited.flush();
    LeaveCriticalSection(&g_lock);
    DeleteCriticalSection(&g_lock);
    g_lock_ready = false;
}

}  // namespace rr
