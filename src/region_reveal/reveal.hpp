#pragma once

namespace rr {

// Resolves the game functions by signature and installs the detours.
// Returns false without touching the process when the running Cube.exe is not
// one of the builds documented in docs/TARGET_BUILD.md.
bool initialize();

void shutdown();

}  // namespace rr
