#include "settings.hpp"

#include <windows.h>

#include <algorithm>

namespace rr {

PreviewSettings read_preview_settings(int maxRadius, int defaultRadius) {
    PreviewSettings out;
    out.radius = defaultRadius;

    wchar_t path[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) return out;
    wchar_t* slash = wcsrchr(path, L'\\');
    if (!slash) return out;
    wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"RegionReveal.ini");

    out.enabled = GetPrivateProfileIntW(L"preview", L"enabled", 1, path) != 0;
    const int radius = static_cast<int>(GetPrivateProfileIntW(L"preview", L"radius", defaultRadius, path));
    out.radius = std::min(maxRadius, std::max(0, radius));
    return out;
}

}  // namespace rr
