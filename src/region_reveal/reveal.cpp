#include "reveal.hpp"

#include <windows.h>
#include <intrin.h>

#include <climits>
#include <cstdio>
#include <cstring>
#include <string>

#include "../game/cube_world.hpp"
#include "../game/landmarks.hpp"
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

// One copy per cell, never a shared slot.
//
// The previous version handed out copies from a small ring, which is only safe
// while the renderer holds fewer pointers than the ring has slots - a number
// nobody could derive. Keying on the cell removes the question: a pointer handed
// out for cell (x,y) is only ever reused for that same cell, so two live
// pointers can never alias no matter how many the caller keeps.
//
// The map draws at most one region's cells, and only cells with content and
// without the bit get a copy, so the table is sized past the whole 64x64 window
// the renderer walks. If it ever did fill, shadowing stops rather than evicting
// something a caller may still hold.
class ShadowCache {
public:
    cw::MapCell* get(cw::MapCell* cell, int x, int y) {
        const std::size_t start = hash(x, y);
        for (std::size_t i = 0; i < kSlots; ++i) {
            Slot& slot = slots_[(start + i) & (kSlots - 1)];
            if (slot.used && slot.x == x && slot.y == y) {
                return reinterpret_cast<cw::MapCell*>(slot.bytes);
            }
            if (!slot.used) {
                slot.used = true;
                slot.x = x;
                slot.y = y;
                std::memcpy(slot.bytes, cell, cw::kCellStride);
                slot.bytes[cw::kCellFlags] |= cw::kRevealedBit;
                ++live_;
                return reinterpret_cast<cw::MapCell*>(slot.bytes);
            }
        }
        return nullptr;  // full: hand back the real cell instead of aliasing
    }

    // Only called when the region or world changes, between draws.
    void reset() {
        for (Slot& slot : slots_) slot.used = false;
        live_ = 0;
    }

    std::size_t live() const { return live_; }

private:
    static constexpr std::size_t kSlots = 8192;  // > 64*64 cells in view

    struct Slot {
        bool used = false;
        int x = 0;
        int y = 0;
        std::uint8_t bytes[cw::kCellStride] = {};
    };

    static std::size_t hash(int x, int y) {
        return (static_cast<std::size_t>(x) * 73856093u ^
                static_cast<std::size_t>(y) * 19349663u) &
               (kSlots - 1);
    }

    Slot slots_[kSlots];
    std::size_t live_ = 0;
};

ShadowCache g_shadows;

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
    char raw[128];
    int used = 0;
    for (int i = 0; i < 24; ++i) {
        used += _snprintf_s(raw + used, sizeof(raw) - used, _TRUNCATE, "%02x", p.nameRaw[i]);
    }
    log_linef("  nameGlobal   = %s", raw);
    used = 0;
    for (int i = 0; i < 24; ++i) {
        used += _snprintf_s(raw + used, sizeof(raw) - used, _TRUNCATE, "%02x", p.detailRaw[i]);
    }
    log_linef("  detailGlobal = %s", raw);
}
#endif

// Names what each region of the survey should contribute, so the expected
// markers are known before anyone opens the map. A region whose storage chunk is
// not resident says so rather than reporting a landmark it cannot see.
void log_survey(cw::WorldMap* map, const cw::Region& centre) {
    for (int dx = -rr::kSurveyRadius; dx <= rr::kSurveyRadius; ++dx) {
        for (int dy = -rr::kSurveyRadius; dy <= rr::kSurveyRadius; ++dy) {
            const int rx = centre.x + dx;
            const int ry = centre.y + dy;
            if (rx < 0 || ry < 0 || rx >= cw::kRegionDim || ry >= cw::kRegionDim) continue;

            const cw::Landmark found = cw::landmark_at(map, rx, ry);
            switch (found.status) {
                case cw::LandmarkLookup::Ok:
                    log_linef("  survey (%d,%d) raw=%u %s [%s]%s", rx, ry, found.raw,
                              cw::landmark_name(found.raw),
                              cw::landmark_kind_name(cw::landmark_kind(found.raw)),
                              (dx == 0 && dy == 0) ? "  <- centre" : "");
                    break;
                case cw::LandmarkLookup::Unreadable:
                    log_linef("  survey (%d,%d) UNREADABLE", rx, ry);
                    break;
                case cw::LandmarkLookup::NoChunk:
                    log_linef("  survey (%d,%d) NO RECORD (chunk not resident)", rx, ry);
                    break;
            }
        }
    }
}

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
        g_shadows.reset();
    }

    const cw::Region region = cw::local_player_region(map);
    if (!region.valid() || region == g_region) return;

    g_region = region;
    g_shadows.reset();

    // Dumped whenever the region changes, not only on a first visit. An earlier
    // build logged it only for a new centre, so re-entering a known world -
    // which is the normal case - produced no survey at all.
    const bool isNew = g_visited.visit(region.x, region.y);
    log_linef("%s region (%d,%d) in world '%s'; radius %d -> %u regions covered",
              isNew ? "visited NEW" : "re-entered", region.x, region.y,
              g_visited.world().c_str(), rr::kSurveyRadius,
              static_cast<unsigned>(g_visited.covered()));
    log_survey(map, region);
    if (!isNew) return;

    EnterCriticalSection(&g_lock);
    g_visited.flush();
    LeaveCriticalSection(&g_lock);
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

    if (!g_visited.revealed(cw::region_of(x), cw::region_of(y))) {
        DIAG(++g_diag.outside_visited);
        return cell;
    }
    if (*cw::cell_flags(cell) & cw::kRevealedBit) {
        DIAG(++g_diag.already_lit);
        return cell;
    }
    // Deliberately not filtered on the cell's +0x10 field.
    //
    // That field means "this cell has generated content", and an unexplored cell
    // has none - so filtering on it left nothing at all to reveal: a live session
    // logged noContent=334873 against REVEALED=0. The marker pass at 0x4CA4FB
    // gates only on the reveal bit and takes its icon from the region's own 0x68
    // record, so a contentless cell can still carry a marker.
    //
    // Removing this filter once before crashed the game (0xC0000409 in the draw)
    // after 82 million shadowed cells handed out from an aliasing ring. Both of
    // those changed: shadows are now keyed by cell so they cannot alias, and a
    // region is 64 cells rather than a 4096-cell storage chunk, so the volume is
    // roughly three orders of magnitude lower. If it faults again the cause is
    // semantic rather than lifetime, and the filter comes back for good.
#ifdef REGIONREVEAL_DIAGNOSTICS
    if (*cw::cell_content(cell) == 0) ++g_diag.no_content;
    ++g_diag.revealed;
#endif
    cw::MapCell* shadow = g_shadows.get(cell, x, y);
    return shadow ? shadow : cell;
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
