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
// The passes ask for every cell within 32 - or 64 with the wider range - of the
// map's centre, twice a frame, so each answer is kept per cell: the cell's area once it is known, and
// whether that area is revealed. A cell whose area cannot be decided yet is
// asked again a second later, when the world generator may have caught up.
//
// Slots are indexed by the cell's coordinates modulo 128. Any window narrower
// than that maps its cells to distinct slots, so a copy handed out for a cell
// stays that cell's copy for the whole frame. Everything here belongs to the
// game thread.
class LabelCells {
public:
    static constexpr int kSide = 128;
    static constexpr std::uint32_t kRetryMs = 1000;
    static_assert(kSide >= 2 * cw::kWideLabelRadius, "a label window must map to distinct slots");

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
    std::vector<Slot> slots_;  // allocated on first use: about 1.3 MB
};

}  // namespace rr
