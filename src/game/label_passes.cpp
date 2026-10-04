#include "label_passes.hpp"

#include <windows.h>

#include <algorithm>
#include <cstring>

namespace cw {
namespace {

// The draw method is 6650 bytes in both builds and differs only in absolute
// addresses, so these offsets hold for both.
constexpr std::size_t kDrawSize = 6650;

// The point-of-interest pass starts with
//     comiss xmm0, [2.0] / ... / jbe <past the pass>
// where xmm0 is the map's zoom, 1.0 at the default zoom. The jump becomes a
// six-byte nop, so the pass always runs.
constexpr std::uint16_t kZoomJump = 0x0129;
constexpr std::uint8_t kZoomJumpBytes[] = {0x0F, 0x86, 0x9E, 0x0C, 0x00, 0x00};
constexpr std::uint8_t kSixByteNop[] = {0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00};

// Each pass bounds its loops with the radius as the last byte of an
// instruction, an 8-bit displacement or immediate: -0x20 (E0) where a loop
// starts, +0x20 where it ends, the end recomputed after each row and cell.
struct RadiusByte {
    std::uint16_t offset;  // of the instruction, from the start of the draw method
    std::uint8_t opcode[2];
    bool negative;
};

constexpr RadiusByte kRadiusBytes[] = {
    // Point-of-interest pass.
    {0x0135, {0x8D, 0x70}, true},   // lea esi, [eax - r]   first x
    {0x0138, {0x83, 0xC0}, false},  // add eax, r           x end
    {0x0151, {0x8D, 0x41}, true},   // lea eax, [ecx - r]   first y
    {0x0157, {0x83, 0xC1}, false},  // add ecx, r           y end
    {0x0D92, {0x83, 0xC1}, false},  // add ecx, r           y end, per cell
    {0x0DAA, {0x8D, 0x42}, false},  // lea eax, [edx + r]   x end, per row
    // Landmark pass.
    {0x0DFF, {0x8D, 0x70}, true},   // lea esi, [eax - r]   first x
    {0x0E02, {0x83, 0xC0}, false},  // add eax, r           x end
    {0x0E24, {0x8D, 0x50}, true},   // lea edx, [eax - r]   first y
    {0x0E30, {0x8D, 0x48}, false},  // lea ecx, [eax + r]   y end
    {0x1967, {0x83, 0xC1}, false},  // add ecx, r           y end, per cell
};

std::uint8_t radius_byte(int radius, bool negative) {
    return static_cast<std::uint8_t>(negative ? -radius : radius);
}

}  // namespace

int clamp_label_radius(int radius) { return std::min(kMaxLabelRadius, std::max(kLabelRadius, radius)); }

bool label_passes_match(const std::uint8_t* draw, std::size_t drawSize) {
    if (drawSize != kDrawSize) return false;
    if (std::memcmp(draw + kZoomJump, kZoomJumpBytes, sizeof(kZoomJumpBytes)) != 0) return false;
    for (const RadiusByte& b : kRadiusBytes) {
        const std::uint8_t* at = draw + b.offset;
        if (at[0] != b.opcode[0] || at[1] != b.opcode[1] || at[2] != radius_byte(kLabelRadius, b.negative)) {
            return false;
        }
    }
    return true;
}

bool patch_label_passes(std::uint8_t* draw, std::size_t drawSize, const LabelPassOptions& options) {
    const int radius = clamp_label_radius(options.radius);
    if (!options.anyZoom && radius == kLabelRadius) return true;
    if (!label_passes_match(draw, drawSize)) return false;

    DWORD previous = 0;
    if (!VirtualProtect(draw, drawSize, PAGE_EXECUTE_READWRITE, &previous)) return false;
    if (options.anyZoom) std::memcpy(draw + kZoomJump, kSixByteNop, sizeof(kSixByteNop));
    for (const RadiusByte& b : kRadiusBytes) draw[b.offset + 2] = radius_byte(radius, b.negative);
    VirtualProtect(draw, drawSize, previous, &previous);
    FlushInstructionCache(GetCurrentProcess(), draw, drawSize);
    return true;
}

}  // namespace cw
