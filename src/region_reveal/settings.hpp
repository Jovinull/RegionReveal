#pragma once

#include "../game/label_passes.hpp"

namespace rr {

struct Options {
    cw::LabelPassOptions labels;
    // Mark landmark names: " •" once you have been there, green with " †"
    // once the place's boss is defeated.
    bool marks = true;
    // Show city districts when zoomed out. Off by default: around a city they
    // pile up on top of its name, and the game itself only shows them zoomed in.
    bool farDistricts = false;
};

// Optional RegionReveal.ini beside the game, read once at start-up:
//
//   [labels]
//   any_zoom=1       ; 0: points of interest only when zoomed in, as in the game
//   range=96         ; cells each way from the map's centre, 32 (the game's) to 127
//   marks=1          ; 0: no visited / boss-defeated marks on landmark names
//   far_districts=0  ; 1: city districts at every zoom too
//
// A missing file or key keeps the default shown.
Options read_options();

}  // namespace rr
