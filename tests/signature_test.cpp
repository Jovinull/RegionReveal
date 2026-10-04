// Checks the shipped byte signatures against a Cube.exe on disk.
//
// This exercises the one part of the mod that can be verified without running
// the game: that each pattern resolves to exactly one address, in every build
// we claim to support, and that it is the address recorded in
// docs/TARGET_BUILD.md.
//
//   signature_test <Cube.exe> <RVA> ...
//
// One hex RVA per signature, in the order of kSignatures below.
// tests/run_tests.py supplies them for both builds.

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "../src/game/label_passes.hpp"
#include "../src/game/label_text.hpp"
#include "../src/game/signatures.hpp"

namespace {

// The mod scans the loaded image; here the file is read raw, so the sections
// are laid out by hand to map file offsets onto RVAs.
bool load_text(const char* path, std::vector<std::uint8_t>* image, std::uint32_t* text_rva,
               std::uint32_t* text_size, std::uint32_t* text_off) {
    FILE* file = nullptr;
    if (fopen_s(&file, path, "rb") != 0 || !file) return false;

    std::fseek(file, 0, SEEK_END);
    image->resize(static_cast<std::size_t>(std::ftell(file)));
    std::fseek(file, 0, SEEK_SET);
    const std::size_t read = std::fread(image->data(), 1, image->size(), file);
    std::fclose(file);
    if (read != image->size()) return false;

    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image->data());
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(image->data() + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

    const auto* section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if (std::memcmp(section->Name, ".text", 5) != 0) continue;
        *text_rva = section->VirtualAddress;
        *text_size = section->SizeOfRawData;
        *text_off = section->PointerToRawData;
        return true;
    }
    return false;
}

int failures = 0;

void check(const char* name, std::uint8_t* found, std::uint8_t* base, std::uint32_t text_rva,
           std::uint32_t text_off, std::uint32_t expected_rva) {
    if (!found) {
        std::printf("  FAIL %-22s no unique match\n", name);
        ++failures;
        return;
    }
    const std::uint32_t rva =
        static_cast<std::uint32_t>(found - base) - text_off + text_rva;
    if (rva != expected_rva) {
        std::printf("  FAIL %-22s at RVA 0x%06X, expected 0x%06X\n", name, rva, expected_rva);
        ++failures;
        return;
    }
    std::printf("  ok   %-22s RVA 0x%06X\n", name, rva);
}

struct Signature {
    const char* name;
    const char* pattern;
};

const Signature kSignatures[] = {
    {"WorldMap::getCell", cw::kSigWorldMapGetCell},
    {"MapOverlayWidget::draw", cw::kSigMapOverlayDraw},
    {"World area lookup", cw::kSigWorldAreaAt},
};
constexpr int kCount = static_cast<int>(sizeof(kSignatures) / sizeof(kSignatures[0]));

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2 + kCount) {
        std::fprintf(stderr, "usage: signature_test <Cube.exe> <%d RVAs>\n", kCount);
        return 2;
    }

    std::vector<std::uint8_t> image;
    std::uint32_t text_rva = 0, text_size = 0, text_off = 0;
    if (!load_text(argv[1], &image, &text_rva, &text_size, &text_off)) {
        std::fprintf(stderr, "could not read .text from %s\n", argv[1]);
        return 2;
    }

    cw::ModuleRange range{image.data() + text_off, image.data() + text_off + text_size};
    std::printf("%s\n", argv[1]);

    for (int i = 0; i < kCount; ++i) {
        check(kSignatures[i].name, cw::find_unique(range, kSignatures[i].pattern), image.data(), text_rva,
              text_off, std::strtoul(argv[2 + i], nullptr, 16));
    }

    // The optional label-pass changes rewrite bytes inside the draw method;
    // they must be exactly the expected ones.
    if (std::uint8_t* draw = cw::find_unique(range, cw::kSigMapOverlayDraw)) {
        std::uint8_t* end = cw::function_end(draw, range.text_end);
        const bool ok = end && cw::label_passes_match(draw, static_cast<std::size_t>(end - draw));
        std::printf("  %s label passes (zoom gate and 11 range bytes)\n", ok ? "ok  " : "FAIL");
        if (!ok) ++failures;
        const bool text = end && cw::label_text_matches(draw, static_cast<std::size_t>(end - draw));
        std::printf("  %s landmark name draws (two calls and the record slot)\n", text ? "ok  " : "FAIL");
        if (!text) ++failures;
    }

    return failures == 0 ? 0 : 1;
}
