#pragma once

namespace rr {

// Resolves the game functions by signature and installs the detours.
// Returns false without touching the process when the running Cube.exe is not
// one of the builds documented in docs/TARGET_BUILD.md.
bool initialize();

// Names the game's main thread, so regions are tracked from the first frame
// rather than from the first time the map is drawn. Optional: without it the
// thread is learnt from the map renderer.
void adopt_game_thread(unsigned long thread);

void shutdown();

}  // namespace rr
