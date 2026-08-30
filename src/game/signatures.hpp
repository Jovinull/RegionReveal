#pragma once

#include <cstddef>
#include <cstdint>

namespace cw {

struct ModuleRange {
    std::uint8_t* text_begin;
    std::uint8_t* text_end;
};

// Locates the .text section of the running Cube.exe.
bool module_text(ModuleRange* out);

// Scans `range` for an IDA-style pattern ("55 8B EC ?? ?? E8"). Returns the
// single match, or nullptr when the pattern is absent or ambiguous - a second
// hit is treated as a failure so an unrecognised build can never be hooked.
std::uint8_t* find_unique(const ModuleRange& range, const char* pattern);

// Signatures were cut from the 2013-07-20 build and verified to match the
// 2013-07-02 build at a different address; see tools/make_signatures.py.
extern const char kSigWorldMapGetCell[];
extern const char kSigMapOverlayDraw[];

}  // namespace cw
