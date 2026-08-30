#pragma once

namespace cw {

// The landmark a gameplay region carries, read from its 0x68 record at +0x18.
//
// The names come from a table of wide strings at 0x71C54C..0x71C6DC, all of them
// referenced from one generator function spanning 0x592A5A..0x5931F5 that pairs
// a procedurally built word with a type word. The indices below are that
// function's registration order, offset by one because the renderer treats 0 as
// "no landmark" (`test eax, eax / je skip` at 0x4CA54B).
//
// Evidence, per value:
//
//   CONFIRMED by counting a screenshot of the map against the same world's save,
//   over a 30-region survey:
//     1  two regions held raw 1; the map drew exactly two, both labelled CITY.
//        The string table's word at that index is "Village", so either the
//        display word differs from the table word or the index is off by one
//        somewhere. Named City here because that is what the game printed.
//     2  three regions, three MOUNTAINS labels.
//     3  two regions, two FOREST labels - and a live probe read 3 while the
//        player stood in one, which is the strongest single anchor.
//     4  one region, one LAKE label.
//
//   STRONG EVIDENCE: 0 (the renderer skips it) and 10 (the renderer skips it
//   explicitly, `cmp eax, 0xA / je`).
//
//   UNKNOWN: everything else. Registration order suggests the words below, but
//   the same screenshot showed six TEMPLE labels where the survey held eleven
//   raw-14 regions, so the adventure end of the table does not line up and is
//   not to be trusted. Values 8, 9, 13 and 16+ have never been observed in a
//   saved world at all.
inline const char* landmark_name(unsigned raw) {
    switch (raw) {
        case 0: return "none";
        case 1: return "City";
        case 2: return "Mountain";
        case 3: return "Forest";
        case 4: return "Lake";
        case 5: return "Canyon";
        case 6: return "Rock";
        case 7: return "Tree";
        case 8: return "Valley";
        case 9: return "Crater";
        case 10: return "Peak";
        case 11: return "Island";
        // Below here the registration order stopped matching what the map drew,
        // so the words are withheld rather than printed as if they were known.
        case 12: case 13: case 14: case 15: case 16: case 17: case 18:
        case 19: case 20: case 21: case 22: case 23: case 24:
            return "adventure?";
        default: return "unmapped";
    }
}

// Grouping for diagnostics only - nothing filters on it, and the game itself
// draws all of them through the same marker pass.
enum class LandmarkKind { None, Settlement, Natural, Adventure, Unmapped };

inline LandmarkKind landmark_kind(unsigned raw) {
    switch (raw) {
        case 0: return LandmarkKind::None;
        case 1: return LandmarkKind::Settlement;
        case 2: case 3: case 4: case 5: case 6:
        case 7: case 8: case 9: case 10: case 11: return LandmarkKind::Natural;
        case 12: case 13: case 14: case 15: case 16: case 17: case 18:
        case 19: case 20: case 21: case 22: case 23: case 24:
            return LandmarkKind::Adventure;
        default: return LandmarkKind::Unmapped;
    }
}

inline const char* landmark_kind_name(LandmarkKind kind) {
    switch (kind) {
        case LandmarkKind::Settlement: return "settlement";
        case LandmarkKind::Natural: return "natural";
        case LandmarkKind::Adventure: return "adventure";
        case LandmarkKind::None: return "none";
        default: return "unmapped";
    }
}

}  // namespace cw
