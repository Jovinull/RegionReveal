# Validation audit

A round of falsification rather than feature work. A claim only reaches
CONFIRMED here when an attempt to disprove it failed.

## What the 2026-10-03 round changed

| Earlier claim | Verdict now |
|---|---|
| "The pass at `0x4C9831` is the terrain pass" | **Wrong.** It draws a cell's point of interest, a label. Terrain is drawn by `WorldMap::render` (`0x5FC1B0`) from the tile image at `ZoneTile+0x08`, whether or not the reveal bit is set. |
| "Revealing terrain means making the game generate, and save, a tile per cell" | **Wrong.** The map draws any tile image attached to a cell. The world generator's height function (`0x5C5E20`) is analytic, so the mod builds a preview from it with the game's own tile code and attaches it in memory; nothing is saved. |
| "A region is the 8 x 8-cell block of one `0x68` record" | **True of the record, not of what the map borders.** The dotted lines bound named areas, a Voronoi of one centre per storage chunk (`0x477E10`). The reveal unit is now the area. |
| "`ZoneTile+0x10` is unknown" | **Resolved.** Point-of-interest type byte; `+0x18` is its level. |

## What the 2026-08 round changed about earlier claims

| Earlier claim | Verdict now |
|---|---|
| "A chunk covers 64x64 cells starting 8 bytes into it" | **Wrong.** `getCell` computes `chunk + index * 0x34`; there is no header. The chunk constructor builds `0x1000` objects starting at offset 0. Fixed in `src/game/cube_world.hpp`. |
| "The map cell is an anonymous 0x34-byte struct" | **Superseded.** It is `cube::ZoneTile` — its constructor writes that class's RTTI vftable, `0x71DFBC`. |
| "`cube::Region` vs. map chunk is a hypothesis" | **Resolved, differently.** The save keys chunks as `reg<x>_<y>`, so the game itself calls a chunk a region. The `cube::Region` *class* is a separate, much larger object and remains unconnected. |
| "Terrain data does not exist, so terrain reveal is impossible" | **Withdrawn — premature.** Terrain thumbnails exist as their own per-cell records, `tile<x>_<y>`, 32x32. The question is generation cost, not existence. |
| "`WorldMap+0x90..0x9C` may track the current region" | **Dead lead.** Written only by the constructor, to `-1`, and by nothing else in the translation unit. |
| "Windows Defender blocks the build" | **Wrong scanner.** Defender's real-time protection is off; McAfee is the active one. |
| "Qube-Loader could not be found" | **Wrong.** It exists at `qad3n/Qube-Loader`; a failed search was reported as a negative result. Its offsets resolve in our 2013-07-20 build and not the other — see `docs/QUBE_COMPATIBILITY.md`. |
| "The Classic launcher targets the 2019 release, so it would reject Alpha" | **Wrong for the version that matters.** That was read off `master`. The Alpha ecosystem uses the 2018 **v1.5**, which gates on `fileSize == 3885568` — our 2013-07-20 build exactly — and names it **Alpha 0.1.1**. |
| "The mod reveals the player's region" | **Incomplete.** It reveals only the *current* region, and previously visited regions revert when the player leaves. That does not meet the requirement; see `docs/BEHAVIOUR.md`. |

## Confirmed

- **The map cell is `cube::ZoneTile`.** `0x5FB7F0` writes vftable `0x71DFBC`,
  which the RTTI scan resolves to `.?AVZoneTile@cube@@`.
- **Chunk layout.** `0x603230` allocates `0x35A00` bytes, runs the vector
  constructor iterator over `0x1000` objects of size `0x34`, then constructs 64
  objects of `0x68` at `chunk + 0x34000`. `0x34000 + 64 * 0x68 = 0x35A00`, exactly
  the allocation.
- **A chunk is what the game calls a region.** The live save holds `reg509_509`
  through `reg515_515`; the player's chunk that session was `(512, 512)` and
  `32768 >> 6 == 512`.
- **Map database schema.** `Save/map_<world>.db` is a SQLite `blobs(key, value)`
  store with four record kinds: `land<cx>_<cy>` (25 present, 304–328 B),
  `reg<cx>_<cy>` (49 present, all exactly 60164 B), `tile<x>_<y>` (57 present,
  7–8 KB, variable) and `discovered` (4 B).
- **`tile` records are per cell, not per region.** Header reads
  `01 00 00 00 | 15 00 00 00 | 20 00 00 00 | 20 00 00 00` — version, a tag, then
  **32 x 32**. Observed keys such as `tile32797_32800` fall inside the player's
  region.
- **`discovered` is the reveal counter.** The saved value was `13`, and
  `WorldMap+0x8000BC` is what `discover` increments.
- **The renderer holds a lock while drawing.** `0x601EA0` is
  `LeaveCriticalSection(this + 0x8000D8)`, called from inside the draw method.
- **The five stolen bytes need no relocation, in both builds.** Disassembled
  instruction by instruction: `push ebp; mov ebp, esp; push reg; push reg` — no
  relative branch, no displacement, no implicit EIP use, ending exactly on an
  instruction boundary at five bytes.

## Strong evidence

- **The map renderer reads only grid A.** Grid B at `WorldMap+0x4000B0` holds
  `cube::LandscapeTile` (key `land`) and has 40 call sites across the binary,
  none of them inside the draw method's range.
- **The map only ever draws the current region.** Over a two-minute session the
  renderer asked for `x = 32768..32833` — chunk 512 plus a two-cell fringe — while
  the player stayed in chunk 512. One session in one region; a second observation
  elsewhere is needed before promoting this.
- **RegionReveal persists nothing.** The detour returns a copy and never writes;
  `lit=0` shows the game itself had revealed none of the cells the mod lit, and
  the saved `discovered` counter stayed at 13. Not conclusive: the `reg` record
  format was not decoded, so the saved bits were never read back directly.

## Hypothesis

- ~~`ZoneTile+0x10` is the handle to that cell's loaded 32x32 tile image.~~
  **Falsified.** The tile handler never calls `getCell` and never touches the
  cell grid, and the renderer treats `+0x10` as a byte whose address it takes,
  not as a handle. What the field is remains UNKNOWN; see
  `docs/POI_AND_TERRAIN.md`.

## Unknown

- **The `reg` record format.** All 49 blobs are exactly 60164 bytes and the
  dominant byte period is 14, but `60164 = 4096 * 14.6875 + 4` does not divide
  cleanly, and a falsification test failed: predicting that exactly 13 records
  (matching `discovered = 13`) would carry a set reveal bit found no matching byte
  position at any phase or bit. The format is **not** decoded, and no claim is
  made about which serialized byte holds the reveal flag.
- **Who writes `ZoneTile+0x10`.** A linear sweep of `.text` desynchronised and
  produced only false hits inside CRT code; the scan was discarded rather than
  reported. Needs recursive descent or a write watchpoint.
- ~~An authoritative current-region source.~~ **Resolved:** the local
  `Creature` through `owner + 0x8006D0`, and the area from `cube::World`'s own
  lookup; the `discover` hook is gone.

## Terrain: root-cause status

Resolved on 2026-10-03. What the 2026-08 round wrote here was built on two
misreadings, both corrected in `docs/POI_AND_TERRAIN.md`:

- the pass gated on `+0x10` draws labels, not ground, so "the renderer skips a
  cell when `+0x10` is zero" was never about terrain;
- the map draws a cell's tile image whenever one is attached, revealed or not.

So the ground of an area needs a tile per cell, not a bit per cell, and the
game's own generator still cannot make one without a resident zone. What it does
not need is the game's generator: the height function is analytic, the tile
image class is callable, and an image attached in memory is drawn and later
freed by the game like any other. The cost is memory, about 230 KB a
preview in a process near its address-space limit, which is why previews are
built only near the map's centre and only while the map is open.

## Marker reveal, proven side by side

The one thing counters could never settle. Same world, same position, map opened
both times:

| | Landmark labels drawn |
|---|---|
| RegionReveal loaded | **24** |
| `dinput8.dll` renamed away | **2** |

The two that survive are `GAGAR FOREST`, the region the player was standing in,
and `ASMI CITY`, one they had already discovered. **Twenty-two labels exist only
because of the mod.** The terrain is pixel-identical between the two, which is
what the design predicts: the mod answers a visibility question, it does not
generate ground.

The screenshot also settled four landmark values by counting them against the
same world's save over the 30-region survey - raw 1 drew two CITY labels, raw 2
three MOUNTAINS, raw 3 two FOREST, raw 4 one LAKE - and falsified the rest of the
table, where eleven raw-14 regions produced six temples. Those names were
withdrawn; see `src/game/landmarks.hpp`.

## Acceptance matrix

| Requirement | Status | Evidence |
|---|---|---|
| DLL loads safely | PASS | Both builds log `RegionReveal active`; game keeps running |
| Primary build detected | PASS | 2013-07-20, resolved in the live mapped image |
| Secondary build detected | PASS | 2013-07-02, resolved in the live mapped image |
| Unsupported build fails closed | PASS | No signature resolves in `Server.exe`; `find_unique` rejects a second match |
| Stolen bytes relocation-free | PASS | Instruction-level check, both builds |
| Signatures unique | PASS | Exactly one match per build, all nine signatures |
| Region == map chunk | SUPERSEDED | True of the storage chunk the save keys as `reg<x>_<y>`; the unit that carries one landmark, and that the mod now calls a region, is the 8x8-cell block of one `0x68` record |
| Primary target externally corroborated | PASS | Qube-Loader offsets resolve only in 2013-07-20; launcher v1.5 gates on its exact size |
| Persistent visited-area memory | PASS | Format v3, one cell per area entered; observed 2026-10-03 converting a v2 history and recording areas on entry. The region version was observed across a restart on 2026-10-02 |
| Never reveals unvisited areas | PASS by construction | Only cells whose area lookup returns a recorded seed are reported revealed. Seen on screen for the region version on 2026-10-02; not yet checked on screen across an area border |
| Whole area revealed on entry | PASS | 2026-10-03, both builds: one entry revealed 3 389 cells of an area on 2013-07-20 and 4 499 on 2013-07-02, labels drawn to the dotted border |
| Map cell identified | PASS | `cube::ZoneTile`, via the constructor's vftable write |
| A/B proves POI reveal | PASS | 24 labels with the DLL against 2 without, same world and spot; repeated 2026-10-02 as 26 against 2 |
| No persistent save mutation | PASS | With the DLL removed after a session, the map shows only genuinely explored landmarks; a world the mod surveyed kept its explored area. `reg` blobs still never decoded |
| Current player region authoritative | PASS | Read from the local `Creature` via `owner_of(worldMap) + 0x8006D0`, and recorded only once the game has revealed the player's cell; crossings observed 2026-10-02 match a position read independently from outside the process |
| `discover()` dependency removed | PASS | The hook and its signature are gone; one hook remains |
| Per-world identity | PASS | World name read from `[WorldMap+0xAC]+0x94`; two worlds switched in one session kept separate histories |
| Visited-region storage | PASS | Sorted list of visited centres, format v2, in `RegionReveal_<world>.visited`; atomic replace, fail-closed parse, v1 rejected |
| Terrain generator identified | PASS | `0x603A00`, generate path at `0x603F32`, writes back at `0x604E3C` (`docs/POI_AND_TERRAIN.md`) |
| Arbitrary tile generation by the game | **FAIL**, worked around | The game's generator needs a resident zone, which exists only near the player; the mod builds previews from the analytic height function instead |
| Terrain of a revealed area shown | PASS, near the map's centre | Previews with real relief, water and border dots, 6 cells around the view centre by default; observed on both builds 2026-10-03 |
| Previews never saved | PASS | After a session with more than 150 previews on 2013-07-02, `map_oldbuild.db` held 26 `tile` records, all within 3 cells of the player |
| Marker pass understood | PASS | Two draw passes; the marker pass gates on the reveal bit alone, which is why the `+0x10` filter was removed |
| Adjacent region stays hidden | PASS | Seen on screen 2026-10-02: regions three away from every visited one stayed unlabelled until a visit brought them into a survey |
| City / dungeon / boss separately | UNKNOWN | Point-of-interest type 1 is a city and four landmark values are pinned; dungeon and boss are not separated |
| Terrain reveal understood | PASS | Tile image, loader, zone manager and unloader traced; `docs/POI_AND_TERRAIN.md` |
| Performance acceptable | PASS | 26–69 cycles per `getCell` call measured 2026-08-30; previews cost about 10 ms of work each, in slices of at most 5 ms a frame, and only while the map is open |
| Memory within the 32-bit process | PASS, guarded | About 230 KB per preview; building pauses under 400 MB of free address space. A 16-teleport stress run stayed between 1.05 and 1.21 GB private |
| Extended stability | PARTIAL | Sessions of 13 and 15 minutes on 2026-10-02 and the 2026-10-03 stress run without a crash, after fixing the four crashes in `docs/TESTING.md`; the 30-minute run of test I is still to do |
| Loader strategy reviewed | PASS | Proxy audited; Qube-Loader and launcher v1.5 both analysed; no collision with either |
| Documentation consistent | PASS | This round rewrote every contradicted claim |
| Clean reproducible build | PASS | `cmake -B build -A Win32` builds all targets under `/W4 /WX` |

Everything short of PASS is blocked on one of two things: a gameplay session
using a build the scanner tolerates, or the two open reverse-engineering
questions above.
