#pragma once

#include <cstddef>
#include <string>

#include "cube_world.hpp"

namespace cw {

// A map cell coordinate pair.
struct Cell {
    int x = -1;
    int y = -1;

    bool valid() const { return x >= 0 && y >= 0 && x < kMapCells && y < kMapCells; }
};

// The map cell under the local player, read from the controller that owns
// `map`. Invalid whenever the pointer chain cannot be trusted. A valid answer
// is not proof the player is in a world: the title screen runs an unnamed one
// with a player standing at a placeholder position.
Cell local_player_cell(WorldMap* map);

// The world's name, as the game itself uses it for "Save/map_<name>.db".
// Empty on the title screen and whenever it cannot be read.
std::string world_name(WorldMap* map);

// True when `size` bytes at `address` are committed and readable.
bool readable(const void* address, std::size_t size);

}  // namespace cw
