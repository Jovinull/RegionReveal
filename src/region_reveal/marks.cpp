#include "marks.hpp"

#include <cstring>

#include "../game/cube_world.hpp"

namespace rr {
namespace {

constexpr wchar_t kVisitedSuffix[] = L" •";       // a bullet
constexpr wchar_t kBossDefeatedSuffix[] = L" †";  // a dagger

int position_to_cell(std::int64_t position) {
    return static_cast<int>(position / cw::kPosUnitsPerBlock / cw::kBlocksPerCell);
}

}  // namespace

cw::Cell place_origin(const std::uint8_t* record) {
    std::int64_t x = 0;
    std::int64_t y = 0;
    std::memcpy(&x, record + cw::kPlaceOriginX, sizeof(x));
    std::memcpy(&y, record + cw::kPlaceOriginY, sizeof(y));
    const cw::Cell cell{position_to_cell(x), position_to_cell(y)};
    return cell.valid() ? cell : cw::Cell{};
}

PlaceMark place_mark(const std::uint8_t* record, bool originRevealed) {
    std::uint32_t mission = 0;
    std::memcpy(&mission, record + cw::kPlaceMission, sizeof(mission));
    if (mission != 0 && record[cw::kPlaceMissionState] == cw::kMissionDone) return PlaceMark::BossDefeated;
    return originRevealed ? PlaceMark::Visited : PlaceMark::None;
}

void apply_mark_color(PlaceMark mark, float* rgba) {
    if (mark != PlaceMark::BossDefeated) return;
    rgba[0] = 0.45f;
    rgba[1] = 1.0f;
    rgba[2] = 0.45f;
}

const cw::GameWString* MarkedText::apply(const cw::GameWString* text, PlaceMark mark) {
    if (mark == PlaceMark::None || !text) return text;
    const wchar_t* suffix = mark == PlaceMark::BossDefeated ? kBossDefeatedSuffix : kVisitedSuffix;
    const std::uint32_t suffixSize = static_cast<std::uint32_t>(wcslen(suffix));
    if (text->size > text->capacity || text->size + suffixSize > kCapacity) return text;

    std::memcpy(chars_, text->chars(), text->size * sizeof(wchar_t));
    std::memcpy(chars_ + text->size, suffix, (suffixSize + 1) * sizeof(wchar_t));
    string_.pointer = chars_;
    string_.size = text->size + suffixSize;
    string_.capacity = kCapacity;  // >= 8: the game reads the text through `pointer`
    return &string_;
}

}  // namespace rr
