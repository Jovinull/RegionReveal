#include "hooks.hpp"

#include <windows.h>

#include <cstring>

namespace rr {
namespace {

constexpr std::uint8_t kJmpRel32 = 0xE9;

void write_jump(std::uint8_t* at, const void* to) {
    at[0] = kJmpRel32;
    const auto delta = reinterpret_cast<std::uint8_t*>(const_cast<void*>(to)) - (at + 5);
    std::memcpy(at + 1, &delta, sizeof(delta));
}

}  // namespace

bool InlineHook::install(std::uint8_t* target, void* detour) {
    if (installed() || !target || !detour) return false;

    auto* trampoline = static_cast<std::uint8_t*>(
        VirtualAlloc(nullptr, kStolen + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!trampoline) return false;

    std::memcpy(trampoline, target, kStolen);
    write_jump(trampoline + kStolen, target + kStolen);

    DWORD previous = 0;
    if (!VirtualProtect(target, kStolen, PAGE_EXECUTE_READWRITE, &previous)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }
    std::memcpy(saved_, target, kStolen);
    write_jump(target, detour);
    VirtualProtect(target, kStolen, previous, &previous);
    FlushInstructionCache(GetCurrentProcess(), target, kStolen);

    target_ = target;
    trampoline_ = trampoline;
    return true;
}

void InlineHook::remove() {
    if (!installed()) return;

    DWORD previous = 0;
    if (VirtualProtect(target_, kStolen, PAGE_EXECUTE_READWRITE, &previous)) {
        std::memcpy(target_, saved_, kStolen);
        VirtualProtect(target_, kStolen, previous, &previous);
        FlushInstructionCache(GetCurrentProcess(), target_, kStolen);
    }
    VirtualFree(trampoline_, 0, MEM_RELEASE);
    trampoline_ = nullptr;
    target_ = nullptr;
}

}  // namespace rr
