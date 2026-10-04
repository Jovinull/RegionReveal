#pragma once

#include "../game/label_passes.hpp"

namespace rr {

// Optional RegionReveal.ini beside the game, read once at start-up:
//
//   [labels]
//   any_zoom=1  ; 0: points of interest only when zoomed in, as in the game
//   wide=1      ; 0: labels within 32 cells of the map's centre, as in the game
//
// A missing file or key means 1.
cw::LabelPassOptions read_label_options();

}  // namespace rr
