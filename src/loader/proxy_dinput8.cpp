// dinput8.dll proxy: the loading path that needs no injector.
//
// Cube.exe imports exactly one function from dinput8.dll, and the loader
// searches the executable's own folder first. Dropping this DLL there gets
// RegionReveal loaded before the game's entry point runs, while every call is
// forwarded to the real dinput8 in the system directory.
//
// Nothing in the game folder is renamed, replaced or written to - one file is
// added, and deleting it restores the stock install. It also avoids
// CreateRemoteThread, which antivirus heuristics reject on sight.

#include <windows.h>

#pragma comment(linker, "/EXPORT:DirectInput8Create=_DirectInput8Create@20")

namespace {

using DirectInput8CreateFn = HRESULT(WINAPI*)(HINSTANCE, DWORD, const IID&, void**, IUnknown*);

HMODULE g_system_dinput8 = nullptr;
DirectInput8CreateFn g_real_create = nullptr;

}  // namespace

namespace rr {

bool proxy_attach() {
    wchar_t path[MAX_PATH]{};
    const UINT length = GetSystemDirectoryW(path, MAX_PATH);
    if (length == 0 || length > MAX_PATH - 16) return false;

    if (wcscat_s(path, MAX_PATH, L"\\dinput8.dll") != 0) return false;

    g_system_dinput8 = LoadLibraryW(path);
    if (!g_system_dinput8) return false;

    g_real_create = reinterpret_cast<DirectInput8CreateFn>(
        GetProcAddress(g_system_dinput8, "DirectInput8Create"));
    return g_real_create != nullptr;
}

void proxy_detach() {
    if (g_system_dinput8) {
        FreeLibrary(g_system_dinput8);
        g_system_dinput8 = nullptr;
        g_real_create = nullptr;
    }
}

}  // namespace rr

extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE instance, DWORD version, const IID& iid,
                                             void** out, IUnknown* outer) {
    if (!g_real_create) return E_FAIL;
    return g_real_create(instance, version, iid, out, outer);
}
