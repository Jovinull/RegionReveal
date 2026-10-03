#pragma once

namespace rr {

// Names the game's main thread, which draws the map and keeps asking for the
// cells around the player. Areas are tracked only on that thread.
void adopt_game_thread(unsigned long thread);

// Finds the game functions by signature and installs the getCell detour.
// Leaves the process untouched, and logs why, when the running Cube.exe is not
// one of the builds in docs/TARGET_BUILD.md.
bool initialize();

}  // namespace rr
