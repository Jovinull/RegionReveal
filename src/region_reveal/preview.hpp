#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include "preview_tile.hpp"
#include "settings.hpp"

namespace cw {
struct WorldMap;
struct TileImage;
}

namespace rr {

class RevealedAreas;
class RevealWindow;

// The work behind area reveal, done on the game's own thread in short slices.
//
// Around the map's view centre it keeps two things current: the bitmap of cells
// in revealed areas, which the label detour reads, and a preview tile for every
// such cell the map would draw that has no real tile - built from the world
// generator's height function, attached to the cell in memory only, and freed
// again once the view moves away. Real tiles always win: a cell that has one, or
// has one in the save, is never touched, and the game destroys a preview itself
// when it generates the real tile.
//
// Everything runs from the getCell detour on the game thread, a few milliseconds
// a frame. An earlier version ran on a thread of its own and crashed the game
// twice on exit: the game tears down its World and WorldMap after stopping its
// own workers, knew nothing of that thread, and it went on taking a lock that no
// longer existed. On the game thread there is nothing to race - when the game
// stops calling getCell, this stops running.
//
// Previews exist only while the map is open. Cube.exe is a 32-bit process that
// is not large-address-aware and already runs within a few hundred MB of its
// 2 GB limit; walking costs it another ~200 MB as zones generate, so nothing is
// held while the map is closed, and building pauses when address space runs low.
class PreviewEngine {
public:
    PreviewEngine(RevealedAreas& areas, RevealWindow& window);

    // Called on every getCell the game thread makes; does a bounded slice of
    // work at most once a frame.
    void tick(cw::WorldMap* map);

    // Called on every map draw: the map is open while these keep coming.
    void note_map_drawn() { lastDraw_ = GetTickCount(); }

private:
    struct Preview {
        int x;
        int y;
        cw::TileImage* image;
    };

    void forget_world();
    void step_window(cw::WorldMap* map, int cx, int cy, LONGLONG deadline);
    void validate(cw::WorldMap* map);
    void release(cw::WorldMap* map, int cx, int cy, int keepRadius);
    void step_build(cw::WorldMap* map, int cx, int cy, int radius, LONGLONG deadline);
    void report(DWORD now);

    bool in_window(int x, int y) const;

    RevealedAreas& areas_;
    RevealWindow& window_;

    LARGE_INTEGER frequency_{};
    LONGLONG lastTick_ = 0;
    DWORD lastDraw_ = 0;

    std::string world_;
    DWORD worldCheckedAt_ = 0;

    PreviewSettings settings_;
    DWORD settingsAt_ = 0;
    unsigned freeMb_ = ~0u;
    DWORD memoryAt_ = 0;
    bool paused_ = false;

    // The published bitmap the builder works from, and the one being computed.
    std::vector<bool> member_;
    bool haveWindow_ = false;
    int winX_ = 0;
    int winY_ = 0;
    unsigned winGeneration_ = ~0u;
    DWORD winAt_ = 0;
    unsigned revealedCells_ = 0;

    std::vector<bool> next_;
    bool computing_ = false;
    int nextX_ = 0;
    int nextY_ = 0;
    unsigned nextGeneration_ = 0;
    int nextRow_ = 0;
    unsigned nextRevealed_ = 0;
    std::vector<std::int32_t> seeds_;

    std::vector<Preview> live_;
    std::unordered_set<std::uint32_t> liveKeys_;

    // The preview being sampled, a few rows a frame.
    bool building_ = false;
    int buildX_ = 0;
    int buildY_ = 0;
    int buildRow_ = 0;
    PreviewSamples samples_;

    unsigned built_ = 0;
    double buildMs_ = 0;
    std::size_t reportedLive_ = 0;
    DWORD reportAt_ = 0;
};

}  // namespace rr
