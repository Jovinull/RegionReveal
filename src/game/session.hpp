#pragma once

#include <string>

#include "cube_world.hpp"

namespace cw {

struct Region {
    int x = -1;
    int y = -1;

    bool valid() const { return x >= 0 && y >= 0 && x < kGridDim && y < kGridDim; }
    bool operator==(const Region& other) const { return x == other.x && y == other.y; }
    bool operator!=(const Region& other) const { return !(*this == other); }
};

// Reads the local player's position out of the object that owns `map` and
// converts it to a region. Returns an invalid Region whenever the chain cannot
// be trusted - on the title screen the player slot is not populated yet.
Region local_player_region(WorldMap* map);

// The world's name, as the game itself uses it to build "Save/map_<name>.db".
// Empty when it cannot be read.
std::string world_name(WorldMap* map);

// True when `address` can be read for `size` bytes without faulting.
bool readable(const void* address, std::size_t size);

}  // namespace cw
