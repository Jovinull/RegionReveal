#include "settings.hpp"

#include <windows.h>

#include <string>

#include "paths.hpp"

namespace rr {

cw::LabelPassOptions read_label_options() {
    cw::LabelPassOptions options;
    const std::wstring path = beside_game(L"RegionReveal.ini");
    if (path.empty()) return options;
    options.anyZoom = GetPrivateProfileIntW(L"labels", L"any_zoom", 1, path.c_str()) != 0;
    options.wideRange = GetPrivateProfileIntW(L"labels", L"wide", 1, path.c_str()) != 0;
    return options;
}

}  // namespace rr
