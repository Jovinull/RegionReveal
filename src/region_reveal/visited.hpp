#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rr {

// Which gameplay regions RegionReveal considers visited, in its own file beside
// the game.
//
// The game's save is never involved: Cube World persists a storage chunk's cells
// wholesale, so a reveal bit written into one would be saved, which is exactly
// what this mod avoids. Keeping the record separate also means deleting the file
// resets the mod and nothing else.
//
// A region is 8x8 cells and the world is 8192 regions per axis, so a full bitset
// would be 8 MiB of almost entirely zeroes. A player visits hundreds, so the
// file is a sorted list of the ones actually seen. Anything unreadable, of the
// wrong version, or belonging to a different world yields an empty set: a
// damaged file must never reveal a region, only fail to remember one.
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
    std::size_t size() const { return keys_.size(); }

private:
    bool load();

    std::string world_;
    std::vector<std::uint32_t> keys_;  // sorted; (x << 16) | y
    bool dirty_ = false;
};

// Packs a region coordinate pair into the key the file stores. Both axes fit in
// 13 bits, so 16 bits each leaves the format room to grow.
std::uint32_t region_key(int x, int y);

}  // namespace rr
