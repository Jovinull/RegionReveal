// The dinput8.dll proxy: how RegionReveal gets into the game.
//
// Cube.exe imports exactly one function from dinput8.dll, and Windows searches
// the executable's own folder before the system directory. Dropping this DLL
// there loads the mod before the game's entry point, and every call is
// forwarded to the real dinput8.dll in the system directory.
//
// Nothing in the game folder is renamed, replaced or written to: one file is
// added, and deleting it restores the stock install.

#include <windows.h>
#include <unknwn.h>

#pragma comment(linker, "/EXPORT:DirectInput8Create=_DirectInput8Create@20")

namespace {

using DirectInput8CreateFn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);

INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;
DirectInput8CreateFn g_real_create = nullptr;

// Loaded on the first call rather than from DllMain, where loading another
// library is best avoided. It stays loaded for the life of the process.
BOOL CALLBACK load_real_dinput8(PINIT_ONCE, PVOID, PVOID*) {
    wchar_t path[MAX_PATH]{};
    const UINT length = GetSystemDirectoryW(path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH || wcscat_s(path, MAX_PATH, L"\\dinput8.dll") != 0) return TRUE;

    if (HMODULE real = LoadLibraryW(path)) {
        g_real_create = reinterpret_cast<DirectInput8CreateFn>(GetProcAddress(real, "DirectInput8Create"));
    }
    return TRUE;
}

}  // namespace

extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE instance, DWORD version, REFIID iid, LPVOID* out,
                                             LPUNKNOWN outer) {
    InitOnceExecuteOnce(&g_once, load_real_dinput8, nullptr, nullptr);
    if (!g_real_create) return E_FAIL;
    return g_real_create(instance, version, iid, out, outer);
}
