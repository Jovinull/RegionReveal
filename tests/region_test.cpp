// Exercises everything that needs neither the game nor a Cube.exe: the
// area-lookup geometry, the visited-area file, the revealed-area set and the
// per-cell answers the map's label passes get.

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <utility>

#include "../src/game/cube_world.hpp"
#include "../src/game/world.hpp"
#include "../src/region_reveal/areas.hpp"
#include "../src/region_reveal/marks.hpp"
#include "../src/region_reveal/paths.hpp"
#include "../src/region_reveal/visited.hpp"

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    if (!ok) ++g_failures;
    std::printf("  %-4s %s\n", ok ? "ok" : "FAIL", what);
}

// --- the visited file -------------------------------------------------------

std::wstring visited_path(const std::string& world) {
    return rr::beside_game(L"RegionReveal_" + std::wstring(world.begin(), world.end()) + L".visited");
}

void write_raw(const std::string& world, const void* data, std::size_t size) {
    FILE* file = nullptr;
    _wfopen_s(&file, visited_path(world).c_str(), L"wb");
    if (!file) return;
    if (size) std::fwrite(data, 1, size, file);
    std::fclose(file);
}

std::string read_raw(const std::string& world) {
    FILE* file = nullptr;
    _wfopen_s(&file, visited_path(world).c_str(), L"rb");
    if (!file) return {};
    std::string out;
    char buffer[4096];
    for (std::size_t n; (n = std::fread(buffer, 1, sizeof(buffer), file)) > 0;) out.append(buffer, n);
    std::fclose(file);
    return out;
}

void remove_file(const std::string& world) { DeleteFileW(visited_path(world).c_str()); }

#pragma pack(push, 1)
struct Header {
    char magic[4];
    std::uint32_t version, dim, count, worldLength;
};
#pragma pack(pop)

std::string file_image(const std::string& world, std::uint32_t version, std::uint32_t dim,
                       const std::uint32_t* keys, std::uint32_t count) {
    const Header header{{'R', 'R', 'V', 'S'}, version, dim, count, static_cast<std::uint32_t>(world.size())};
    std::string blob(reinterpret_cast<const char*>(&header), sizeof(header));
    blob += world;
    blob.append(reinterpret_cast<const char*>(keys), count * sizeof(std::uint32_t));
    return blob;
}

void write_file(const std::string& world, std::uint32_t version, std::uint32_t dim, const std::uint32_t* keys,
                std::uint32_t count) {
    const std::string blob = file_image(world, version, dim, keys, count);
    write_raw(world, blob.data(), blob.size());
}

void geometry() {
    std::printf("geometry\n");
    check(cw::kMapCells == 65536, "the map is 65536 cells per axis");
    check(cw::kBlocksPerChunk == 16384, "a storage chunk spans 16384 blocks");

    // Cell 32800 is in chunk 512: the lookup compares chunks 511 to 513.
    const cw::ChunkSpan middle = cw::chunks_consulted(32800 * 256 + 128, 32800 * 256 + 128);
    check(middle.x0 == 511 && middle.x1 == 513 && middle.y0 == 511 && middle.y1 == 513,
          "a cell's area is decided among its chunk and the eight around it");

    const cw::ChunkSpan corner = cw::chunks_consulted(128, 128);
    check(corner.x0 == 0 && corner.x1 == 1 && corner.y0 == 0 && corner.y1 == 1,
          "at the world's first cell the span is clipped, not negative");

    const int last = (cw::kMapCells - 1) * 256 + 128;
    const cw::ChunkSpan end = cw::chunks_consulted(last, last);
    check(end.x0 == 1022 && end.x1 == 1023 && end.y0 == 1022 && end.y1 == 1023,
          "at the world's last cell the span stops at chunk 1023");

    const cw::ChunkSpan edge = cw::chunks_consulted(512 * 16384, 512 * 16384 - 1);
    check(edge.x0 == 511 && edge.x1 == 513 && edge.y0 == 510 && edge.y1 == 512,
          "either side of a chunk boundary picks its own neighbours");

    const cw::AreaId id = cw::area_id(1023, 7);
    check(cw::area_chunk_x(id) == 1023 && cw::area_chunk_y(id) == 7, "an area id unpacks to its chunk");
}

void cell_keys() {
    std::printf("cell keys\n");
    const std::uint32_t key = rr::cell_key(32788, 32804);
    check(rr::key_x(key) == 32788 && rr::key_y(key) == 32804, "a key unpacks to its cell");
    check(rr::key_x(rr::cell_key(cw::kMapCells - 1, 0)) == cw::kMapCells - 1,
          "the last cell on x survives packing");
    check(rr::cell_key(1, 0) > rr::cell_key(0, cw::kMapCells - 1), "keys sort by x first");
}

void visited_round_trip() {
    std::printf("visited areas\n");
    remove_file("trip");
    {
        rr::VisitedAreas v;
        v.open("trip");
        check(v.cells().empty(), "a world with no file starts empty");
        check(v.add(32830, 32790), "entering an area records the cell");
        check(!v.add(32830, 32790), "the same cell again changes nothing");
        check(v.add(32788, 32804), "a second area records a second cell");
        check(!v.add(-1, 5) && !v.add(cw::kMapCells, 5), "cells outside the world are refused");
        v.flush();
    }
    const std::uint32_t expected[] = {rr::cell_key(32788, 32804), rr::cell_key(32830, 32790)};
    check(read_raw("trip") == file_image("trip", 3, cw::kMapCells, expected, 2),
          "the file is the header, the name and the sorted cells, nothing else");

    rr::VisitedAreas v;
    v.open("trip");
    check(v.cells().size() == 2 && v.cells()[0] == expected[0] && v.cells()[1] == expected[1],
          "both cells reload, in order");
    remove_file("trip");
}

void version_2_converts() {
    std::printf("version 2 conversion\n");
    // Two 8 x 8-cell blocks, (4098,4100) and (4102,4101).
    const std::uint32_t blocks[] = {rr::cell_key(4098, 4100), rr::cell_key(4102, 4101)};
    write_file("old2", 2, 8192, blocks, 2);

    rr::VisitedAreas v;
    v.open("old2");
    const std::uint32_t converted[] = {rr::cell_key(4098 * 8 + 4, 4100 * 8 + 4),
                                       rr::cell_key(4102 * 8 + 4, 4101 * 8 + 4)};
    check(v.cells().size() == 2 && v.cells()[0] == converted[0] && v.cells()[1] == converted[1],
          "each block becomes the cell at its middle");
    check(read_raw("old2") == file_image("old2", 3, cw::kMapCells, converted, 2),
          "the file is rewritten as version 3 as soon as it is opened");
    remove_file("old2");

    const std::uint32_t outside[] = {rr::cell_key(8192, 1)};
    write_file("bad2", 2, 8192, outside, 1);
    v.open("bad2");
    check(v.cells().empty(), "a version 2 block outside the world counts as damage");
    remove_file("bad2");
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

    const std::uint32_t one[] = {rr::cell_key(5, 5)};
    const std::string borrowed = file_image("one", 3, cw::kMapCells, one, 1);
    write_raw("three", borrowed.data(), borrowed.size());
    v.open("three");
    check(v.cells().empty(), "a file naming another world is not used");
    remove_file("one");
    remove_file("two");
    remove_file("three");
}

void world_names() {
    std::printf("world names\n");
    rr::VisitedAreas v;
    remove_file("My World 2");
    v.open("My World 2");
    check(v.world() == "My World 2", "spaces and digits are accepted");
    v.add(1, 1);
    v.flush();
    check(!read_raw("My World 2").empty(), "and its file is written");
    remove_file("My World 2");

    v.open("");
    check(v.world().empty(), "an empty name closes the set");
    check(!v.add(1, 1), "and nothing can be recorded");
    for (const char* name : {"../escape", "a\\b", "c:d", "what?", "star*", "pipe|", "quote\"", "tab\tname"}) {
        v.open(name);
        std::string what = "refused: ";
        what += name;
        check(v.world().empty(), what.c_str());
    }
    v.open(std::string(65, 'x'));
    check(v.world().empty(), "a name longer than 64 characters is refused");
}

void damaged_files() {
    std::printf("damaged files\n");
    rr::VisitedAreas v;

    struct V1Header {
        char magic[4];
        std::uint32_t version, gridDim, bitsBytes, worldLength;
    } v1{{'R', 'R', 'V', 'S'}, 1, 1024, 16, 3};
    std::string blob(reinterpret_cast<const char*>(&v1), sizeof(v1));
    blob += "old";
    blob.append(16, '\xff');
    write_raw("old", blob.data(), blob.size());
    v.open("old");
    check(v.cells().empty(), "a version 1 file is rejected, not converted");
    remove_file("old");

    const std::uint32_t unsorted[] = {rr::cell_key(9, 9), rr::cell_key(1, 1)};
    write_file("mess", 3, cw::kMapCells, unsorted, 2);
    v.open("mess");
    check(v.cells().empty(), "unsorted cells count as damage");
    remove_file("mess");

    const std::uint32_t fine[] = {rr::cell_key(1, 1)};
    write_file("dims", 3, 8192, fine, 1);
    v.open("dims");
    check(v.cells().empty(), "a version 3 header with the wrong grid size is rejected");
    remove_file("dims");

    const std::string extra = file_image("tail", 3, cw::kMapCells, fine, 1) + "x";
    write_raw("tail", extra.data(), extra.size());
    v.open("tail");
    check(v.cells().empty(), "trailing bytes are rejected");
    remove_file("tail");

    const std::string cut = file_image("cut", 3, cw::kMapCells, unsorted, 2);
    write_raw("cut", cut.data(), cut.size() - 2);
    v.open("cut");
    check(v.cells().empty(), "a file cut short is rejected");
    remove_file("cut");

    const char garbage[] = "not a visited file at all";
    write_raw("junk", garbage, sizeof(garbage));
    v.open("junk");
    check(v.cells().empty(), "garbage is rejected");
    check(v.add(3, 3), "and the world can still be recorded from scratch");
    v.flush();
    v.open("other");
    v.open("junk");
    check(v.cells().size() == 1, "the damaged file is replaced by a good one");
    remove_file("junk");
}

// --- revealed areas and the label passes' answers ----------------------------

// A world whose areas come from a table, and which can refuse to decide.
struct FakeWorld {
    std::map<std::pair<int, int>, cw::AreaId> areas;
    cw::AreaId elsewhere = 0;  // the area of every cell not in `areas`; 0 = undecided
    bool generated = true;
    int lookups = 0;

    static cw::AreaLookup lookup(void* context, int x, int y) {
        auto* world = static_cast<FakeWorld*>(context);
        ++world->lookups;
        if (!world->generated) return {};
        const auto found = world->areas.find({x, y});
        if (found != world->areas.end()) return {true, found->second};
        if (world->elsewhere) return {true, world->elsewhere};
        return {};
    }
};

void revealed_areas() {
    std::printf("revealed areas\n");
    rr::RevealedAreas areas;
    const unsigned start = areas.generation();
    check(areas.add(7), "a new area is added");
    check(!areas.add(7), "the same area again is not new");
    check(areas.contains(7) && !areas.contains(8), "membership is exact");
    check(areas.generation() != start, "adding changes the generation");

    FakeWorld world;
    world.areas[{10, 10}] = 3;
    world.areas[{20, 20}] = 4;
    const std::uint32_t stored[] = {rr::cell_key(10, 10), rr::cell_key(20, 20), rr::cell_key(30, 30)};
    areas.reset({stored, stored + 3});
    check(areas.count() == 0 && areas.pending() == 3, "a new world forgets its areas and queues its cells");

    world.generated = false;
    areas.resolve(&FakeWorld::lookup, &world);
    check(areas.count() == 0 && areas.pending() == 3, "nothing resolves before the world is generated");

    world.generated = true;
    areas.resolve(&FakeWorld::lookup, &world);
    check(areas.contains(3) && areas.contains(4), "stored cells become their areas");
    check(areas.pending() == 1, "a cell still undecided stays queued");
}

void label_passes() {
    std::printf("label passes\n");
    rr::LabelCells cells;
    rr::RevealedAreas areas;
    FakeWorld world;
    world.areas[{100, 100}] = 1;
    world.areas[{101, 100}] = 2;
    areas.add(1);
    const std::uint32_t now = 5000;

    check(cells.revealed(100, 100, now, areas, &FakeWorld::lookup, &world),
          "a cell of a revealed area is revealed");
    check(!cells.revealed(101, 100, now, areas, &FakeWorld::lookup, &world), "a cell of another area is not");
    const int asked = world.lookups;
    for (int frame = 0; frame < 10; ++frame) {
        cells.revealed(100, 100, now, areas, &FakeWorld::lookup, &world);
        cells.revealed(101, 100, now, areas, &FakeWorld::lookup, &world);
    }
    check(world.lookups == asked, "a decided cell is looked up once, not every frame");

    areas.add(2);
    check(cells.revealed(101, 100, now, areas, &FakeWorld::lookup, &world),
          "entering the other area reveals its cells at once");
    check(world.lookups == asked, "without asking the game again");

    world.areas[{102, 100}] = 1;
    world.generated = false;
    check(!cells.revealed(102, 100, now, areas, &FakeWorld::lookup, &world), "an undecided cell is not revealed");
    world.generated = true;
    check(!cells.revealed(102, 100, now + 999, areas, &FakeWorld::lookup, &world),
          "and is not asked again within a second");
    check(cells.revealed(102, 100, now + 1000, areas, &FakeWorld::lookup, &world), "but is after one");

    cells.clear();
    world.areas[{100, 100}] = 9;
    check(!cells.revealed(100, 100, now, areas, &FakeWorld::lookup, &world),
          "after a world change every cell is decided afresh");

    areas.add(9);
    std::uint8_t real[cw::kCellSize];
    for (int i = 0; i < cw::kCellSize; ++i) real[i] = static_cast<std::uint8_t>(i * 7);
    real[cw::kCellFlags] = 0x80;
    auto* cell = reinterpret_cast<cw::MapCell*>(real);
    const auto* copy =
        reinterpret_cast<const std::uint8_t*>(cells.view(cell, 100, 100, now, areas, &FakeWorld::lookup, &world));
    check(copy != real, "a label pass asking for a revealed cell gets a copy, never the cell itself");
    check(copy[cw::kCellFlags] == (0x80 | cw::kRevealedBit), "the copy has the reveal bit and keeps the other flags");
    bool same = true;
    for (int i = 0; i < cw::kCellSize; ++i) same = same && (i == cw::kCellFlags || copy[i] == real[i]);
    check(same, "every other byte matches the cell");
    check(real[cw::kCellFlags] == 0x80, "the cell itself is untouched");
    check(cells.view(cell, 101, 101, now, areas, &FakeWorld::lookup, &world) == cell,
          "a cell outside every revealed area is handed back as it is");

    real[0x10] = 0x42;
    const auto* again =
        reinterpret_cast<const std::uint8_t*>(cells.view(cell, 100, 100, now, areas, &FakeWorld::lookup, &world));
    check(again == copy && again[0x10] == 0x42, "asking again refreshes the same copy, so it never goes stale");

    world.elsewhere = 9;
    cells.set_radius(cw::kMaxLabelRadius);
    std::set<const void*> slots;
    const int side = 2 * cw::kMaxLabelRadius;
    for (int x = 0; x < side; ++x) {
        for (int y = 0; y < side; ++y) {
            slots.insert(cells.view(cell, 32736 + x, 32736 + y, now, areas, &FakeWorld::lookup, &world));
        }
    }
    check(slots.size() == static_cast<std::size_t>(side * side),
          "every cell of the widest label window, 254 x 254, gets its own copy");
}

// --- marks on landmark names ------------------------------------------------

void place_marks() {
    std::printf("place marks\n");
    std::uint8_t record[0x68] = {};
    const std::int64_t x = (32820LL * 256 + 40) * 65536;
    const std::int64_t y = (32821LL * 256 + 200) * 65536;
    std::memcpy(record + cw::kPlaceOriginX, &x, sizeof(x));
    std::memcpy(record + cw::kPlaceOriginY, &y, sizeof(y));
    const cw::Cell origin = rr::place_origin(record);
    check(origin.x == 32820 && origin.y == 32821, "a place's origin is the cell holding its position");

    check(rr::place_mark(record, false) == rr::PlaceMark::None, "a place never visited has no mark");
    check(rr::place_mark(record, true) == rr::PlaceMark::Visited, "a place the game revealed is marked visited");

    const std::uint32_t mission = 26572;
    std::memcpy(record + cw::kPlaceMission, &mission, sizeof(mission));
    record[cw::kPlaceMissionState] = 1;
    check(rr::place_mark(record, true) == rr::PlaceMark::Visited, "a boss still being fought is not defeated");
    record[cw::kPlaceMissionState] = cw::kMissionDone;
    check(rr::place_mark(record, false) == rr::PlaceMark::BossDefeated,
          "a defeated boss marks its place, visited or not");
    const std::uint32_t none = 0;
    std::memcpy(record + cw::kPlaceMission, &none, sizeof(none));
    check(rr::place_mark(record, false) == rr::PlaceMark::None, "the state alone, without a mission, means nothing");

    float white[4] = {1, 1, 1, 1};
    rr::apply_mark_color(rr::PlaceMark::Visited, white);
    check(white[0] == 1 && white[1] == 1 && white[2] == 1, "a visited place keeps the game's colour");
    rr::apply_mark_color(rr::PlaceMark::BossDefeated, white);
    check(white[0] < 1 && white[1] == 1 && white[2] < 1 && white[3] == 1, "a defeated boss turns its name green");
}

void marked_text() {
    std::printf("marked text\n");
    rr::MarkedText marked;

    cw::GameWString shortName{};
    wcscpy_s(shortName.buffer, L"LAKE");
    shortName.size = 4;
    shortName.capacity = 7;
    check(marked.apply(&shortName, rr::PlaceMark::None) == &shortName, "no mark: the game's own string");
    const cw::GameWString* out = marked.apply(&shortName, rr::PlaceMark::Visited);
    check(out != &shortName && out->capacity >= 8 && std::wstring(out->chars(), out->size) == L"LAKE \u2022",
          "a short name gets the visited dot");
    check(std::wstring(shortName.chars(), shortName.size) == L"LAKE", "and the game's string is untouched");

    wchar_t heap[] = L"CATACOMBS OF DAMAION";
    cw::GameWString longName{};
    longName.pointer = heap;
    longName.size = static_cast<std::uint32_t>(wcslen(heap));
    longName.capacity = 31;
    out = marked.apply(&longName, rr::PlaceMark::BossDefeated);
    check(std::wstring(out->chars(), out->size) == L"CATACOMBS OF DAMAION \u2020",
          "a long name gets the dagger of a defeated boss");
    check(out->chars()[out->size] == 0, "and stays terminated");

    longName.size = 40;
    longName.capacity = 20;
    check(marked.apply(&longName, rr::PlaceMark::Visited) == &longName, "a string that makes no sense is left alone");

    std::wstring huge(200, L'A');
    cw::GameWString hugeName{};
    hugeName.pointer = &huge[0];
    hugeName.size = 200;
    hugeName.capacity = 200;
    check(marked.apply(&hugeName, rr::PlaceMark::Visited) == &hugeName, "a name too long to extend is left alone");
}

void hidden_cells() {
    std::printf("hidden cells\n");
    rr::LabelCells cells;
    std::uint8_t real[cw::kCellSize];
    for (int i = 0; i < cw::kCellSize; ++i) real[i] = static_cast<std::uint8_t>(i * 5 + 1);
    real[cw::kCellFlags] = 0x83;
    const auto* hidden = reinterpret_cast<const std::uint8_t*>(
        cells.hidden(reinterpret_cast<const cw::MapCell*>(real), 7, 9));
    check(hidden != real && (hidden[cw::kCellFlags] & cw::kRevealedBit) == 0,
          "a hidden district is a copy without the reveal bit");
    check(hidden[cw::kCellFlags] == 0x82 && real[cw::kCellFlags] == 0x83, "other flags kept, the cell untouched");
}

}  // namespace

int main() {
    geometry();
    cell_keys();
    visited_round_trip();
    version_2_converts();
    world_isolation();
    world_names();
    damaged_files();
    revealed_areas();
    label_passes();
    place_marks();
    marked_text();
    hidden_cells();

    std::printf("\n%s\n", g_failures ? "FAILURES" : "all region tests passed");
    return g_failures == 0 ? 0 : 1;
}
