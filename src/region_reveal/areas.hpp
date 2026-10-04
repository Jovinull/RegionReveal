#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "../game/cube_world.hpp"
#include "../game/label_passes.hpp"
#include "../game/world.hpp"

namespace rr {

// How an area is looked up: cw::area_of_cell in the game, a fake in the tests.
using AreaLookupFn = cw::AreaLookup (*)(void* context, int cellX, int cellY);

// The areas revealed in the current world.
//
// Filled from the cells stored in the visited file. Each is looked up in the
// running game, and only resolves once the game has generated the area centres
// around it, so a history from far away waits until the player is near again.
class RevealedAreas {
public:
    // A different world: forget every area and queue the stored cells.
    void reset(const std::vector<std::uint32_t>& storedCells);

    // Looks up the queued cells again; those that resolve become areas.
    void resolve(AreaLookupFn lookup, void* context);

    // Returns true when the area was not revealed yet.
    bool add(cw::AreaId area);

    bool contains(cw::AreaId area) const;
    std::size_t count() const { return areas_.size(); }
    std::size_t pending() const { return pending_.size(); }

    // Changes whenever the set does, so cached answers know to recheck.
    unsigned generation() const { return generation_; }

private:
    std::vector<cw::AreaId> areas_;       // sorted
    std::vector<std::uint32_t> pending_;  // stored cell keys not resolved yet
    unsigned generation_ = 0;
};

// What the map overlay's label passes are told about each cell.
//
// The passes ask for every cell within their radius of the map's centre, twice
// a frame - up to 64 516 cells - so each answer is kept per cell: the cell's area once it is known, and
// whether that area is revealed. A cell whose area cannot be decided yet is
// asked again a second later, when the world generator may have caught up.
//
// Slots are indexed by the cell's coordinates modulo a power of two at least as
// wide as the label window, so the cells a pass asks for in one frame map to
// distinct slots and a copy handed out for a cell stays that cell's copy for
// the whole frame. Everything here belongs to the game thread.
class LabelCells {
public:
    static constexpr std::uint32_t kRetryMs = 1000;

    // The label passes' radius; sizes the table. Call before the first frame.
    void set_radius(int radius);

    // Whether (x, y) lies in a revealed area. `now` is GetTickCount().
    bool revealed(int x, int y, std::uint32_t now, const RevealedAreas& areas, AreaLookupFn lookup,
                  void* context);

    // What a label pass asking for (x, y) is handed: `cell` itself, or, when
    // the cell lies in a revealed area, a copy with the reveal bit set. The copy
    // is taken afresh on every call, so it never goes stale.
    cw::MapCell* view(cw::MapCell* cell, int x, int y, std::uint32_t now, const RevealedAreas& areas,
                      AreaLookupFn lookup, void* context);

    // Forgets every answer: they belong to the previous world.
    void clear();

private:
    enum class State : std::uint8_t { Unasked, Unknown, Known };

    struct Slot {
        std::uint32_t epoch = 0;  // never equal to a live epoch, so a fresh slot is empty
        int x = 0;
        int y = 0;
        State state = State::Unasked;
        bool member = false;
        std::uint32_t checkedAt = 0;
        unsigned memberGeneration = 0;
        cw::AreaId area = 0;
        std::uint8_t copy[cw::kCellSize] = {};
    };

    Slot& slot(int x, int y);
    bool decide(Slot& s, std::uint32_t now, const RevealedAreas& areas, AreaLookupFn lookup, void* context);

    std::uint32_t epoch_ = 1;
    int side_ = 64;            // a power of two, at least twice the radius
    std::vector<Slot> slots_;  // side_ * side_, allocated on first use: 80 bytes a slot
};

}  // namespace rr
