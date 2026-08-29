#pragma once

namespace rr {

// Appends a line to RegionReveal.log beside the game executable. The mod is
// hard to observe from inside the game, so the log is the only record of which
// build was detected and whether the hooks went in.
void log_line(const char* message);

void log_linef(const char* format, ...);

}  // namespace rr
