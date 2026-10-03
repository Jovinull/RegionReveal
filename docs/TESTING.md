# Testing

Loading, hooking, area reveal and terrain previews have been verified in the
running game. The region-based version was run through tests A, B, C, G and part
of I on 2026-10-02; the area-based version through A, C, J and L on 2026-10-03,
on both builds. The game was driven with synthetic input and the
player's position read out of the process; see the two runs below. D, E and F,
and H against a live unsupported build, are still open.

Back up `Save/` before any of it.

## Already verified

Run against isolated copies of both installs, never the originals:

| Check | Result |
|---|---|
| `Cube.exe` starts unmodified in this environment | runs, windowed 1280×720 |
| Proxy loads before the entry point, 2013-07-20 build | `supported build detected; RegionReveal active` |
| Same, 2013-07-02 build | `supported build detected; RegionReveal active` |
| Signatures resolve in the **live mapped image** | all of them, both builds |
| `getCell` trampoline executes and returns correctly | startup self-check passes |
| Game survives with both detours installed | ~14 s, ~700 MB, no crash |
| Signatures on disk land on documented RVAs | `tests/run_tests.py`, both builds |
| Signatures refuse an unrelated binary (`Server.exe`) | no match, hooks would be skipped |

The startup self-check calls `getCell(nullptr, -1, -1)` through the trampoline.
The function rejects a negative coordinate before touching `this`, so the call
is side-effect free, and a bad trampoline faults there rather than somewhere
unattributable later.

## Preconditions for the rest

1. Build per `README.md`, copy `dinput8.dll` next to `Cube.exe`, start the game.
2. Check `RegionReveal.log` beside the executable. It must read
   `supported build detected; RegionReveal active`. Anything else means no hook
   was installed and the remaining tests are meaningless.

## Functional tests

| | Scenario | Expected |
|---|---|---|
| **A** | Enter an area not visited before, open the map. | Every label inside the area's dotted border is visible without walking over it. |
| **B** | With the map open, look across the border. | The next area stays dark. Reveal must not spill past the dotted line. |
| **C** | Walk into an area outside the history **with the map closed**, then open it. | The area is recorded on entry, not on opening the map; it is revealed and the previous areas stay revealed. |
| **D** | Find a revealed dungeon marker, approach it. | Marker matches a real dungeon. The dungeon is not entered, cleared or flagged complete. |
| **E** | Same for a boss marker. | Boss is alive, undamaged, not credited as defeated. |
| **F** | Same for a city. | City renders as it normally would; NPCs, vendors and quests behave as vanilla. |
| **G** | Quit, reopen the world with and without the DLL. | With it, coverage comes back from `RegionReveal_<world>.visited`. Without it, the map shows only what the player genuinely explored. If mod-revealed markers survive without the DLL, the no-write assumption is wrong — stop and re-examine `docs/REVERSE_ENGINEERING.md`, "Persistence". |
| **H** | Run the DLL against any other Cube World build. | `RegionReveal.log` reports an unsupported build, no hook is installed, the game runs normally and does not crash. |
| **I** | Play ~30 minutes crossing several areas, opening the map often. | No crash, no map corruption, no frame-time degradation. |
| **J** | Open the map in a revealed area. | Ground with relief, water and dotted borders appears around the map's centre within a second or two, including where the player has never been. |
| **K** | Walk towards previewed ground. | Previews turn into real tiles, trees and buildings included, as the game generates them; no cell stays a preview once a real tile exists. |
| **L** | Quit through the menu after a session with previews. | No crash and no `CRASH` line in `RegionReveal.log`; `Save/map_*.db` gains `tile` records only where the player actually was. |

## Run on 2026-10-02

2013-07-20 build, Release `dinput8.dll` built from this tree, existing world
`sdaads`. The game was driven with `SendInput`; the player's position was read
with `ReadProcessMemory` through the same chain the mod uses, located by
scanning for `cube::WorldMap`'s vftable. `Cube.exe` is `DYNAMIC_BASE` and loaded
at `0xE60000` that day, so any VA from the docs has to be rebased first.

| Check | Result |
|---|---|
| Offline: `region_test`, `signature_test` on both builds, `Server.exe` refused | pass |
| Mod loads, hook installs, game runs | `supported build detected; RegionReveal active` |
| A — open the map in a visited region | 24 landmark labels over unexplored ground |
| C, before the fix — cross into `(4102,4101)` with the map closed | **fail**: nothing recorded until the map was opened |
| C, after the fix — cross into `(4103,4101)` with the map closed | `visited NEW region (4103,4101) … 36 regions covered` logged on entry, file rewritten with 4 sorted centres; the new region's label appeared on the next map open |
| C, again, into another storage chunk — `(4104,4101)`, chunk 513 | `visited NEW region (4104,4101) … 41 regions covered`; new labels from column 4106 appeared on the map |
| Back and forth across a boundary | `re-entered` each time, no duplicate centre |
| Restart | `visited: 3 centres, 35 regions covered` loaded before anything was drawn |
| Two worlds in one session, both directions, via the start menu | each world recorded only its own region; nothing from the title screen's placeholder player |
| Unused world with only a v1 file | started empty, as designed |
| G — same world with the DLL removed | 2 labels (the player's region and a city found earlier) against 26 with it; the world list's explored area was unchanged by the mod |
| Clean exit through the menu | no crash; set flushed |
| I, partly — two sessions of about 13 and 15 minutes: walking, swimming, a death and revive, repeated map opens, two world switches | no crash, no error in the log. Private memory rose from 1.2 to 1.6 GB while new terrain loaded; not compared against vanilla |

The first C result is the bug fixed in this round; `docs/BEHAVIOUR.md` has the
cause and the thread evidence behind the fix.

## Run on 2026-10-03

The area-based version, Release `dinput8.dll` built from this tree. The test
character was raised to level 60 and made invulnerable by writing its creature
directly from the test harness, so walking and teleporting between areas did not
end in deaths; that touches the test save only, never the mod.

**2013-07-20, world `saddsa`:**

| Check | Result |
|---|---|
| Offline: `region_test` (63 checks), `signature_test` with all nine signatures on both builds, `Server.exe` refused | pass |
| v2 history loaded | `visited: converted 1 version 2 regions`, then `1 areas recorded`; the area resolved to 3 389 cells |
| A — open the map | every label of the area drawn, out to its dotted border |
| C — teleport into new areas with the map closed | `entered NEW land area …` logged on arrival, the cell appended to the file; re-entering logs `re-entered` and adds nothing |
| J — previews | built nearest the centre first, about 10 ms of work each in slices of at most 5 ms, 70 to 90 live with the default radius; relief, coastlines and border dots visible |
| Map closed | every preview released within five seconds; toggling the map repeatedly returned private memory to the same level each time, so nothing leaks |
| Stress — 16 teleports between areas, map toggled throughout | private memory 1.05 to 1.21 GB, about 1.57 GB of address space in use, no crash |
| L — exit through the menu | clean, no `CRASH` line, no Windows Error Reporting event |

**2013-07-02, a new character and world `oldbuild` (seed 12345):**

| Check | Result |
|---|---|
| Mod loads, all signatures resolve in the live image | `supported build detected; RegionReveal active` |
| Area recorded on entering the world | `entered NEW land area 532384 at cell (32800,32800) in world 'oldbuild'`, 4 499 cells |
| A, J — open the map | Ikokor City, Kurkor Palace, the Catacombs of Segor, ruins, palaces and Damalan Forest labelled; 147 previews built in the first 10 s |
| L — exit through the menu | clean; `map_oldbuild.db` holds 26 `tile` records, all within 3 cells of the player |

This run is what checked the offsets no signature covers — the owner's view cell
and pan, the area's seed and kind — on the older build.

### What the earlier attempts of this round found

Three failures, each fixed before the runs above:

- **Previews only covered a quarter of a cell.** Built 16 x 16; the map scales
  tile voxels by a fixed 8 blocks, so a cell needs the full 32 x 32.
- **Holes in the previewed ground.** The game frees every tile more than 10
  cells from the view centre each second, previews included, and the mod's list
  went stale. Previews are now built within 9 at most and the list is checked
  against the cells on every pass.
- **Crashes.** Two on exit, in `ntdll`, from the mod's own preview thread taking
  a `WorldMap` lock the game had already destroyed; and two while walking, the
  game's own mesh allocation throwing at about 1.75 GB private in a process with
  a 2 GB address space. Everything now runs on the game thread, previews exist
  only while the map is open, and building stops when address space runs low.
  The crash reporter in `src/region_reveal/crash.cpp` is what attributed these.

## Test G is the important one

It is the test that can falsify the central design claim. RegionReveal never
writes to a map cell — it hands the renderer a copy — so a reloaded world must
show the map exactly as vanilla exploration left it. If revealed cells survive a
reload, something is writing through, and the mod is modifying saves after all.

Compare `Save/map_*` before and after by hash to settle it:

```sh
sha256sum "Save/map_"*        # before playing
# play with the mod, save, quit
sha256sum "Save/map_"*        # must be unchanged where no real exploring happened
```

## Crash reports

If the game crashes with the mod loaded, `RegionReveal.log` ends with a line like

```
CRASH (unhandled) code C0000005 at Cube.exe!0x5FC2A1, thread 1234, accessing 00000014
```

followed by the return addresses found on the stack, each as module and address
rebased to the module's preferred base, so they can be looked up in the
disassembly directly. Configuring with `-DREGIONREVEAL_CRASHDUMP=ON` also writes
`RegionReveal_crash.dmp` beside the game for a debugger.

## Instrumenting the open questions

`docs/REVERSE_ENGINEERING.md` lists what static analysis could not settle. These
need a debugger (x32dbg) rather than the mod:

- Breakpoint `WorldMap::discover` and confirm the coordinates track the local
  player, not other creatures. This underpins region detection.
- Watchpoint a cell's `+0x30` byte, walk over it, and confirm the write comes
  from `discover` alone.
- Breakpoint the `Database` set call in `0x6033E2` / `0x603645` / `0x603A00` and
  determine whether chunk cell bytes are what gets written.
- `WorldMap+0x90..0x9C` was suggested here as a possible current-region field.
  **That lead is closed:** those fields are written only by the constructor, to
  `-1`, and by nothing else in the translation unit. A different authoritative
  source is still needed; see `docs/AUDIT.md`.
