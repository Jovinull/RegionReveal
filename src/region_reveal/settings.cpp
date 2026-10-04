#include "settings.hpp"

#include <windows.h>

#include <string>

#include "paths.hpp"

namespace rr {

Options read_options() {
    Options options;
    const std::wstring path = beside_game(L"RegionReveal.ini");
    if (path.empty()) return options;
    const wchar_t* file = path.c_str();
    options.labels.anyZoom = GetPrivateProfileIntW(L"labels", L"any_zoom", 1, file) != 0;
    options.labels.radius = cw::clamp_label_radius(
        static_cast<int>(GetPrivateProfileIntW(L"labels", L"range", cw::kDefaultLabelRadius, file)));
    options.marks = GetPrivateProfileIntW(L"labels", L"marks", 1, file) != 0;
    options.farDistricts = GetPrivateProfileIntW(L"labels", L"far_districts", 0, file) != 0;
    return options;
}

}  // namespace rr
