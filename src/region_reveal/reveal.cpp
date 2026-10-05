#include "reveal.hpp"

#include <windows.h>
#include <intrin.h>

#include <atomic>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "../game/cube_world.hpp"
#include "../game/label_passes.hpp"
#include "../game/label_text.hpp"
#include "../game/session.hpp"
#include "../game/signatures.hpp"
#include "../game/world.hpp"
#include "../hooks.hpp"
#include "../threads.hpp"
#include "areas.hpp"
#include "log.hpp"
#include "marks.hpp"
#include "settings.hpp"
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
MarkedText g_marked;
Options g_options;

// The map the label passes last drew, for the landmark name marks, and the
// game's own test of a place against a cell.
cw::WorldMap* g_label_map = nullptr;
cw::PlaceTestFn g_place_test = nullptr;

// The place whose name is being drawn and its mark, worked out once for both
// of its draws.
const std::uint8_t* g_marked_record = nullptr;
PlaceMark g_mark = PlaceMark::None;

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
void track(cw::WorldMap* map, DWORD now) {
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
        if (GetCurrentThreadId() == g_game_thread.load(std::memory_order_relaxed)) track(self, GetTickCount());
        return cell;
    }

    // The label passes run on the game thread by definition. They ask for up
    // to 64 516 cells a frame, so this path does as little as it can.
    g_game_thread.store(GetCurrentThreadId(), std::memory_order_relaxed);
    g_label_map = self;
    const DWORD now = GetTickCount();
    track(self, now);
    if (!cell) return cell;

    // Zoomed out, a city's districts pile up on top of its name, and the game
    // itself would not draw them at that zoom: hide them there.
    if (caller == g_label_calls[0] && !g_options.farDistricts &&
        reinterpret_cast<const std::uint8_t*>(cell)[cw::kCellPoi] == cw::kPoiCityDistrict &&
        *reinterpret_cast<const float*>(cw::owner_of(self) + cw::kOwnerMapZoom) <= cw::kPoiZoom) {
        return g_cells.hidden(cell, x, y);
    }

    if (cw::cell_revealed(cell) || g_areas.count() == 0) return cell;
    return g_cells.view(cell, x, y, now, g_areas, lookup_in_game, self);
}

// Read through the trampoline: whether the game itself has revealed the cell.
bool revealed_in_game(void* map, const std::uint8_t*, int x, int y) {
    const cw::MapCell* cell = g_get_cell.original<cw::GetCellFn>()(static_cast<cw::WorldMap*>(map), x, y);
    return cell && cw::cell_revealed(cell);
}

bool inside_place(void*, const std::uint8_t* record, int x, int y) {
    const std::int64_t centreX = cell_centre(x);
    const std::int64_t centreY = cell_centre(y);
    return g_place_test(record, &centreX, &centreY) > 0.0f;
}

// The landmark pass's two text draws come here with the place being labelled:
// the outline first, then the text, for the same place.
const cw::GameWString* mark_label(const std::uint8_t* record, const cw::GameWString* text, float* color,
                                  bool foreground) {
    if (!record || !g_label_map) return text;
    if (!foreground || record != g_marked_record) {
        g_marked_record = record;
        g_mark = place_mark(record, place_seen(record, revealed_in_game, inside_place, g_label_map));
    }
    if (foreground) apply_mark_color(g_mark, color);
    return g_marked.apply(text, g_mark);
}

// The label passes are the draw method's only two calls to getCell. Anything
// other than exactly two means this is not the code the mod was written for.
bool find_label_calls(std::uint8_t* draw, std::uint8_t* end, std::uint8_t* getCell) {
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
    std::uint8_t* draw_end = cw::function_end(draw, text.text_end);
    if (!draw_end || !find_label_calls(draw, draw_end, get_cell)) {
        log_line("could not find the map's two label passes - no hook installed");
        return false;
    }

    g_options = read_options();
    const std::size_t draw_size = static_cast<std::size_t>(draw_end - draw);
    g_place_test = cw::place_test_of(draw, draw_size);

    // Loaded through the import table, this runs before the game has started
    // any thread. Injected by a loader, the game may already be running, so
    // every other thread is held while code is rewritten. Nothing in between
    // may take a lock another thread could hold: no logging, no allocation.
    const bool injected = GetCurrentThreadId() != main_thread_id();
    bool hooked = false;
    bool working = false;
    bool labels = false;
    bool marks = false;
    {
        // A stopped thread is always at an instruction boundary. The label
        // edits replace whole instructions or only their operands, so only the
        // five stolen bytes, three instructions long, need guarding.
        const std::vector<ThreadFreeze::Range> guarded = {{get_cell, 5}};
        std::unique_ptr<ThreadFreeze> freeze(injected ? new ThreadFreeze(guarded) : nullptr);
        if (!freeze || freeze->ok()) {
            hooked = g_get_cell.install(get_cell, &get_cell_detour);
            working = hooked && trampoline_works();
            if (hooked && !working) g_get_cell.remove();
            if (working) {
                labels = cw::patch_label_passes(draw, draw_size, g_options.labels);
                marks = g_options.marks && cw::hook_label_text(draw, draw_size, &mark_label);
            }
        }
    }
    if (!hooked) {
        log_line("could not hook WorldMap::getCell - no hook installed");
        return false;
    }
    if (!working) {
        log_line("the getCell trampoline did not behave - hook removed");
        return false;
    }

    // Optional, and independent of the reveal: if the bytes are not exactly the
    // expected ones, labels simply keep the game's own zoom and range limits.
    if (labels) {
        g_cells.set_radius(g_options.labels.radius);
        log_linef("labels: points of interest %s, %d cells around the map's centre",
                  g_options.labels.anyZoom ? "at every zoom" : "when zoomed in", g_options.labels.radius);
    } else {
        log_line("labels: the map's label passes did not match - keeping the game's zoom and range limits");
    }
    log_linef("marks: %s", marks ? "visited and boss-defeated marks on landmark names"
                                 : (g_options.marks ? "the landmark name draws did not match - no marks" : "off"));

    // The game now calls into this DLL, so it must never be unloaded.
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                       reinterpret_cast<LPCWSTR>(&g_get_cell), &self);

    LARGE_INTEGER finished{};
    QueryPerformanceCounter(&finished);
    log_linef("supported build detected; RegionReveal active (%s, set up in %.0f ms)",
              injected ? "loaded by a mod loader" : "loaded as dinput8.dll",
              static_cast<double>(finished.QuadPart - started.QuadPart) * 1000.0 /
                  static_cast<double>(frequency.QuadPart));
    return true;
}

}  // namespace rr
