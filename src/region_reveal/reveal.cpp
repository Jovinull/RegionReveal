#include "reveal.hpp"

#include <windows.h>
#include <intrin.h>

#include <atomic>
#include <cstring>
#include <string>

#include "../game/cube_world.hpp"
#include "../game/session.hpp"
#include "../game/signatures.hpp"
#include "../game/world.hpp"
#include "../hooks.hpp"
#include "areas.hpp"
#include "log.hpp"
#include "visited.hpp"

namespace rr {
namespace {

InlineHook g_get_cell;

// Where the map overlay's two getCell calls return to: one in the pass that
// draws each cell's point of interest, one in the pass that draws landmark
// names. Both draw a label only for a revealed cell, and nothing else in the
// game is told anything different.
const void* g_label_calls[2] = {};

// The game's main thread. It draws the map, and WorldMap::discover keeps
// calling getCell on it with the cells around the player; the other callers
// are worker threads. Everything below is only ever touched on this thread,
// which is why none of it needs a lock.
std::atomic<DWORD> g_game_thread{0};

VisitedAreas g_visited;
RevealedAreas g_areas;
LabelCells g_cells;

std::string g_world;  // the name last read from the game, usable or not
bool g_in_area = false;
cw::AreaId g_area = 0;
DWORD g_tracked_at = 0;

// An area is dozens of cells of 256 blocks across, so the player cannot cross
// one in a quarter second, and getCell is called far more often than that.
constexpr DWORD kTrackEveryMs = 250;

cw::AreaLookup lookup_in_game(void* map, int x, int y) {
    return cw::area_of_cell(static_cast<cw::WorldMap*>(map), x, y);
}

// Follows the world and the player's area, and records each area entered.
void track(cw::WorldMap* map) {
    const DWORD now = GetTickCount();
    if (now - g_tracked_at < kTrackEveryMs) return;
    g_tracked_at = now;

    const std::string world = cw::world_name(map);
    if (world != g_world) {
        g_world = world;
        g_visited.open(world);
        g_areas.reset(g_visited.cells());
        g_cells.clear();
        g_in_area = false;
    }
    if (g_visited.world().empty()) return;

    g_areas.resolve(lookup_in_game, map);

    const cw::Cell here = cw::local_player_cell(map);
    if (!here.valid()) return;
    const cw::AreaLookup area = cw::area_of_cell(map, here.x, here.y);
    if (!area.known || (g_in_area && area.area == g_area)) return;

    // Proof the player is really there: the game reveals the cells around the
    // local player as they move. A loading world's name and the player's
    // position need not change in the same frame, and the title screen's
    // placeholder position must never be recorded. Read through the
    // trampoline, so this is the real cell.
    const cw::MapCell* cell = g_get_cell.original<cw::GetCellFn>()(map, here.x, here.y);
    if (!cell || !cw::cell_revealed(cell)) return;

    g_in_area = true;
    g_area = area.area;
    const bool first = g_areas.add(area.area);
    log_linef("%s area (%d,%d) at cell (%d,%d) in world '%s'", first ? "entered new" : "back in",
              cw::area_chunk_x(area.area), cw::area_chunk_y(area.area), here.x, here.y, world.c_str());
    if (first && g_visited.add(here.x, here.y)) g_visited.flush();
}

cw::MapCell* __fastcall get_cell_detour(cw::WorldMap* self, void*, int x, int y) {
    cw::MapCell* cell = g_get_cell.original<cw::GetCellFn>()(self, x, y);
    const void* caller = _ReturnAddress();

    if (caller != g_label_calls[0] && caller != g_label_calls[1]) {
        // An area counts as entered when the player walks into it, map open or
        // not, so tracking also runs from gameplay's calls.
        if (GetCurrentThreadId() == g_game_thread.load(std::memory_order_relaxed)) track(self);
        return cell;
    }

    // The label passes run on the game thread by definition.
    g_game_thread.store(GetCurrentThreadId(), std::memory_order_relaxed);
    track(self);

    if (!cell || cw::cell_revealed(cell) || g_areas.count() == 0) return cell;
    if (!g_cells.revealed(x, y, GetTickCount(), g_areas, lookup_in_game, self)) return cell;
    return g_cells.revealed_copy(cell, x, y);
}

// MSVC pads between functions with int3; the first run of them after the
// entry marks the end of the draw method.
std::uint8_t* end_of_function(std::uint8_t* begin, std::uint8_t* limit) {
    for (std::uint8_t* at = begin + 0x100; at + 3 <= limit; ++at) {
        if (at[0] == 0xCC && at[1] == 0xCC && at[2] == 0xCC) return at;
    }
    return nullptr;
}

// The label passes are the draw method's only two calls to getCell. Anything
// other than exactly two means this is not the code the mod was written for.
bool find_label_calls(std::uint8_t* draw, std::uint8_t* limit, std::uint8_t* getCell) {
    std::uint8_t* end = end_of_function(draw, limit);
    if (!end) return false;

    int found = 0;
    for (std::uint8_t* at = draw; at + 5 <= end; ++at) {
        if (*at != 0xE8) continue;  // call rel32
        std::int32_t offset = 0;
        std::memcpy(&offset, at + 1, sizeof(offset));
        if (at + 5 + offset != getCell) continue;
        if (found == 2) return false;
        g_label_calls[found++] = at + 5;
    }
    return found == 2;
}

// getCell rejects a negative coordinate before it touches `this`, so calling
// it through the trampoline with a null instance has no side effect and must
// return null. A malformed trampoline faults here rather than mid-game.
bool trampoline_works() {
    __try {
        return g_get_cell.original<cw::GetCellFn>()(nullptr, -1, -1) == nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

void adopt_game_thread(unsigned long thread) { g_game_thread.store(thread, std::memory_order_relaxed); }

bool initialize() {
    LARGE_INTEGER started{};
    LARGE_INTEGER frequency{};
    QueryPerformanceCounter(&started);
    QueryPerformanceFrequency(&frequency);

    cw::ModuleRange text{};
    if (!cw::module_text(&text)) {
        log_line("could not locate the .text section of Cube.exe - no hook installed");
        return false;
    }

    std::uint8_t* get_cell = cw::find_unique(text, cw::kSigWorldMapGetCell);
    std::uint8_t* draw = cw::find_unique(text, cw::kSigMapOverlayDraw);
    if (!get_cell || !draw || !cw::resolve_world_api(text)) {
        log_line("unsupported Cube World Alpha build - no hook installed");
        return false;
    }
    if (!find_label_calls(draw, text.text_end, get_cell)) {
        log_line("could not find the map's two label passes - no hook installed");
        return false;
    }

    if (!g_get_cell.install(get_cell, &get_cell_detour)) {
        log_line("could not hook WorldMap::getCell - no hook installed");
        return false;
    }
    if (!trampoline_works()) {
        g_get_cell.remove();
        log_line("the getCell trampoline did not behave - hook removed");
        return false;
    }

    // The game now calls into this DLL, so it must never be unloaded.
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                       reinterpret_cast<LPCWSTR>(&g_get_cell), &self);

    LARGE_INTEGER finished{};
    QueryPerformanceCounter(&finished);
    log_linef("supported build detected; RegionReveal active (set up in %.0f ms)",
              static_cast<double>(finished.QuadPart - started.QuadPart) * 1000.0 /
                  static_cast<double>(frequency.QuadPart));
    return true;
}

}  // namespace rr
