#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rr {

// Where the player has been, kept per world in its own file beside the game.
//
// One cell is stored for each named area the player walked into - the cell they
// were standing on when they entered it. Which area that is, and so what gets
// revealed, is worked out from the running game: an area is only known while its
// storage chunk is resident, and the file has to make sense before that. Storing
// a place rather than an area identity also means the file never depends on how
// areas are numbered.
//
// The game's save is never involved: Cube World persists a storage chunk's cells
// wholesale, so a reveal bit written into one would be saved, which is exactly
// what this mod avoids.
class VisitedAreas {
public:
    // Switches to `world`, loading its file. An empty name closes the set.
    void open(const std::string& world);

    // Records a cell the player stood on when entering an area. Returns true
    // when it was not already stored, which is also the only thing that makes
    // the set dirty.
    bool add(int cellX, int cellY);

    // Writes the cells through a temporary and a replacing rename.
    void flush();

    const std::string& world() const { return world_; }
    // Sorted packed cell keys; see cell_key().
    const std::vector<std::uint32_t>& cells() const { return cells_; }

private:
    bool load();

    std::string world_;
    std::vector<std::uint32_t> cells_;
    bool dirty_ = false;
};

// Packs a map cell coordinate pair; both axes are below 65536.
std::uint32_t cell_key(int x, int y);
inline int key_x(std::uint32_t key) { return static_cast<int>(key >> 16); }
inline int key_y(std::uint32_t key) { return static_cast<int>(key & 0xFFFF); }

}  // namespace rr
