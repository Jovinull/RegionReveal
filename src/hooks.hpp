#pragma once

#include <cstdint>

namespace rr {

// A 5-byte relative-jump detour.
//
// The five stolen bytes are copied verbatim into the trampoline, so a target is
// only safe when its first five bytes form whole, position-independent
// instructions. WorldMap::getCell starts with `push ebp / mov ebp, esp /
// push ebx / push esi`, which qualifies, and its byte signature pins those
// exact bytes before anything is written.
//
// The patch is not atomic. It is installed from DllMain while Cube.exe is
// still loading its imports, before the game has started a thread that could
// be executing those bytes.
class InlineHook {
public:
    bool install(std::uint8_t* target, void* detour);

    // Restores the original bytes. Only safe while no thread can be inside the
    // detour or the trampoline, i.e. straight after a failed install check.
    void remove();

    // Call this to reach the original function from inside the detour.
    template <typename Fn>
    Fn original() const {
        return reinterpret_cast<Fn>(trampoline_);
    }

    bool installed() const { return trampoline_ != nullptr; }

private:
    static constexpr int kStolen = 5;

    std::uint8_t* target_ = nullptr;
    std::uint8_t* trampoline_ = nullptr;
    std::uint8_t saved_[kStolen]{};
};

}  // namespace rr
