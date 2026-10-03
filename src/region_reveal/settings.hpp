#pragma once

namespace rr {

// Optional RegionReveal.ini beside the game, re-read while the game runs:
//
//   [preview]
//   enabled=1   ; 0 frees every terrain preview and builds no more
//   radius=6    ; cells around the map centre that get a preview, 0..9
//
// A missing file or key keeps the default. Previews cost memory in a 32-bit
// process the game already runs close to its limit, so this is the knob for a
// machine where that matters.
struct PreviewSettings {
    bool enabled = true;
    int radius = 6;
};

PreviewSettings read_preview_settings(int maxRadius, int defaultRadius);

}  // namespace rr
