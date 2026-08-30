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
// STRONG EVIDENCE for the table as a whole; CONFIRMED for 3 = Forest, where a
// live probe read 3 from the record while the player reported standing in a
// forest. The rest is inference from registration order and has not been
// checked against a second observation.
//
// Ruins appears at several consecutive indices, which most likely means the game
// draws several ruin variants under one word. That is not established, so the
// duplicates are listed as they occur rather than merged.
inline const char* landmark_name(unsigned raw) {
    switch (raw) {
        case 0: return "none";
        case 1: return "Village";
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
        case 12: case 13: case 14: case 15: return "Ruins";
        case 16: return "Gravesite";
        case 17: return "Castle";
        case 18: return "Ruins";
        case 19: return "Catacombs";
        case 20: return "Palace";
        case 21: return "Temple";
        case 22: return "Pyramid";
        case 23: return "Cave";
        case 24: return "Portal";
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
