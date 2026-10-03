#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rr {

// Where the player has been, kept per world in RegionReveal_<world>.visited
// beside the game.
//
// One cell is stored for each named area the player walked into: the cell they
// stood on when they entered it. Which area that is gets worked out from the
// running game, because an area can only be identified once the world
// generator has produced its surroundings, and the file has to be meaningful
// before that. Storing a place rather than an area identity also keeps the
// file independent of how the mod names areas.
//
// The game's own save is never involved.
class VisitedAreas {
public:
    // Switches to `world`, loading its file. An empty or unusable name closes
    // the set, so nothing is recorded on the title screen.
    void open(const std::string& world);

    // Records a cell the player stood on when entering an area. Returns true
    // when it was not stored already.
    bool add(int cellX, int cellY);

    // Writes the set if it changed, through a temporary file and a replacing
    // rename so an interrupted write cannot damage the previous copy.
    void flush();

    const std::string& world() const { return world_; }

    // Sorted cell keys; see cell_key().
    const std::vector<std::uint32_t>& cells() const { return cells_; }

private:
    bool load();

    std::string world_;
    std::vector<std::uint32_t> cells_;
    bool dirty_ = false;
};

// Packs a map cell into a key that sorts by x, then y. Both axes are below
// 65536.
inline std::uint32_t cell_key(int x, int y) {
    return (static_cast<std::uint32_t>(x) << 16) | static_cast<std::uint32_t>(y);
}
inline int key_x(std::uint32_t key) { return static_cast<int>(key >> 16); }
inline int key_y(std::uint32_t key) { return static_cast<int>(key & 0xFFFF); }

}  // namespace rr
