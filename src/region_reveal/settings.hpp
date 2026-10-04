#pragma once

#include "../game/label_passes.hpp"

namespace rr {

// Optional RegionReveal.ini beside the game, read once at start-up:
//
//   [labels]
//   any_zoom=1  ; 0: points of interest only when zoomed in, as in the game
//   range=96    ; cells each way from the map's centre, 32 (the game's) to 127
//
// A missing file or key keeps the default shown.
cw::LabelPassOptions read_label_options();

}  // namespace rr
