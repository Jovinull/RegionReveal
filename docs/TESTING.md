# Testing

## Offline

Two programs, built with the mod, need no running game:

```sh
build/Release/region_test.exe
python tests/run_tests.py build/Release/signature_test.exe "<2013-07-20>/Cube.exe" "<2013-07-02>/Cube.exe"
```

- `region_test` — 68 checks over the logic that needs neither the game nor a
  `Cube.exe`: which chunks the area lookup compares for a cell, including the
  world's edges; the visited file (round trip, exact bytes on disk, version 2
  conversion, world isolation, accepted and refused world names, every kind of
  damaged file); the revealed-area set; and the per-cell answers the label
  passes get, with a fake world that can refuse to decide (an undecided cell is
  not revealed and is asked again after a second, a decided one is looked up
  once, a new area updates cached answers, copies carry the bit and never alias
  within a 64 × 64 window).
- `signature_test` — every signature must match exactly once, at the address
  recorded in `docs/TARGET_BUILD.md`, in each supported build, and the twelve
  bytes the label options replace must be exactly the expected ones.
  `run_tests.py` identifies each executable by SHA-256 first. `Server.exe` is
  refused: the `getCell` and map-overlay signatures do not match in it.

## In the game

Back up `Save/` first, then copy `dinput8.dll` beside `Cube.exe`.

`RegionReveal.log`, beside the game, is the record of what the mod did:

| Line | Meaning |
|---|---|
| `supported build detected; RegionReveal active (set up in N ms)` | the hook is in; anything else at start-up means the game runs unmodified |
| `labels: points of interest at every zoom, 96 cells around the map's centre` | the label options as applied; the defaults, or what `RegionReveal.ini` asked for |
| `visited: N areas recorded in world 'name'` | a world was opened and its file read |
| `entered new area (X,Y) at cell (x,y) in world 'name'` | an area was entered for the first time and written to the file |
| `back in area (X,Y) ...` | the player moved into an area already recorded |

The area is named by the storage chunk of its centre, so two lines with the
same `(X,Y)` are the same area.

With the default options, points of interest (city districts, dungeon entrances)
show at every zoom and labels reach 96 cells from the map's centre. Set
`any_zoom=0` and `range=32` in `RegionReveal.ini` to compare with the game's own
limits.

| | Scenario | Expected |
|---|---|---|
| **A** | Enter an area not visited before, open the map. | Every landmark inside the area's dotted border is labelled; zoomed in, its points of interest too. The ground is unchanged. |
| **B** | Pan the map across the dotted border. | The neighbouring area's labels are absent, except a landmark whose 8 × 8 block straddles the border. |
| **C** | Walk into another area **with the map closed**, then open it. | `entered new area` is logged on arrival, not when the map opens; both areas are labelled. |
| **D** | Approach a revealed dungeon. | It is not entered, cleared or flagged complete. |
| **E** | Same for a boss. | Alive, undamaged, not credited as defeated. |
| **F** | Same for a city. | NPCs, vendors and quests behave as vanilla. |
| **G** | Quit, reopen the world with and without the DLL. | With it, `visited: N areas` and the same labels come back. Without it, the map shows only what was genuinely explored. |
| **H** | Run the DLL against any other Cube World build. | The log reports an unsupported build, no hook is installed, the game runs normally. |
| **I** | Play about 30 minutes across several areas, opening the map often. | No crash, no map corruption, no frame-time change. |

### Test G is the important one

It is the test that can falsify the central design claim. RegionReveal never
writes to a map cell — it hands the label pass a copy — so a reloaded world
without the DLL must show the map exactly as vanilla exploration left it. If
labels the mod revealed survive without the DLL, something is writing through.

## Run on 2026-10-03, label-only version

2013-07-20 build. A test-only build of the same source added counters to the
detour: for every cell the two label passes asked about, whether the game had
revealed it, whether the mod did, and whether its area was recorded, undecided
or another one. Never shipped; the counters are what make "every label shown"
checkable rather than a matter of counting text on a screenshot.

| Check | Result |
|---|---|
| Offline: `region_test`, `signature_test` on both builds, `Server.exe` refused | pass |
| Start-up | `RegionReveal active (set up in 6 ms)` to `10 ms`, inside `DllMain` |
| New world `MARK TEST`, seed 777 — a name with a space, which the previous version refused | stored as `mark test`, file written |
| Area recorded on spawning | `entered new area (512,512) at cell (32800,32800)`; the area has 3 610 cells, x 32777–32845, y 32773–32834 |
| A, default zoom — landmark pass | 33 blocks with a landmark in the 64 × 64 window: 32 shown by the mod, 1 already explored, **0 hidden inside the area, 0 shown outside it** |
| A, zoomed in — point-of-interest pass | 25 points of interest in the window, all 25 shown by the mod, **0 hidden, 0 leaked**; a survey of the whole area found exactly those 25 (four city cells, the rest dungeon-like types with levels) |
| What the map showed | Durala City with its Pet, Crafting and Adventurer districts; Likuron and Narden Castle, Krorok Palace, Catacombs of Damaion, Varmi and Duradara, Ruins of Duragor and Varsel, Rock of Arurior and Narla, Ikoria, Krokor and Kursel Mountains, Gegor Canyon, Ikorok Valley |
| Terrain | unchanged: real tiles around the player, placeholders elsewhere |
| Points of interest after loading | none at the moment the world loaded, all 25 twenty seconds later |
| Undecided cells within 160 of the player | 21 121 at load, 641 twenty seconds later; none inside the player's area either time |
| Exit through the menu | clean, no error |

### The label options at 64 cells, same session and world

| Check | Result |
|---|---|
| Start-up | `labels: points of interest at every zoom, 64 cells around the map's centre` (the range at the time) |
| Window | x 32736–32863, y 32736–32863: 16 384 cells, all loaded |
| Default zoom, point-of-interest pass | runs; city districts drawn. 100 points of interest in the window: the area's 25 shown, the other 75 belong to other areas and stay hidden; 0 hidden inside the area |
| Landmark pass | 138 blocks with a landmark: 35 shown by the mod, 1 already explored, 102 of other areas hidden, 0 hidden inside the area, 0 leaked. One more than before: a block on the area's east border, now inside the window |
| Zoomed out | more of the area's landmarks on screen (Narrior Tree, Catacombs of Asgor, Likusel Palace); a city's districts overlap |
| `RegionReveal.ini` with both options off | the window back to 64 × 64 and the point-of-interest pass skipped at the default zoom |

## Run on 2026-10-04, range 96

Same world and test build with counters. The player was moved with a test
harness that writes the local player's position, so a border could be reached
in seconds.

| Check | Result |
|---|---|
| Frame rate with the map open, by range | 80, 66, 60 and 49 fps at 32, 64, 96 and 127 cells, measured by a build that only counted frames |
| **B** — standing 2 cells inside the area's west border, map open | window x 32683–32874: the whole area is in it, up to its east border at 32845. The area's 25 points of interest shown; 149 of other areas hidden. 255 landmark blocks: 34 shown by the mod, 2 explored by the game, 219 of other areas hidden. **0 hidden inside the area, 0 leaked.** The dotted border visible on screen |
| **C** — crossing the border with the map closed | `entered new area (511,512) at cell (32767,32800)` logged on arrival: "Aruka Ocean". On opening the map, its labels (Krotar Island, Narka Island, Asden Palace, Varden Castle, Kurron Mountains…) appear and the first area's stay. Counters: 44 points of interest and 67 landmarks shown, 0 hidden, 0 leaked |
| **G** — restart with the DLL | `visited: 2 areas recorded in world 'mark test'`, `back in area (511,512)`; the same 44 and 67 labels |
| **G** — same world without the DLL | not one label on the map at the default zoom, where the mod showed 44 points of interest and 67 landmarks; the ground identical. Nothing the mod showed was saved |
| 2013-07-02, world `oldbuild` | label options applied (`96 cells`), `visited: 1 areas`, `back in area (512,512)`; Ikokor City with its districts at the default zoom; 25 points of interest and 38 landmarks shown, 0 hidden, 0 leaked |
| Crashes | none in either log |

**Still to do:** D, E and F (dungeons, bosses and cities left untouched by
gameplay — the mod changes nothing outside the two label passes), H against a
live unsupported build, and I, the 30-minute session.

## Earlier runs

- **2026-10-02, 8 × 8-block version, 2013-07-20.** Region tracking with the map
  closed was found broken and fixed; a restart restored the history; two worlds
  in one session stayed separate. A/B against the same world with the DLL
  removed: 26 labels with it, 2 without — the player's own spot and a city
  found earlier — so nothing reached the save.
- **2026-10-03, area version with terrain previews, both builds.** Area
  tracking, the version 2 conversion and a 16-teleport stress run behaved; the
  previews themselves were removed afterwards (`docs/MAP_LABELS.md`). That run
  also exercised, on 2013-07-02, the offsets no signature covers.

## Open questions for a debugger

`docs/REVERSE_ENGINEERING.md` lists what static analysis could not settle. These
need x32dbg rather than the mod:

- a write watchpoint on a cell's `+0x10`, to find who fills in points of
  interest;
- a breakpoint on `0x5FA4C0`, to see what decides whether a landmark draws for
  a given cell;
- the `Database` set calls in `0x605420`, to locate the reveal bit inside a
  saved `reg` record.
