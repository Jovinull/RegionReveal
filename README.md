# Region Reveal

Reveals the whole named area you walk into on Cube World Alpha's world map:
every city, dungeon, landmark and point of interest in it, and the shape of its
terrain.

> **Working on both Alpha builds, checked in the running game on 2026-10-03.**
> Entering "Lands of Ikokor" lit up the 4 499 cells of that area at once - Ikokor
> City, palaces, catacombs, ruins, a forest - and the map drew real relief,
> water and the dotted area borders over ground the player had never been near.
> Walking into the next area does the same for that one. See
> [`docs/TESTING.md`](docs/TESTING.md).

## What it does

The map's dotted lines are the borders between the game's **named areas** -
"Lands of Asmi", "Damarok Ocean" - which `cube::World` lays out as a warped
Voronoi of one centre per storage chunk. An area typically covers 2 000 to 6 000
map cells, roughly 45 to 80 cells or 11 000 to 20 000 blocks across.

When the player enters an area for the first time, RegionReveal:

- **reveals every cell of that area** to the map, so every label the game has
  for it - cities, palaces, dungeons, ruins, catacombs, mountains, forests - is
  drawn, however far from the player it is;
- **draws its terrain** where the game has none yet: around the map's centre,
  each cell of a revealed area without a real tile gets a preview built from the
  world generator's own height function - real relief, coast and water, rock on
  cliffs, snow on peaks - and the area borders are drawn over it as dotted lines.
  Real tiles always win: a cell that has one, or has one in the save, never
  gets a preview.

Areas stay revealed across sessions, kept per world in
`RegionReveal_<world>.visited` beside the game. Areas never entered, and areas
of another world, stay hidden.

It does this **without writing to the game's save**. The reveal is a copy of the
cell with the bit set, handed only to the map renderer; previews live in memory
and are never written, so `Save/` is exactly what vanilla play would leave.

## What it does not do

Quests, bosses, loot, fast travel and world generation are untouched. Fast
travel and every other gameplay caller keep seeing the real map.

It cannot invent points of interest. A label appears only once the game has
generated the storage chunk that carries it; a far corner of a huge area fills
in as the world generator reaches it.

Preview terrain is an approximation: the relief is the game's, the colours are
not. Trees, buildings and the exact biome palette appear only when the game
builds the real tile.

## Supported builds

Both Alpha builds are supported. Functions are located by byte signature at
startup, so no absolute address is hard-coded.

The 2013-07-20 build is the one the Alpha modding ecosystem targets — the Cube
World Mod Launcher v1.5 accepts only its exact file size and calls it **Alpha
0.1.1**, and Qube-Loader's hard-coded offsets resolve in it and not in the
2013-07-02 build. Details in [`docs/QUBE_COMPATIBILITY.md`](docs/QUBE_COMPATIBILITY.md).

| Build | `Cube.exe` SHA-256 | Size |
|---|---|---|
| 2013-07-20 (primary) | `84a7a132a84d4282338e7ea45a1940d32066d64e39cbafed8f2418d5a6dc30bf` | 3 885 568 |
| 2013-07-02 | `a4eeb3606ad2b82e4c9b3d0db6f9472ffa6e89a4d56084a9114ff1fb3c812699` | 3 878 400 |

Anything else is refused: if a signature is missing — or matches twice — no hook
is installed at all, a line is written to `RegionReveal.log`, and the game runs
unmodified. Full audit in [`docs/TARGET_BUILD.md`](docs/TARGET_BUILD.md).

## Building

Requires Visual Studio 2022 Build Tools with the 32-bit MSVC toolset and a
Windows SDK. The game is PE32, so the mod must be 32-bit.

```sh
cmake -B build -A Win32
cmake --build build --config Release
```

Produces `dinput8.dll`, plus `RegionReveal.dll` and `RegionRevealLauncher.exe`
for the injection route, and the two test programs.

## Installing

Copy **`dinput8.dll`** into the Cube World Alpha folder, next to `Cube.exe`, and
start the game normally.

`Cube.exe` imports one function from `dinput8.dll`, and Windows searches the
executable's own folder before the system directory, so the mod loads before the
game's entry point and forwards that call to the real `dinput8.dll` in
`System32`. **One file is added; nothing is renamed, replaced or written to.**
Delete it and the install is stock again.

There is also `RegionReveal.dll` plus `RegionRevealLauncher.exe`, the same mod
loaded by injection instead. It works, but antivirus blocks the launcher on
sight — `CreateRemoteThread` into another process is the textbook injection
pattern — so the proxy above is the supported route.

Expect the antivirus to object to the proxy too. The mod patches five bytes of
`Cube.exe` in memory, which is genuinely the same technique a malicious hook
uses; see `docs/TOOLING.md` for what the DLL does and does not link against.

Back up your `Save/` folder before playing with any mod, this one included.

### Settings

Optional. A `RegionReveal.ini` beside `Cube.exe` is re-read every two seconds
while the game runs:

```ini
[preview]
enabled=1   ; 0 frees every terrain preview and builds no more
radius=6    ; cells around the map centre that get a preview, 0 to 9
```

Without the file the defaults above apply. Area reveal itself has no switch:
removing the DLL is the off switch.

## How it works

One 5-byte detour, on `cube::WorldMap::getCell(x, y)`, found by signature, and
everything else runs from inside it on the game thread.

- **Tracking.** Gameplay keeps calling `getCell` around the player. Four times a
  second the detour reads the local player's cell, asks the game's own area
  lookup which area it belongs to, and records a cell of every area entered for
  the first time.
- **Revealing.** Around the map's view centre the mod keeps a bitmap of which
  cells belong to a revealed area, recomputed a few rows a frame. When the map
  renderer asks for such a cell, it gets a copy with the reveal bit set; the
  real cell is never written.
- **Terrain.** While the map is open, cells of revealed areas that have no tile
  get a 32 × 32 voxel image built from `cube::World`'s height function, allocated
  and meshed by the game's own tile code and attached to the cell in memory, with
  the area-border dots the game's tile generator would have computed. About
  10 ms of work each, spread over frames in slices of at most 5 ms.

The return-address check matters: `getCell` has nine callers, and only the map
renderer should see the modified answer. Gameplay code asking the same question
keeps getting the truth, which is what stops the mod from leaking into fast
travel or progression.

Details and evidence: [`docs/BEHAVIOUR.md`](docs/BEHAVIOUR.md),
[`docs/POI_AND_TERRAIN.md`](docs/POI_AND_TERRAIN.md) and
[`docs/REVERSE_ENGINEERING.md`](docs/REVERSE_ENGINEERING.md).

## Status

Verified in the running game on 2026-10-03:

- on both builds, entering an area reveals all of it, labels and terrain, and
  the area is recorded on entry whether or not the map is open;
- on 2013-07-20, the next area is added when the player crosses into it, a
  history from the earlier region-based format is converted on first load,
  previews are released when the map closes with no memory left behind, and a
  stress run of 16 teleports between areas with the map toggled throughout held
  private memory between 1.05 and 1.21 GB and exited cleanly.

Earlier rounds are in [`docs/TESTING.md`](docs/TESTING.md) and
[`docs/AUDIT.md`](docs/AUDIT.md), including the A/B that proved the reveal never
reaches the save.

## Known limitations

- **Terrain shows near the map's centre only.** Previews cover 6 cells around
  the map's view centre by default, 9 at most, and only while the map is open.
  The game frees every tile further than 10 cells from that centre each second,
  previews included, and `Cube.exe` is a 32-bit process that is not
  large-address-aware: it already runs within a few hundred MB of its 2 GB limit,
  and each preview costs about 230 KB. Labels are not limited this way. Building
  pauses below 400 MB of free address space and previews are released below
  250 MB.
- **Preview colours are approximate.** Grass, sand, rock, snow and water are
  picked from height and slope, not from the game's biome data.
- **Boss is not covered separately.** Whatever represents a boss on the map has
  not been investigated; no claim is made either way.
- **No per-category toggles**, deliberately: the reveal unit is the area, and
  only four landmark types are identified with confidence (City, Mountain,
  Forest, Lake).

## Licence

MIT — see [`LICENSE`](LICENSE). No game asset or executable is redistributed
here, and no third-party mod code is included.
