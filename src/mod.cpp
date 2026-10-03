#include <windows.h>

#include "region_reveal/log.hpp"
#include "region_reveal/reveal.hpp"

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        // Cube.exe imports this DLL, so this runs on the game's main thread
        // while its imports load, before its entry point. No game thread
        // exists yet to race the patch, and this is the thread that will later
        // draw the map. Setting up takes a few milliseconds of pattern
        // scanning and needs nothing beyond kernel32, so it happens here.
        rr::adopt_game_thread(GetCurrentThreadId());
        rr::log_line("RegionReveal loaded");
        rr::initialize();
    }
    // Always succeed: even when the mod stays inactive, the game still needs
    // the forwarded DirectInput8Create.
    return TRUE;
}
