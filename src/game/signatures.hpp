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

// MSVC pads between functions with int3; the first run of three after the
// first 0x100 bytes marks where the function starting at `begin` ends.
// Null if none is found before `limit`.
std::uint8_t* function_end(std::uint8_t* begin, std::uint8_t* limit);

// Cut from the 2013-07-20 build by tools/make_signatures.py and verified to
// match the 2013-07-02 build too, at a different address.
extern const char kSigWorldMapGetCell[];  // cube::WorldMap::getCell
extern const char kSigMapOverlayDraw[];   // cube::MapOverlayWidget's draw method
extern const char kSigWorldAreaAt[];      // cube::World's named-area lookup

}  // namespace cw
