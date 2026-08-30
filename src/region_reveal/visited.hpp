#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rr {

// Which regions RegionReveal considers visited, in its own file beside the game.
//
// The game's save is never involved: Cube World persists a region's cells
// wholesale, so a reveal bit written into one would be saved, which is exactly
// what this mod avoids. Keeping the record separate also means deleting the file
// resets the mod and nothing else.
//
// Storage is one bit per region, 1024 x 1024 regions, so 128 KiB flat. Anything
// unreadable, truncated or belonging to a different world yields an empty set:
// a corrupt file must never reveal a region, only fail to remember one.
class VisitedRegions {
public:
    // Switches to `world`, loading its file. An empty name closes the set.
    void open(const std::string& world);

    bool contains(int x, int y) const;

    // Returns true when this region was not already known, which is also the
    // only thing that makes the set dirty.
    bool add(int x, int y);

    // Writes the file when there is something new, via a temporary and a
    // replacing rename so an interrupted write cannot truncate the real one.
    void flush();

    const std::string& world() const { return world_; }
    bool open() const { return !world_.empty(); }

private:
    bool load();

    std::string world_;
    std::vector<std::uint8_t> bits_;
    bool dirty_ = false;
};

}  // namespace rr
