#include <windows.h>

#include "region_reveal/log.hpp"
#include "region_reveal/reveal.hpp"
#include "threads.hpp"

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        // As dinput8.dll this runs on the game's main thread while its imports
        // load, before the entry point; injected by a mod loader it runs on the
        // loader's thread instead. Either way the main thread is the one that
        // will draw the map, and the oldest thread of the process. Setting up
        // takes a few milliseconds of pattern scanning and needs nothing beyond
        // kernel32, so it happens here.
        rr::adopt_game_thread(rr::main_thread_id());
        rr::log_line("RegionReveal loaded");
        rr::initialize();
    }
    // Always succeed: as dinput8.dll the game still needs the forwarded
    // DirectInput8Create even when the mod stays inactive.
    return TRUE;
}
