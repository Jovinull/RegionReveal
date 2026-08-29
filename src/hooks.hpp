#pragma once

#include <cstdint>

namespace rr {

// A 5-byte relative-jump detour.
//
// The five stolen bytes are copied verbatim into the trampoline, so a target is
// only safe when its first five bytes form whole, position-independent
// instructions. Both hooked functions start with `push ebp / mov ebp, esp /
// push reg / push reg`, which satisfies that, and the byte signature used to
// find them re-checks those exact bytes before anything is written.
class InlineHook {
public:
    ~InlineHook() { remove(); }

    bool install(std::uint8_t* target, void* detour);
    void remove();

    // Call this to reach the original function from inside a detour.
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
