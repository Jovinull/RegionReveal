#include <windows.h>

#include "region_reveal/log.hpp"
#include "region_reveal/reveal.hpp"

#ifdef REGIONREVEAL_PROXY_DINPUT8
namespace rr {
bool proxy_attach();
void proxy_detach();
}  // namespace rr
#endif

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
#ifdef REGIONREVEAL_PROXY_DINPUT8
            // Forwarding has to be ready before the game's first call; failing
            // here would leave the game without input, so refuse to load.
            if (!rr::proxy_attach()) return FALSE;
#endif
            // Signature scanning walks megabytes of .text, which is far more
            // than belongs under the loader lock.
            if (HANDLE thread = CreateThread(nullptr, 0, start, nullptr, 0, nullptr)) {
                CloseHandle(thread);
            }
            break;
        case DLL_PROCESS_DETACH:
            rr::shutdown();
#ifdef REGIONREVEAL_PROXY_DINPUT8
            rr::proxy_detach();
#endif
            break;
        default:
            break;
    }
    return TRUE;
}
