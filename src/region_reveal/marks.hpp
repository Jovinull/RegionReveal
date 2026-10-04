#pragma once

#include <cstdint>

#include "../game/label_text.hpp"
#include "../game/session.hpp"

namespace rr {

// What a landmark's name says about the player's progress there.
enum class PlaceMark : std::uint8_t {
    None,
    Visited,       // the game itself has revealed the place: the player was there
    BossDefeated,  // the place's boss mission is done
};

// The cell of a place record's origin. Invalid if it is outside the map.
cw::Cell place_origin(const std::uint8_t* record);

PlaceMark place_mark(const std::uint8_t* record, bool originRevealed);

// Recolours the name: green for a defeated boss, the game's own colour
// otherwise.
void apply_mark_color(PlaceMark mark, float* rgba);

// The name with its mark appended - " •" visited, " †" boss defeated, both
// glyphs of the game's map font. The result lives in this object and is
// rebuilt by every call.
class MarkedText {
public:
    const cw::GameWString* apply(const cw::GameWString* text, PlaceMark mark);

private:
    static constexpr std::uint32_t kCapacity = 127;
    wchar_t chars_[kCapacity + 1] = {};
    cw::GameWString string_{};
};

}  // namespace rr
