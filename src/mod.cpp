#include <windows.h>

#include "region_reveal/log.hpp"
#include "region_reveal/reveal.hpp"

namespace {

DWORD WINAPI start(LPVOID) {
    rr::log_line("RegionReveal loaded");
    rr::initialize();
    return 0;
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(module);
            // Signature scanning walks megabytes of .text, which is far more
            // than belongs under the loader lock.
            if (HANDLE thread = CreateThread(nullptr, 0, start, nullptr, 0, nullptr)) {
                CloseHandle(thread);
            }
            break;
        case DLL_PROCESS_DETACH:
            rr::shutdown();
            break;
        default:
            break;
    }
    return TRUE;
}
