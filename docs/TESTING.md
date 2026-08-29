# Testing

**None of these have been run.** The analysis behind RegionReveal is static; the
game has never been launched with the mod attached. This document is the
procedure to execute, not a record of results.

Back up `Save/` before any of it.

## Preconditions

1. Build per `README.md`, copy `RegionReveal.dll` and `RegionRevealLauncher.exe`
   next to `Cube.exe`, start through the launcher.
2. Check `RegionReveal.log` beside the executable. It must read
   `supported build detected; RegionReveal active`. Anything else means no hook
   was installed and the remaining tests are meaningless.

## Functional tests

| | Scenario | Expected |
|---|---|---|
| **A** | Enter a region not visited before, open the map. | Markers already generated in that chunk are visible without walking over them. |
| **B** | With the map open, look at the adjacent region. | Still dark. Reveal must not spill past the chunk boundary. |
| **C** | Walk into that adjacent region, reopen the map. | It now reveals; the previous one reverts to only its genuinely explored cells. |
| **D** | Find a revealed dungeon marker, approach it. | Marker matches a real dungeon. The dungeon is not entered, cleared or flagged complete. |
| **E** | Same for a boss marker. | Boss is alive, undamaged, not credited as defeated. |
| **F** | Same for a city. | City renders as it normally would; NPCs, vendors and quests behave as vanilla. |
| **G** | Save, quit, reopen the world. | **Determine and record what happens.** The design intends nothing to persist, so previously auto-revealed cells should be dark again. If they persist, the no-write assumption is wrong — stop and re-examine `docs/REVERSE_ENGINEERING.md`, "Persistence". |
| **H** | Run the DLL against any other Cube World build. | `RegionReveal.log` reports an unsupported build, no hook is installed, the game runs normally and does not crash. |
| **I** | Play ~30 minutes crossing several regions, opening the map often. | No crash, no map corruption, no frame-time degradation. |

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
- Inspect `WorldMap+0x90..0x9C` while crossing a region boundary. If one pair
  tracks the current region, region detection should move to it and the
  `discover` hook can be dropped entirely.
