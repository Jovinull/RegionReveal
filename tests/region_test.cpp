// Exercises the pure logic: region geometry, the survey radius and the
// visited-region file. None of it needs the game running.

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "../src/game/cube_world.hpp"
#include "../src/game/landmarks.hpp"
#include "../src/region_reveal/visited.hpp"

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    if (!ok) ++g_failures;
    std::printf("  %-4s %s\n", ok ? "ok" : "FAIL", what);
}

std::wstring visited_path(const char* world) {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\');
    *(slash + 1) = L'\0';
    std::wstring out(path);
    out += L"RegionReveal_";
    while (*world) out.push_back(*world++);
    out += L".visited";
    return out;
}

void write_raw(const char* world, const void* data, std::size_t size) {
    FILE* file = nullptr;
    _wfopen_s(&file, visited_path(world).c_str(), L"wb");
    if (!file) return;
    if (size) std::fwrite(data, 1, size, file);
    std::fclose(file);
}

void remove_file(const char* world) {
    DeleteFileW(visited_path(world).c_str());
}

int region_from_block(long long block) {
    return cw::region_of(static_cast<int>(block / cw::kBlocksPerCell));
}

// Counts how many of the whole world a set actually covers, by sweeping the
// window a survey could possibly touch rather than trusting the reported size.
int covered_around(const rr::VisitedRegions& v, int x, int y, int reach) {
    int total = 0;
    for (int dx = -reach; dx <= reach; ++dx) {
        for (int dy = -reach; dy <= reach; ++dy) {
            if (v.revealed(x + dx, y + dy)) ++total;
        }
    }
    return total;
}

void geometry() {
    std::printf("geometry\n");
    check(cw::kRegionCells == 8, "a region spans 8 cells");
    check(cw::kBlocksPerCell * cw::kRegionCells == 2048, "a region spans 2048 blocks");
    check(cw::kRegionDim == 8192, "the world is 8192 regions per axis");

    check(region_from_block(8402688) == 32823 / 8, "the observed cell boundary maps as logged");

    const long long boundary = 2048LL * 4100;
    check(region_from_block(boundary - 1) == 4099, "one block before a boundary is the low region");
    check(region_from_block(boundary) == 4100, "the boundary block starts the next region");
    check(region_from_block(boundary + 1) == 4100, "one block after stays in the new region");

    check(cw::region_of(32799) != cw::region_of(32800), "cells 32799/32800 differ");
    check(cw::region_of(32800) == cw::region_of(32807), "cells 32800..32807 share a region");
    check(cw::region_of(-1) == -1, "a negative cell floors rather than wraps");

    // Regression: a live session sat in region 4102, which an earlier bound of
    // 1024 rejected as out of range, leaving the mod inert.
    check(4102 < cw::kRegionDim, "a real observed region is inside the world bounds");
    check(cw::kRegionDim > cw::kGridDim, "regions are finer than storage chunks");
}

void survey_radius() {
    std::printf("survey radius\n");
    remove_file("survey");

    rr::VisitedRegions v;
    v.open("survey");
    v.visit(4102, 4100);

    check(v.centres() == 1, "one visit records one centre");
    check(v.covered() == 25, "and covers 5x5 regions");
    check(covered_around(v, 4102, 4100, 4) == 25, "counted by sweeping, still 25");

    check(v.revealed(4102, 4100), "the centre is revealed");
    check(v.revealed(4100, 4098) && v.revealed(4104, 4102), "so are opposite corners");
    check(!v.revealed(4105, 4100), "one region past the radius on x is not");
    check(!v.revealed(4102, 4103), "one region past the radius on y is not");
    check(!v.revealed(4105, 4103), "nor past it diagonally");
    remove_file("survey");
}

void overlap() {
    std::printf("overlapping surveys\n");
    remove_file("overlap");

    rr::VisitedRegions v;
    v.open("overlap");
    v.visit(100, 100);
    v.visit(101, 100);  // shifted by one, so the 5x5s share 20 regions

    check(v.centres() == 2, "two centres");
    check(v.covered() == 30, "union is 30, not 50: the overlap is not double counted");
    check(!v.visit(100, 100), "re-entering a known region reports no change");
    check(v.centres() == 2 && v.covered() == 30, "and changes neither set");
    remove_file("overlap");
}

void world_edges() {
    std::printf("world edges\n");
    remove_file("edge");

    rr::VisitedRegions v;
    v.open("edge");
    v.visit(0, 0);
    check(v.covered() == 9, "a corner surveys 3x3, the part that exists");
    check(!v.revealed(-1, 0) && !v.revealed(0, -1), "nothing negative is revealed");
    check(!v.revealed(cw::kRegionDim - 1, 0), "and nothing wraps to the far side");

    rr::VisitedRegions opposite;
    remove_file("edge2");
    opposite.open("edge2");
    opposite.visit(cw::kRegionDim - 1, cw::kRegionDim - 1);
    check(opposite.covered() == 9, "the opposite corner also surveys 3x3");
    check(!opposite.revealed(cw::kRegionDim, cw::kRegionDim), "past the last region is not revealed");
    check(!opposite.revealed(0, 0), "and it did not wrap to the origin");

    rr::VisitedRegions edge;
    remove_file("edge3");
    edge.open("edge3");
    edge.visit(0, 4000);
    check(edge.covered() == 15, "an x edge surveys 3x5");
    remove_file("edge");
    remove_file("edge2");
    remove_file("edge3");
}

void persistence_stores_centres_only() {
    std::printf("persistence stores centres only\n");
    remove_file("persist");

    {
        rr::VisitedRegions v;
        v.open("persist");
        v.visit(4102, 4100);
        v.visit(4103, 4100);
        v.flush();
    }

    // Two centres at four bytes each, plus the header and the world name.
    HANDLE file = CreateFileW(visited_path("persist").c_str(), GENERIC_READ, FILE_SHARE_READ,
                              nullptr, OPEN_EXISTING, 0, nullptr);
    const DWORD size = file == INVALID_HANDLE_VALUE ? 0 : GetFileSize(file, nullptr);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    check(size == 20 + 7 + 2 * 4, "the file holds two centres, not the covered regions");

    rr::VisitedRegions v;
    v.open("persist");
    check(v.centres() == 2, "both centres reload");
    check(v.covered() == 30, "and coverage is rebuilt from them");
    check(v.revealed(4104, 4102), "a region only reachable by survey is revealed again");
    remove_file("persist");
}

void world_isolation() {
    std::printf("world isolation\n");
    remove_file("one");
    remove_file("two");

    rr::VisitedRegions v;
    v.open("one");
    v.visit(4100, 4100);
    v.flush();

    v.open("two");
    check(v.centres() == 0 && v.covered() == 0, "a different world starts empty");
    check(!v.revealed(4100, 4100), "and inherits nothing");

    v.open("one");
    check(v.revealed(4100, 4100), "switching back restores the original");
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
    blob.append(131072, '\xff');
    write_raw("old", blob.data(), blob.size());

    rr::VisitedRegions v;
    v.open("old");
    check(v.centres() == 0, "a version 1 file is rejected, not converted");
    check(!v.revealed(4100, 4100), "and its all-ones body reveals nothing");
    remove_file("old");

    const char garbage[] = "not a visited file at all";
    write_raw("junk", garbage, sizeof(garbage));
    v.open("junk");
    check(v.centres() == 0, "garbage is rejected");
    remove_file("junk");

    write_raw("trunc", garbage, 4);
    v.open("trunc");
    check(v.centres() == 0, "a truncated file is rejected");
    remove_file("trunc");

    v.open("");
    check(v.world().empty(), "an empty world name closes the set");
    v.open("../escape");
    check(v.world().empty(), "a path-like world name is refused");
}

void landmark_table() {
    std::printf("landmark table\n");
    check(std::strcmp(cw::landmark_name(3), "Forest") == 0,
          "raw 3 is Forest, the one value with runtime evidence");
    check(std::strcmp(cw::landmark_name(0), "none") == 0, "raw 0 is no landmark");
    check(std::strcmp(cw::landmark_name(17), "Castle") == 0, "raw 17 is Castle");
    check(std::strcmp(cw::landmark_name(99), "unmapped") == 0, "an unknown raw says so");
    check(cw::landmark_kind(1) == cw::LandmarkKind::Settlement, "Village is a settlement");
    check(cw::landmark_kind(3) == cw::LandmarkKind::Natural, "Forest is natural");
    check(cw::landmark_kind(19) == cw::LandmarkKind::Adventure, "Catacombs is adventure");
}

}  // namespace

int main() {
    geometry();
    survey_radius();
    overlap();
    world_edges();
    persistence_stores_centres_only();
    world_isolation();
    fails_closed();
    landmark_table();

    std::printf("\n%s\n", g_failures ? "FAILURES" : "all region tests passed");
    return g_failures == 0 ? 0 : 1;
}
