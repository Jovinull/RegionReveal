# Validation audit

A claim only reaches PASS here when an attempt to disprove it failed, in the
running game where that is possible. `docs/TESTING.md` has the runs.

## Claims that did not survive

| Earlier claim | What is true |
|---|---|
| "A chunk covers 64 × 64 cells starting 8 bytes into it" | There is no header: `getCell` computes `chunk + index * 0x34`. |
| "The map cell is an anonymous 0x34-byte struct" | It is `cube::ZoneTile`; its constructor writes that class's RTTI vftable, `0x71DFBC`. |
| "`ZoneTile+0x10` is the handle of the cell's tile image" | It is the point-of-interest type byte, `+0x18` its level. The tile image is at `+0x08`. |
| "The pass at `0x4C9831` draws terrain" | It draws points of interest. Both overlay passes draw labels; terrain is `WorldMap::render`'s, and does not depend on the reveal bit. |
| "A region is the 8 × 8-cell block of one `0x68` record" | True of the landmark record, not of what the map borders. The dotted lines bound named areas, one centre per storage chunk. |
| "The area lookup returns null until the area is generated" | It returns the nearest of the centres generated *so far*, which can be the wrong area. The mod requires all nine candidates to exist. |
| "`WorldMap+0x90..0x9C` may track the current region" | Written only by the constructor, to `-1`. |
| "`discover` tells us where the player is" | Two of its three callers iterate lists. The mod reads the local player itself. |
| "Qube-Loader could not be found"; "the Classic launcher rejects Alpha" | Both wrong; see `docs/QUBE_COMPATIBILITY.md`. |
| "Windows Defender blocks the build" | McAfee is the active scanner on this machine. |

## Confirmed

- **Map layout.** 65536 × 65536 cells in a sparse 1024 × 1024 grid of storage
  chunks of 64 × 64 cells; each chunk ends with 64 landmark records of `0x68`.
  `0x34000 + 64 * 0x68 = 0x35A00`, exactly the allocation at `0x6032CB`.
- **The reveal bit** is `ZoneTile+0x30` bit 0, written only by
  `WorldMap::discover` (`0x5FC160`).
- **Labels.** The map overlay's draw method calls `getCell` exactly twice, once
  per label pass, and both gate on the reveal bit (`docs/MAP_LABELS.md`).
- **Named areas.** `0x477E10` compares the centres of the 3 × 3 chunks around a
  position; centres live at `World+0x4000BC` and are created only by
  `0x5D7A70`, reached only from zone generation (`docs/REVERSE_ENGINEERING.md`).
- **Chunks are saved byte for byte** (`0x605420` serialises across `0x34000`
  bytes), so a reveal bit written into a cell would be saved. The mod never
  writes one.
- **The five stolen bytes need no relocation, in both builds:**
  `push ebp; mov ebp, esp; push ebx; push esi`.

## Unknown

- **The `reg` record format.** All blobs are exactly 60164 bytes, but which byte
  holds the reveal flag was never found; a falsification test failed.
- **Who writes `ZoneTile+0x10`.** (`0x5FA4C0`, once listed here, is now read:
  a containment test of the cell against the place, `docs/MAP_LABELS.md`.)
- **Dungeon categories.** Only city (`+0x10` type 1) and four landmark
  values (city, mountain, forest, lake) are pinned.

## Acceptance matrix

| Requirement | Status | Evidence |
|---|---|---|
| DLL loads safely, both builds | PASS | `RegionReveal active` logged, game keeps running; set up before the entry point in 6–10 ms |
| Unsupported build fails closed | PASS | `Server.exe` matches neither `getCell` nor the overlay signature; `find_unique` rejects a second match; the mod also refuses unless exactly two label calls are found |
| Signatures unique in both builds | PASS | `tests/run_tests.py`; the area-lookup signature also pins the area-centre table's offset |
| Every label of an entered area shown | PASS | 2026-10-03: 32 of 33 landmarks in view by the mod (1 already explored) and 25 of 25 points of interest, none hidden |
| Nothing shown outside entered areas | PASS | 0 leaked labels in every counted frame, including standing on an area's border with 149 points of interest and 219 landmarks of other areas in the window |
| Terrain untouched | PASS | Only the two label calls get a different answer; the ground matched vanilla on screen |
| No save mutation | PASS | A/B on 2026-10-04: 44 points of interest and 67 landmarks with the DLL, none without, same world and spot |
| Area recorded on entry, map closed or open | PASS | 2026-10-04: crossing into "Aruka Ocean" with the map closed was logged on arrival |
| Persistent, per-world history | PASS | Covered by `region_test`; in game, a restart restored both areas and their labels |
| 2013-07-02 build | PASS | Same counters on `oldbuild`: 25 points of interest and 38 landmarks, 0 hidden, 0 leaked |
| No wrong area from an incomplete lookup | PASS | Undecided cells are never revealed: 21 121 undecided at load, 641 twenty seconds later, none in the player's area |
| World names with spaces | PASS | `mark test` recorded; the previous version refused such names silently |
| Labels at any zoom, whole area at once | PASS | With the default options the point-of-interest pass runs at the default zoom and the window is 192 × 192; from the area's west border its east border is still in the window |
| Label options fail safe | PASS | The twelve bytes are compared before writing and checked on disk in both builds; a mismatch leaves the game's limits and the reveal in place |
| Performance | PASS, with a cost while the map is open | One lookup per cell, cached. Map screen about 80 fps at the game's range, about 60 at the default 96, about 49 at 127 (`docs/BEHAVIOUR.md`); nothing while the map is closed |
| Clean reproducible build | PASS | `cmake -B build -A Win32`, `/W4 /WX`, 92 unit checks |
| Extended stability | PASS | A 30-minute session of moving between areas and using the map: memory 1.1–1.3 GB with no upward trend, no crash |
| Gameplay untouched | PASS | Place and mission records byte for byte identical before and after the reveal; boss missions stay open; a revealed dungeon and city behave as vanilla |
| Multiplayer | PASS | Connected to a local `Server.exe`: the online world gets its own history and its area's labels are revealed |
| Visited marks agree with the game | PASS | 2026-10-05: with the mod, six names in view carried a mark; without it, the game showed exactly those six and no other |
| Boss-defeated mark | PASS | 2026-10-05: defeating the ruler of Ikorok Valley set its mission state to 2 and the name turned green with ` †`, still so after a restart |
| Districts follow the zoom | PASS | Hidden at the default zoom, shown zoomed in (Pet, Crafting, Trade District), in both builds |
| Mod Launcher v1.5 | PASS | `Mods\RegionReveal.dll` with no `dinput8.dll`: `loaded by a mod loader`, same map as the `dinput8.dll` build, alongside the launcher's `CallbackManager.dll` |
