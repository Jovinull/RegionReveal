#include "hooks.hpp"

#include <windows.h>

#include <cstring>

namespace rr {
namespace {

constexpr std::uint8_t kJmpRel32 = 0xE9;
constexpr std::size_t kJumpSize = 5;

void write_jump(std::uint8_t* at, const void* to) {
    at[0] = kJmpRel32;
    const std::int32_t delta = static_cast<std::int32_t>(static_cast<const std::uint8_t*>(to) - (at + kJumpSize));
    std::memcpy(at + 1, &delta, sizeof(delta));
}

}  // namespace

bool InlineHook::install(std::uint8_t* target, void* detour) {
    if (installed() || !target || !detour) return false;

    // The trampoline: the stolen bytes, then a jump back past them. Written
    // while writable, then made execute-only.
    const std::size_t size = kStolen + kJumpSize;
    auto* trampoline = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!trampoline) return false;
    std::memcpy(trampoline, target, kStolen);
    write_jump(trampoline + kStolen, target + kStolen);
    DWORD previous = 0;
    if (!VirtualProtect(trampoline, size, PAGE_EXECUTE_READ, &previous)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), trampoline, size);

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

    // If the bytes cannot be restored the hook stays as it is, trampoline
    // included: the patched function still jumps through the detour into it.
    DWORD previous = 0;
    if (!VirtualProtect(target_, kStolen, PAGE_EXECUTE_READWRITE, &previous)) return;
    std::memcpy(target_, saved_, kStolen);
    VirtualProtect(target_, kStolen, previous, &previous);
    FlushInstructionCache(GetCurrentProcess(), target_, kStolen);

    VirtualFree(trampoline_, 0, MEM_RELEASE);
    trampoline_ = nullptr;
    target_ = nullptr;
}

}  // namespace rr
