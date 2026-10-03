#include "paths.hpp"

#include <windows.h>

namespace rr {

std::wstring beside_game(const std::wstring& name) {
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return {};

    std::wstring folder(path, length);
    const std::size_t slash = folder.find_last_of(L'\\');
    if (slash == std::wstring::npos) return {};
    folder.resize(slash + 1);
    return folder + name;
}

}  // namespace rr
