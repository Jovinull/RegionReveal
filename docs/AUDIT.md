# Validation audit

A round of falsification rather than feature work. A claim only reaches
CONFIRMED here when an attempt to disprove it failed.

## What this round changed about earlier claims

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
- **An authoritative current-region source.** The `+0x90` lead is dead, so the
  `discover` hook stays because nothing better has been proven.

## Terrain: root-cause status

The requirement has two halves and only one is addressed today.

**A — reveal POIs and markers.** The mod shadows the reveal bit for cells whose
`+0x10` is non-zero. Live counters show 38 625 cells lit across the session,
roughly 25 per redraw, from a state where `lit = 0`.

**B — reveal the region's terrain.** Not solved, and **not proven impossible**.
What is established:

- a map cell's terrain is a 32x32 image stored as `tile<x>_<y>`;
- only 57 such records existed after the session, against 4096 cells per region;
- the renderer skips a cell entirely when `+0x10` is zero.

Terrain is therefore not a flag waiting to be flipped — the per-cell image has to
exist. That puts B at **Result B**, not Result C: plausibly reachable by making
the game generate those tiles, at a cost nobody has measured, and with a side
effect already visible in the data — generated tiles get written to
`Save/map_*.db`, so this route would mutate the save. Calling it impossible would
run past the evidence; calling it cheap would too.

The experiment that settles it: locate the function that produces a `tile` record
and determine whether it can run for an arbitrary cell with no player present.

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
| Signatures unique | PASS | Exactly one match per build |
| Region == map chunk | PASS | Save keys `reg<x>_<y>`; `32768 >> 6 == 512` |
| Primary target externally corroborated | PASS | Qube-Loader offsets resolve only in 2013-07-20; launcher v1.5 gates on its exact size |
| Persistent visited-region memory | PASS (static) | Bitset plus per-world file; previously FAIL |
| Never reveals unvisited regions | PASS | The chunk comparison rejects everything outside the tracked region; `otherRegion` counted 128 602 rejections |
| Map cell identified | PASS | `cube::ZoneTile`, via the constructor's vftable write |
| A/B proves POI reveal | UNVERIFIED | Counters show 38 625 lit from `lit=0`; no side-by-side comparison made |
| No persistent save mutation | UNVERIFIED | Design writes nothing and `discovered` stayed 13; `reg` blobs never read back |
| Current player region authoritative | PASS (static) | Read from the local `Creature` via `owner_of(worldMap) + 0x8006D0`; no global, no second hook. Runtime crossing tests still to run |
| `discover()` dependency removed | PASS | The hook and its signature are gone; one hook remains |
| Per-world identity | PASS (static) | World name read from `[WorldMap+0xAC]+0x94`, the same string the game concatenates into `Save/map_<name>.db` |
| Visited-region storage | PASS (static) | 128 KiB bitset in `RegionReveal_<world>.visited`, atomic replace, fail-closed parse |
| Terrain generator identified | PASS | `0x603A00`, generate path at `0x603F32`, writes back at `0x604E3C` (`docs/POI_AND_TERRAIN.md`) |
| Arbitrary tile generation | **FAIL** | The generate path is guarded on a resident 256x256 terrain array that exists only near the player |
| Marker pass understood | PASS | Two draw passes; the marker pass gates on the reveal bit alone, which is why the `+0x10` filter was removed |
| Adjacent region stays hidden | UNVERIFIED | `otherRegion` rises at the boundary — consistent, but never seen on screen |
| City / dungeon / boss separately | UNKNOWN | No POI type field identified |
| Terrain reveal understood | PARTIAL | Mechanism identified; generation path not found |
| Performance acceptable | UNMEASURED | 42 M `getCell` calls observed; a cycle counter was added but the scanner deleted that build before it ran |
| Extended stability | UNVERIFIED | Longest observed run is two minutes |
| Loader strategy reviewed | PASS | Proxy audited; Qube-Loader and launcher v1.5 both analysed; no collision with either |
| Documentation consistent | PASS | This round rewrote every contradicted claim |
| Clean reproducible build | PASS | `cmake -B build -A Win32` builds all targets under `/W4 /WX` |

Everything short of PASS is blocked on one of two things: a gameplay session
using a build the scanner tolerates, or the two open reverse-engineering
questions above.
