#pragma once

#include <cstdint>

#include "../game/label_text.hpp"
#include "../game/session.hpp"

namespace rr {

// What a landmark's name says about the player's progress there.
enum class PlaceMark : std::uint8_t {
    None,
    Visited,       // the game itself would show the name: the player was there
    BossDefeated,  // the place's boss mission is done
};

// The cell of a place record's origin. Invalid if it is outside the map.
cw::Cell place_origin(const std::uint8_t* record);

// The position of a cell's centre along one axis, as the landmark pass hands
// it to the place test.
std::int64_t cell_centre(int cell);

// Answers one question about one cell for place_seen.
using PlaceCellFn = bool (*)(void* context, const std::uint8_t* record, int x, int y);

// Whether the game itself shows this place's name, as it does once the player
// has been there: some revealed cell of the place's 8 x 8 block lies inside the
// place - the landmark pass's own condition.
bool place_seen(const std::uint8_t* record, PlaceCellFn revealed, PlaceCellFn inside, void* context);

PlaceMark place_mark(const std::uint8_t* record, bool seen);

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
