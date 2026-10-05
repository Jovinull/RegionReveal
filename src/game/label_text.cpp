#include "label_text.hpp"

#include <windows.h>

#include <cstring>

namespace cw {
namespace {

// Offsets from the start of the draw method, the same in both builds: the
// outline and the text draws of the landmark pass, each a `call rel32` to the
// game's text-drawing function, and an instruction that reads the place record
// from the stack slot both stubs read it from.
constexpr std::size_t kDrawSize = 6650;
constexpr std::uint16_t kOutlineCall = 0x1832;
constexpr std::uint16_t kTextCall = 0x191D;
constexpr std::uint16_t kRecordLoad = 0x15A2;
constexpr std::uint8_t kRecordLoadBytes[] = {0x8B, 0x85, 0xAC, 0xFC, 0xFF, 0xFF};  // mov eax, [ebp-0x354]

// The landmark pass's place test: a `call rel32` with the record in ecx and
// pointers to the cell centre's x and y pushed, and the start of the function
// it calls.
constexpr std::uint16_t kPlaceTestCall = 0xF6F;
constexpr std::uint8_t kPlaceTestSetup[] = {
    0x8D, 0x85, 0x4C, 0xFC, 0xFF, 0xFF, 0x50,  // lea eax, [ebp-0x3B4]; push eax  (y)
    0x8D, 0x85, 0x64, 0xFC, 0xFF, 0xFF, 0x50,  // lea eax, [ebp-0x39C]; push eax  (x)
    0x8B, 0xCE,                                // mov ecx, esi                    (the record)
    0x89, 0x95, 0x68, 0xFC, 0xFF, 0xFF,        // mov [ebp-0x398], edx
};
constexpr std::uint8_t kPlaceTestStart[] = {0x55, 0x8B, 0xEC, 0xFF, 0x75, 0x0C, 0xFF, 0x75, 0x08, 0xE8};

LabelTextFn g_fn = nullptr;
void* g_draw_text = nullptr;  // the game's text-drawing function

const std::uint8_t* call_target(const std::uint8_t* call) {
    std::int32_t rel = 0;
    std::memcpy(&rel, call + 1, sizeof(rel));
    return call + 5 + rel;
}

const GameWString* __cdecl dispatch(const std::uint8_t* record, const GameWString* text, float* color,
                                    int foreground) {
    return g_fn ? g_fn(record, text, color, foreground != 0) : text;
}

// Entered in place of the text-drawing call, still inside the draw method's
// frame: ecx is the text renderer, [esp+8] the text, [esp+0x24] the colour,
// and the draw method's ebp gives the place record at [ebp-0x354]. Swaps the
// text argument for whatever dispatch returns and carries on into the game's
// function, which returns straight to the draw method.
__declspec(naked) void outline_stub() {
    __asm {
        push ecx
        push 0
        push dword ptr [esp + 0x2C]
        push dword ptr [esp + 0x14]
        push dword ptr [ebp - 0x354]
        call dispatch
        add esp, 16
        pop ecx
        mov dword ptr [esp + 8], eax
        jmp dword ptr [g_draw_text]
    }
}

__declspec(naked) void text_stub() {
    __asm {
        push ecx
        push 1
        push dword ptr [esp + 0x2C]
        push dword ptr [esp + 0x14]
        push dword ptr [ebp - 0x354]
        call dispatch
        add esp, 16
        pop ecx
        mov dword ptr [esp + 8], eax
        jmp dword ptr [g_draw_text]
    }
}

void write_call(std::uint8_t* call, const void* to) {
    const std::int32_t rel = static_cast<std::int32_t>(static_cast<const std::uint8_t*>(to) - (call + 5));
    std::memcpy(call + 1, &rel, sizeof(rel));
}

}  // namespace

PlaceTestFn place_test_of(const std::uint8_t* draw, std::size_t drawSize) {
    if (drawSize != kDrawSize || draw[kPlaceTestCall] != 0xE8) return nullptr;
    const std::uint8_t* setup = draw + kPlaceTestCall - sizeof(kPlaceTestSetup);
    if (std::memcmp(setup, kPlaceTestSetup, sizeof(kPlaceTestSetup)) != 0) return nullptr;
    const std::uint8_t* test = call_target(draw + kPlaceTestCall);
    if (std::memcmp(test, kPlaceTestStart, sizeof(kPlaceTestStart)) != 0) return nullptr;
    return reinterpret_cast<PlaceTestFn>(const_cast<std::uint8_t*>(test));
}

bool label_text_matches(const std::uint8_t* draw, std::size_t drawSize) {
    if (drawSize != kDrawSize) return false;
    if (draw[kOutlineCall] != 0xE8 || draw[kTextCall] != 0xE8) return false;
    if (call_target(draw + kOutlineCall) != call_target(draw + kTextCall)) return false;
    if (std::memcmp(draw + kRecordLoad, kRecordLoadBytes, sizeof(kRecordLoadBytes)) != 0) return false;
    return place_test_of(draw, drawSize) != nullptr;
}

bool hook_label_text(std::uint8_t* draw, std::size_t drawSize, LabelTextFn fn) {
    if (!fn || !label_text_matches(draw, drawSize)) return false;

    g_fn = fn;
    g_draw_text = const_cast<std::uint8_t*>(call_target(draw + kTextCall));

    DWORD previous = 0;
    if (!VirtualProtect(draw, drawSize, PAGE_EXECUTE_READWRITE, &previous)) return false;
    write_call(draw + kOutlineCall, reinterpret_cast<const void*>(&outline_stub));
    write_call(draw + kTextCall, reinterpret_cast<const void*>(&text_stub));
    VirtualProtect(draw, drawSize, previous, &previous);
    FlushInstructionCache(GetCurrentProcess(), draw, drawSize);
    return true;
}

}  // namespace cw
