# Labels on the world map

How the world map draws its labels, and why revealing them leaves the terrain
alone. Addresses are `Cube.exe` 2013-07-20 (Alpha 0.1.1), ImageBase `0x400000`;
`docs/TARGET_BUILD.md` maps them to the 2013-07-02 build.

## Two label passes — CONFIRMED

`cube::MapOverlayWidget`'s draw method (`0x4C9680`, 6650 bytes) calls
`WorldMap::getCell` from exactly two places. Each is a loop over the 64 × 64
cells around the map's centre — the player's position plus the map's pan,
`± 0x20` cells on each axis — and each draws a label only for a revealed cell:

| Pass | `getCell` call / return | Gate | Draws |
|---|---|---|---|
| Points of interest | `0x4C981E` / `0x4C9823` | `cell[0x10] != 0` and `cell[0x30] & 1` | the cell's own point of interest: type byte at `+0x10` (1 = city, drawn as its districts), level at `+0x18`, coloured against the player's |
| Landmarks | `0x4CA4EE` / `0x4CA4F3` | `cell[0x30] & 1` | the landmark of the cell's 8 × 8-cell block: castles, palaces, catacombs, ruins, mountains, canyons, valleys, the city's name |

The draw method has no other `getCell` call, which is what lets the mod reveal
labels without changing anything else the map draws.

### The point-of-interest pass needs a closer zoom

The first pass runs only when a float at `controller+0x1C4` is above 2.0
(`comiss` against `0x745E10`, `jbe` past the whole loop at `0x4C97A9`). That
value grows as the map zooms in: a diagnostic build read 1.00 at the default
zoom, 1.69 a few wheel notches in and 4.86 fully in, and the pass ran only in
the last case. City districts and dungeon entrances are therefore a close-zoom
feature of the game itself — unless the mod's `any_zoom` option, on by default,
turns that jump into a no-op.

### Both passes walk 32 cells each way

The radius is an 8-bit displacement or immediate, `± 0x20`, written eleven
times: the first and last row and column of each pass, and the bounds each pass
recomputes at the end of a row or cell. Offsets from the start of the draw
method, the same in both builds:

| Pass | Offsets |
|---|---|
| Points of interest | `+0x135`, `+0x138`, `+0x151`, `+0x157`, `+0xD92`, `+0xDAA` |
| Landmarks | `+0xDFF`, `+0xE02`, `+0xE24`, `+0xE30`, `+0x1967` |

The mod's `range` option sets each to the chosen radius, 96 by default and
at most 127, the largest an 8-bit displacement holds. The map data worker keeps
storage chunks loaded three chunks — 192 cells — around the map's centre, so
every cell of the wider window is there to ask about; a diagnostic build saw
all 36 864 of the default window loaded.

### Landmark records — CONFIRMED

The landmark pass does not read the type from the cell. For each revealed cell
it calls `0x6023B0` with the coordinates divided by 8, which returns one of the
64 records of `0x68` bytes a storage chunk keeps after its cells
(`chunk + 0x34000`, one per 8 × 8 block of cells), and reads `+0x18`:

- `0` and `0xA` are skipped (`0x4CA54B`, `0x4CA553`);
- each record is drawn once per frame — the pass keeps a `std::set` of records
  already drawn;
- and only when `0x5FA4C0(record, position)` is positive for the revealed cell.

So a landmark appears as soon as one revealed cell of its block passes that
test, which is why a landmark on an area's border can show from either side.

Four values were pinned by counting a screenshot against the save: 1 = City,
2 = Mountain, 3 = Forest, 4 = Lake. The rest of the game's name table did not
match what the map drew, and **boss** has not been separated from anything.

## Points of interest arrive after the world loads

A diagnostic survey of the area the player spawned in found no cell with a
point of interest at the moment the world finished loading, and 25 of them
twenty seconds later. The game fills `ZoneTile+0x10` in as it generates the
zones around the player, so the close-zoom labels near a fresh spawn appear a
few seconds after the landmark names.

## Why the terrain is not revealed

The ground is drawn by `WorldMap::render` (`0x5FC1B0`), not by the overlay's
label passes. It draws a cell's tile image (`ZoneTile+0x08`) whenever one
exists, revealed or not; the reveal bit only changes the colour of the
placeholder square drawn where there is no tile. Tiles exist only for cells the
game has generated from a zone near the player, so revealing cells never
revealed ground. It is not one of the two label passes, so the mod does not
change what it draws.

An earlier version built approximate terrain tiles from the world generator's
height function and attached them in memory. It worked, but only within ten
cells of the map's centre — the game frees every tile further out once a second
(`0x5FBED0`) — and each tile cost about 230 KB in a 32-bit process already near
its 2 GB address space. It was removed in favour of revealing labels only.
