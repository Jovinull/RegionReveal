#pragma once

namespace rr {

// Records a fatal exception in RegionReveal.log before Windows Error Reporting
// takes over: the code, where it happened as module + offset, which thread, and
// the return addresses found on the stack. The game ships no symbols and WER
// keeps its reports out of reach, so this line is what makes a crash in the
// field attributable at all.
//
// Built with REGIONREVEAL_CRASHDUMP it also writes RegionReveal_crash.dmp
// beside the game for a debugger.
void install_crash_reporter();

}  // namespace rr
