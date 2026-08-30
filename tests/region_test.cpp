// Exercises the pure logic: region geometry and the visited-region file.
//
// None of this needs the game, so none of it should wait on a play session.

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "../src/game/cube_world.hpp"
#include "../src/region_reveal/visited.hpp"

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    if (!ok) ++g_failures;
    std::printf("  %-4s %s\n", ok ? "ok" : "FAIL", what);
}

std::wstring visited_path(const char* world, const wchar_t* suffix) {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\');
    *(slash + 1) = L'\0';
    std::wstring out(path);
    out += L"RegionReveal_";
    while (*world) out.push_back(*world++);
    out += suffix;
    return out;
}

void write_raw(const char* world, const void* data, std::size_t size) {
    FILE* file = nullptr;
    _wfopen_s(&file, visited_path(world, L".visited").c_str(), L"wb");
    if (!file) return;
    if (size) std::fwrite(data, 1, size, file);
    std::fclose(file);
}

void remove_file(const char* world) {
    DeleteFileW(visited_path(world, L".visited").c_str());
}

// A region is 8 cells of 256 blocks, so 2048 blocks per axis.
int region_from_block(long long block) {
    return cw::region_of(static_cast<int>(block / cw::kBlocksPerCell));
}

void geometry() {
    std::printf("geometry\n");
    check(cw::kRegionCells == 8, "a region spans 8 cells");
    check(cw::kBlocksPerCell * cw::kRegionCells == 2048, "a region spans 2048 blocks");
    check(cw::kRegionDim == 8192, "the world is 8192 regions per axis");

    // The boundary the live session crossed: block 8402688 is cell 32823.
    check(region_from_block(8402688) == 32823 / 8, "known cell boundary maps as observed");

    const long long boundary = 2048LL * 4100;
    check(region_from_block(boundary - 1) == 4099, "one block before a boundary is the low region");
    check(region_from_block(boundary) == 4100, "the boundary block starts the next region");
    check(region_from_block(boundary + 1) == 4100, "one block after stays in the new region");

    check(cw::region_of(32799) != cw::region_of(32800), "cells 32799/32800 are different regions");
    check(cw::region_of(32800) == cw::region_of(32807), "cells 32800..32807 share a region");

    // Coordinates are unsigned in practice - the world is centred at 32768 cells
    // - but the shift must still behave for a negative cell rather than wrap.
    check(cw::region_of(-1) == -1, "a negative cell floors rather than wraps");

    // Regression: a live session sat in region 4102, which an earlier bound of
    // 1024 rejected as out of range, leaving the mod inert.
    check(4102 < cw::kRegionDim, "a real observed region is inside the world bounds");
    check(cw::kRegionDim > cw::kGridDim, "regions are finer than storage chunks");
}

void storage_round_trip() {
    std::printf("storage round trip\n");
    remove_file("alpha");

    {
        rr::VisitedRegions v;
        v.open("alpha");
        check(v.size() == 0, "a fresh world starts empty");
        check(v.add(4100, 4100), "adding a region reports it as new");
        check(!v.add(4100, 4100), "adding it twice reports no change");
        check(v.add(4099, 4100), "an adjacent region is separate");
        check(v.contains(4100, 4100) && v.contains(4099, 4100), "both are present");
        check(!v.contains(4101, 4100), "an unvisited neighbour is absent");
        v.flush();
    }
    {
        rr::VisitedRegions v;
        v.open("alpha");
        check(v.size() == 2, "both regions survive a reload");
        check(v.contains(4100, 4100) && v.contains(4099, 4100), "the same two, specifically");
        check(!v.contains(4101, 4100), "and nothing else appeared");
    }
    remove_file("alpha");
}

void world_isolation() {
    std::printf("world isolation\n");
    remove_file("one");
    remove_file("two");

    rr::VisitedRegions v;
    v.open("one");
    v.add(4100, 4100);
    v.flush();

    v.open("two");
    check(v.size() == 0, "a different world starts empty");
    check(!v.contains(4100, 4100), "it does not inherit the first world's region");

    v.open("one");
    check(v.contains(4100, 4100), "switching back restores the original");
    remove_file("one");
    remove_file("two");
}

void fails_closed() {
    std::printf("fails closed\n");

    struct V1Header {
        char magic[4];
        std::uint32_t version, gridDim, bitsBytes, worldLength;
    } v1{{'R', 'R', 'V', 'S'}, 1, 1024, 131072, 3};
    std::string blob(reinterpret_cast<const char*>(&v1), sizeof(v1));
    blob += "old";
    blob.append(131072, '\xff');  // v1 said "every region visited"
    write_raw("old", blob.data(), blob.size());

    rr::VisitedRegions v;
    v.open("old");
    check(v.size() == 0, "a version 1 file is rejected, not converted");
    check(!v.contains(4100, 4100), "and its all-ones body reveals nothing");
    remove_file("old");

    const char garbage[] = "not a visited file at all";
    write_raw("junk", garbage, sizeof(garbage));
    v.open("junk");
    check(v.size() == 0, "garbage is rejected");
    remove_file("junk");

    write_raw("trunc", garbage, 4);
    v.open("trunc");
    check(v.size() == 0, "a truncated file is rejected");
    remove_file("trunc");

    v.open("");
    check(v.world().empty(), "an empty world name closes the set");
    v.open("../escape");
    check(v.world().empty(), "a path-like world name is refused");
}

}  // namespace

int main() {
    geometry();
    storage_round_trip();
    world_isolation();
    fails_closed();

    std::printf("\n%s\n", g_failures ? "FAILURES" : "all region tests passed");
    return g_failures == 0 ? 0 : 1;
}
