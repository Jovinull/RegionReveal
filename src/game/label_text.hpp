#pragma once

#include <cstddef>
#include <cstdint>

namespace cw {

// The map draws a landmark's name twice, an outline then the text itself, each
// through the same text-drawing call. Redirecting those two calls lets the mod
// change the text and its colour for one place at a time.

// MSVC 2012 std::wstring, as the game passes it.
struct GameWString {
    union {
        wchar_t buffer[8];
        wchar_t* pointer;
    };
    std::uint32_t size;
    std::uint32_t capacity;  // the text is in `buffer` while capacity < 8

    const wchar_t* chars() const { return capacity >= 8 ? pointer : buffer; }
};
static_assert(sizeof(GameWString) == 0x18, "MSVC 2012 wstring layout");

// Called for each of the two draws of a landmark name. `record` is the place
// record being labelled, `color` the RGBA the text is drawn in (the outline's
// for the first draw). Returns the text to draw: `text` itself, or another
// string that stays valid until the next call.
using LabelTextFn = const GameWString* (*)(const std::uint8_t* record, const GameWString* text, float* color,
                                           bool foreground);

// Whether the draw method holds the two calls and the stack slot the
// redirection relies on. Both supported builds do.
bool label_text_matches(const std::uint8_t* draw, std::size_t drawSize);

// Redirects the two calls. Must run while no other thread can be inside the
// draw method; see rr::ThreadFreeze.
bool hook_label_text(std::uint8_t* draw, std::size_t drawSize, LabelTextFn fn);

}  // namespace cw
