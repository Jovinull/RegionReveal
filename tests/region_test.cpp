// Exercises the pure logic: region geometry, the visited-area file and the
// terrain preview synthesis. None of it needs the game running.

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "../src/game/cube_world.hpp"
#include "../src/game/landmarks.hpp"
#include "../src/region_reveal/preview_tile.hpp"
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

DWORD file_size(const char* world) {
    HANDLE file = CreateFileW(visited_path(world).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return 0;
    const DWORD size = GetFileSize(file, nullptr);
    CloseHandle(file);
    return size;
}

void remove_file(const char* world) { DeleteFileW(visited_path(world).c_str()); }

int region_from_block(long long block) { return cw::region_of(static_cast<int>(block / cw::kBlocksPerCell)); }

#pragma pack(push, 1)
struct Header {
    char magic[4];
    std::uint32_t version, dim, count, worldLength;
};
#pragma pack(pop)

void write_file(const char* world, std::uint32_t version, std::uint32_t dim, const std::uint32_t* keys,
                std::uint32_t count) {
    const std::uint32_t length = static_cast<std::uint32_t>(std::strlen(world));
    const Header header{{'R', 'R', 'V', 'S'}, version, dim, count, length};
    std::string blob(reinterpret_cast<const char*>(&header), sizeof(header));
    blob.append(world, length);
    blob.append(reinterpret_cast<const char*>(keys), count * sizeof(std::uint32_t));
    write_raw(world, blob.data(), blob.size());
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
    check(cw::kTileDim * cw::kBlocksPerVoxel == cw::kBlocksPerCell, "a tile's 32 voxels span its cell");
}

void cell_keys() {
    std::printf("cell keys\n");
    const std::uint32_t key = rr::cell_key(32788, 32804);
    check(rr::key_x(key) == 32788 && rr::key_y(key) == 32804, "a key unpacks to its cell");
    check(rr::key_x(rr::cell_key(cw::kMapDim - 1, 0)) == cw::kMapDim - 1, "the last cell on x survives packing");
    check(rr::cell_key(1, 0) > rr::cell_key(0, cw::kMapDim - 1), "keys sort by x first");
}

void areas_round_trip() {
    std::printf("visited areas\n");
    remove_file("trip");
    {
        rr::VisitedAreas v;
        v.open("trip");
        check(v.cells().empty(), "a world with no file starts empty");
        check(v.add(32788, 32804), "entering an area records the cell");
        check(!v.add(32788, 32804), "the same cell again changes nothing");
        check(v.add(32830, 32790), "a second area records a second cell");
        check(!v.add(-1, 5) && !v.add(cw::kMapDim, 5), "cells outside the world are refused");
        v.flush();
    }
    check(file_size("trip") == sizeof(Header) + 4 + 2 * 4, "the file holds the two cells and nothing else");

    rr::VisitedAreas v;
    v.open("trip");
    check(v.cells().size() == 2, "both cells reload");
    check(v.cells()[0] == rr::cell_key(32788, 32804) && v.cells()[1] == rr::cell_key(32830, 32790),
          "in sorted order");
    remove_file("trip");
}

void version_2_converts() {
    std::printf("version 2 conversion\n");
    // Two v2 regions: (4098,4100) and (4102,4101), keyed region x << 16 | y.
    const std::uint32_t regions[] = {(4098u << 16) | 4100u, (4102u << 16) | 4101u};
    write_file("old2", 2, cw::kRegionDim, regions, 2);

    {
        rr::VisitedAreas v;
        v.open("old2");
        check(v.cells().size() == 2, "both v2 regions are kept");
        check(v.cells()[0] == rr::cell_key(4098 * 8 + 4, 4100 * 8 + 4), "a region becomes the cell at its middle");
        check(v.cells()[1] == rr::cell_key(4102 * 8 + 4, 4101 * 8 + 4), "and so does the other");
        v.flush();
    }
    check(file_size("old2") == sizeof(Header) + 4 + 2 * 4, "the next flush writes it back as v3");

    rr::VisitedAreas v;
    v.open("old2");
    check(v.cells().size() == 2, "the rewritten file reloads the same cells");
    remove_file("old2");
}

void world_isolation() {
    std::printf("world isolation\n");
    remove_file("one");
    remove_file("two");

    rr::VisitedAreas v;
    v.open("one");
    v.add(100, 100);
    v.flush();

    v.open("two");
    check(v.cells().empty(), "a different world starts empty");

    v.open("one");
    check(v.cells().size() == 1, "switching back restores the original");
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

    rr::VisitedAreas v;
    v.open("old");
    check(v.cells().empty(), "a version 1 file is rejected, not converted");
    remove_file("old");

    const std::uint32_t unsorted[] = {rr::cell_key(9, 9), rr::cell_key(1, 1)};
    write_file("mess", 3, cw::kMapDim, unsorted, 2);
    v.open("mess");
    check(v.cells().empty(), "unsorted cells count as damage");
    remove_file("mess");

    const std::uint32_t fine[] = {rr::cell_key(1, 1)};
    write_file("dims", 3, cw::kRegionDim, fine, 1);
    v.open("dims");
    check(v.cells().empty(), "a v3 header with the wrong grid size is rejected");
    remove_file("dims");

    const char garbage[] = "not a visited file at all";
    write_raw("junk", garbage, sizeof(garbage));
    v.open("junk");
    check(v.cells().empty(), "garbage is rejected");
    remove_file("junk");

    write_raw("trunc", garbage, 4);
    v.open("trunc");
    check(v.cells().empty(), "a truncated file is rejected");
    remove_file("trunc");

    v.open("");
    check(v.world().empty(), "an empty world name closes the set");
    v.open("../escape");
    check(v.world().empty(), "a path-like world name is refused");
}

// Every non-empty voxel of a column, lowest first.
int column_layers(const rr::PreviewTile& tile, int x, int y, int* lowest, int* highest) {
    int count = 0;
    *lowest = -1;
    *highest = -1;
    for (int z = 0; z < tile.depth; ++z) {
        const std::uint8_t* v = tile.at(x, y, z);
        if (!v[0] && !v[1] && !v[2]) continue;
        if (*lowest < 0) *lowest = z;
        *highest = z;
        ++count;
    }
    return count;
}

void preview_flat_land() {
    std::printf("preview: flat land\n");
    rr::PreviewSamples s;
    for (auto& row : s.heights) {
        for (float& h : row) h = 100.5f;
    }
    const rr::PreviewTile tile = rr::synthesize_preview(s);

    check(tile.depth == 1, "flat ground is one voxel layer");
    check(tile.base == 100 / 8, "the layer is the one holding block 100");
    check(tile.voxels.size() == 32u * 32u * 3u, "the buffer is exactly 32 x 32 x depth RGB");
    int lo = 0, hi = 0;
    bool all = true;
    for (int x = 0; x < 32; ++x) {
        for (int y = 0; y < 32; ++y) all = all && column_layers(tile, x, y, &lo, &hi) == 1;
    }
    check(all, "every column has its one voxel");
    const std::uint8_t* v = tile.at(5, 5, 0);
    check(v[1] > v[0] && v[1] > v[2], "grass reads green");
}

void preview_sea() {
    std::printf("preview: sea\n");
    rr::PreviewSamples s;
    for (auto& row : s.heights) {
        for (float& h : row) h = -40.0f;
    }
    const rr::PreviewTile tile = rr::synthesize_preview(s);

    check(tile.depth == 1, "open sea is a single surface layer");
    check(tile.base == 0, "the surface sits on the layer holding sea level");
    const std::uint8_t* v = tile.at(10, 20, 0);
    check(v[2] > v[0] && v[2] > v[1], "water reads blue");
}

void preview_cliff() {
    std::printf("preview: cliff\n");
    // Low ground on the west half, a 64-block cliff on the east half.
    rr::PreviewSamples s;
    for (int i = 0; i < rr::PreviewSamples::kSpan; ++i) {
        for (int j = 0; j < rr::PreviewSamples::kSpan; ++j) s.heights[i][j] = i <= 16 ? 40.0f : 104.0f;
    }
    const rr::PreviewTile tile = rr::synthesize_preview(s);

    check(tile.base == 40 / 8, "the base is the low ground's layer");
    check(tile.depth == 104 / 8 - 40 / 8 + 1, "the depth reaches the cliff top");

    int lo = 0, hi = 0;
    const int edge = column_layers(tile, 16, 10, &lo, &hi);  // first cliff column, heights index 17
    check(hi == 104 / 8 - tile.base, "the cliff column tops out at the cliff");
    check(lo == 40 / 8 + 1 - tile.base, "and reaches down to just above the low ground: no gap");
    check(edge == hi - lo + 1, "with every layer in between filled");

    const std::uint8_t* top = tile.at(16, 10, hi);
    check(std::abs(top[0] - top[1]) < 40 && std::abs(top[1] - top[2]) < 40, "a 64-block rise is drawn as rock");

    const int inland = column_layers(tile, 25, 10, &lo, &hi);
    check(inland == 1, "a column on the plateau is a single voxel");
}

void preview_never_black() {
    std::printf("preview: never black\n");
    // Black is the empty voxel, so no colour may round down to it, whatever the
    // terrain - including heights right at sea level and below it.
    rr::PreviewSamples s;
    for (int i = 0; i < rr::PreviewSamples::kSpan; ++i) {
        for (int j = 0; j < rr::PreviewSamples::kSpan; ++j) {
            s.heights[i][j] = static_cast<float>((i * 37 + j * 11) % 600) - 100.0f;
        }
    }
    const rr::PreviewTile tile = rr::synthesize_preview(s);
    bool ok = true;
    for (int x = 0; x < 32; ++x) {
        for (int y = 0; y < 32; ++y) {
            int lo = 0, hi = 0;
            ok = ok && column_layers(tile, x, y, &lo, &hi) >= 1;
        }
    }
    check(ok, "every column of rough terrain has at least its surface voxel");
    check(static_cast<int>(tile.voxels.size()) == 32 * 32 * tile.depth * 3, "and the buffer matches the depth");
}

void landmark_table() {
    std::printf("landmark table\n");
    // Only the four values a map screenshot pinned down are named; the rest must
    // not print a word that looks authoritative.
    check(std::strcmp(cw::landmark_name(1), "City") == 0, "raw 1 is City, counted 2 for 2");
    check(std::strcmp(cw::landmark_name(2), "Mountain") == 0, "raw 2 is Mountain, 3 for 3");
    check(std::strcmp(cw::landmark_name(3), "Forest") == 0, "raw 3 is Forest, 2 for 2 plus a probe");
    check(std::strcmp(cw::landmark_name(4), "Lake") == 0, "raw 4 is Lake, 1 for 1");
    check(std::strcmp(cw::landmark_name(0), "none") == 0, "raw 0 is no landmark");
    check(std::strcmp(cw::landmark_name(14), "adventure?") == 0,
          "raw 14 does not claim a name: 11 regions held it against 6 temples drawn");
    check(std::strcmp(cw::landmark_name(99), "unmapped") == 0, "an unknown raw says so");
    check(cw::landmark_kind(1) == cw::LandmarkKind::Settlement, "City is a settlement");
    check(cw::landmark_kind(3) == cw::LandmarkKind::Natural, "Forest is natural");
}

}  // namespace

int main() {
    geometry();
    cell_keys();
    areas_round_trip();
    version_2_converts();
    world_isolation();
    fails_closed();
    preview_flat_land();
    preview_sea();
    preview_cliff();
    preview_never_black();
    landmark_table();

    std::printf("\n%s\n", g_failures ? "FAILURES" : "all region tests passed");
    return g_failures == 0 ? 0 : 1;
}
