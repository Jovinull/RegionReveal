#pragma once

#include <sal.h>

namespace rr {

// Appends a line to RegionReveal.log beside the game executable. The mod is
// hard to observe from inside the game, so the log is the only record of which
// build was detected, whether the hook went in and which areas were entered.
void log_line(const char* message);

void log_linef(_In_z_ _Printf_format_string_ const char* format, ...);

}  // namespace rr
