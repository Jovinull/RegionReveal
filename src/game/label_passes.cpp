#include "label_passes.hpp"

#include <windows.h>

#include <cstring>

namespace cw {
namespace {

struct Edit {
    std::uint16_t offset;  // from the start of the draw method
    std::uint8_t size;
    std::uint8_t expected[6];
    std::uint8_t replacement[6];
};

// The draw method is 6650 bytes in both builds and differs only in absolute
// addresses, so these offsets hold for both.
constexpr std::size_t kDrawSize = 6650;

// The point-of-interest pass starts with
//     comiss xmm0, [2.0] / ... / jbe <past the pass>
// where xmm0 is the map's zoom, 1.0 at the default zoom. The jump becomes a
// six-byte nop, so the pass always runs.
constexpr Edit kAnyZoom[] = {
    {0x0129, 6, {0x0F, 0x86, 0x9E, 0x0C, 0x00, 0x00}, {0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00}},
};

// Each pass bounds its loops with the radius as an 8-bit displacement or
// immediate: -0x20 (E0) for the start, +0x20 for the end, recomputed at the end
// of each row. Only that byte changes.
constexpr std::uint8_t kLow = static_cast<std::uint8_t>(-kLabelRadius);
constexpr std::uint8_t kHigh = static_cast<std::uint8_t>(kLabelRadius);
constexpr std::uint8_t kWideLow = static_cast<std::uint8_t>(-kWideLabelRadius);
constexpr std::uint8_t kWideHigh = static_cast<std::uint8_t>(kWideLabelRadius);

constexpr Edit kWideRange[] = {
    // Point-of-interest pass.
    {0x0135, 3, {0x8D, 0x70, kLow}, {0x8D, 0x70, kWideLow}},     // lea esi, [eax - r]   first x
    {0x0138, 3, {0x83, 0xC0, kHigh}, {0x83, 0xC0, kWideHigh}},   // add eax, r           x end
    {0x0151, 3, {0x8D, 0x41, kLow}, {0x8D, 0x41, kWideLow}},     // lea eax, [ecx - r]   first y
    {0x0157, 3, {0x83, 0xC1, kHigh}, {0x83, 0xC1, kWideHigh}},   // add ecx, r           y end
    {0x0D92, 3, {0x83, 0xC1, kHigh}, {0x83, 0xC1, kWideHigh}},   // add ecx, r           y end, per cell
    {0x0DAA, 3, {0x8D, 0x42, kHigh}, {0x8D, 0x42, kWideHigh}},   // lea eax, [edx + r]   x end, per row
    // Landmark pass.
    {0x0DFF, 3, {0x8D, 0x70, kLow}, {0x8D, 0x70, kWideLow}},     // lea esi, [eax - r]   first x
    {0x0E02, 3, {0x83, 0xC0, kHigh}, {0x83, 0xC0, kWideHigh}},   // add eax, r           x end
    {0x0E24, 3, {0x8D, 0x50, kLow}, {0x8D, 0x50, kWideLow}},     // lea edx, [eax - r]   first y
    {0x0E30, 3, {0x8D, 0x48, kHigh}, {0x8D, 0x48, kWideHigh}},   // lea ecx, [eax + r]   y end
    {0x1967, 3, {0x83, 0xC1, kHigh}, {0x83, 0xC1, kWideHigh}},   // add ecx, r           y end, per cell
};

template <std::size_t N>
bool matches(const std::uint8_t* draw, const Edit (&edits)[N]) {
    for (const Edit& e : edits) {
        if (std::memcmp(draw + e.offset, e.expected, e.size) != 0) return false;
    }
    return true;
}

template <std::size_t N>
void apply(std::uint8_t* draw, const Edit (&edits)[N]) {
    for (const Edit& e : edits) std::memcpy(draw + e.offset, e.replacement, e.size);
}

}  // namespace

bool label_passes_match(const std::uint8_t* draw, std::size_t drawSize) {
    return drawSize == kDrawSize && matches(draw, kAnyZoom) && matches(draw, kWideRange);
}

bool patch_label_passes(std::uint8_t* draw, std::size_t drawSize, const LabelPassOptions& options) {
    if (!options.anyZoom && !options.wideRange) return true;
    if (!label_passes_match(draw, drawSize)) return false;

    DWORD previous = 0;
    if (!VirtualProtect(draw, drawSize, PAGE_EXECUTE_READWRITE, &previous)) return false;
    if (options.anyZoom) apply(draw, kAnyZoom);
    if (options.wideRange) apply(draw, kWideRange);
    VirtualProtect(draw, drawSize, previous, &previous);
    FlushInstructionCache(GetCurrentProcess(), draw, drawSize);
    return true;
}

}  // namespace cw
