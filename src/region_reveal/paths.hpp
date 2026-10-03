#pragma once

#include <string>

namespace rr {

// `name` in the folder Cube.exe was started from, where the mod keeps its log
// and its per-world files. Empty if the folder cannot be determined.
std::wstring beside_game(const std::wstring& name);

}  // namespace rr
