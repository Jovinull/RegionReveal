# Testing

Loading, hooking and the region behaviour have been verified in the running game.
Tests A, B, C, G and part of I were run on 2026-10-02 by driving the game with
synthetic input and reading the player's position out of the process; see
"Run on 2026-10-02" below. D, E and F, and H against a live unsupported build,
are still open.

Back up `Save/` before any of it.

## Already verified

Run against isolated copies of both installs, never the originals:

| Check | Result |
|---|---|
| `Cube.exe` starts unmodified in this environment | runs, windowed 1280×720 |
| Proxy loads before the entry point, 2013-07-20 build | `supported build detected; RegionReveal active` |
| Same, 2013-07-02 build | `supported build detected; RegionReveal active` |
| Signatures resolve in the **live mapped image** | all three, both builds |
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
| **A** | Enter a region not visited before, open the map. | Markers of the 5 x 5 regions around it are visible without walking over them. |
| **B** | With the map open, look past the survey. | Still dark. Reveal must not spill past two regions from a visited one. |
| **C** | Walk into a region outside the history **with the map closed**, then open it. | The region is recorded on entry, not on opening the map; its survey is added and the previous regions stay revealed. |
| **D** | Find a revealed dungeon marker, approach it. | Marker matches a real dungeon. The dungeon is not entered, cleared or flagged complete. |
| **E** | Same for a boss marker. | Boss is alive, undamaged, not credited as defeated. |
| **F** | Same for a city. | City renders as it normally would; NPCs, vendors and quests behave as vanilla. |
| **G** | Quit, reopen the world with and without the DLL. | With it, coverage comes back from `RegionReveal_<world>.visited`. Without it, the map shows only what the player genuinely explored. If mod-revealed markers survive without the DLL, the no-write assumption is wrong — stop and re-examine `docs/REVERSE_ENGINEERING.md`, "Persistence". |
| **H** | Run the DLL against any other Cube World build. | `RegionReveal.log` reports an unsupported build, no hook is installed, the game runs normally and does not crash. |
| **I** | Play ~30 minutes crossing several regions, opening the map often. | No crash, no map corruption, no frame-time degradation. |

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
