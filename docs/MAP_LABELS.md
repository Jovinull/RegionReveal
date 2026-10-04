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

The record is the game's place-and-mission record. Its layout matches the
`MissionData` structure the cuwo server project recovered for the same game:
the place's origin (two 64-bit positions at `+0x00`), its size (`+0x10`), the
place category at `+0x18` — what the landmark pass draws — then the place
item, the name generator, the area level, and a mission: monster race and level,
state and progress. That makes `0x5FA4C0` very likely the test of the cell's
position against the place's origin and size.

Four categories were pinned by counting a screenshot against the save: 1 = City,
2 = Mountain, 3 = Forest, 4 = Lake. The rest of the game's name table did not
match what the map drew.

**Bosses** are the missions in those records. The map draws them as crossed
swords whether or not the place is revealed: the same icon sits in the same
spot in the A/B screenshots with and without the mod. The mod neither shows nor
hides them.

### How a landmark name is drawn — CONFIRMED

After picking the record, the pass builds the name into a `std::wstring`
(`0x4E5590`) and chooses its colour from the place's level against the
player's (`0x43CA60` on both): white for a place below your level, cyan around
it, red above it, white always for a city. Then it draws the text twice through
the same text-drawing function — `call 0x639B30` at `0x4CAEB2` for the
outline and at `0x4CAF9D` for the text, offsets `+0x1832` and `+0x191D` of the
draw method — with the font name, the text, six floats, the colour and three
more arguments. All along the record stays in the stack slot `[ebp-0x354]`.

### The place record is also the boss mission — CONFIRMED

The record's layout matches cuwo's `MissionData`: origin at `+0x00`/`+0x08`,
category at `+0x18`, area level at `+0x24`, and a mission from `+0x2C`. A record
has a mission when the dword at `+0x34` is non-zero, and its state byte at
`+0x41` is 0 before the fight, 1 during it and 2 once it is done. The game keys
the boss's fate on it: at `0x60CA22`, state 2 sets the boss creature's health
to zero, so a defeated boss stays dead.

## How RegionReveal marks names

Both text draws of the landmark pass are redirected to a small stub that reads
the record from `[ebp-0x354]` — the draw method's frame is still live — and asks
the mod what to draw:

- the place's mission is done (state 2): the name is drawn green with ` †`;
- otherwise, if the game itself has revealed the cell at the place's origin —
  the player has been there — the name gets ` •`;
- otherwise it is drawn as the game chose.

The stub hands a different string to the game's function, never edits the
game's own, and the colour is changed only in the draw method's local copy,
which it rebuilds for every name. `•` and `†` are both glyphs of the map font
(`resource1.dat`, a TrueType font with 230 glyphs; it has no check mark or
star). The two call sites and the stack slot are checked in both builds before
anything is redirected.

## City districts and the zoom

With the zoom check gone, the point-of-interest pass draws a city's districts
at every zoom, and zoomed out they pile up on top of the city's name. So while
the zoom value is at or below the game's own threshold of 2.0, the mod hides
cells whose point of interest is a district (type 1) from that pass by handing
it a copy without the reveal bit — the same trick as the reveal, the other way
round. Dungeon entrances and other points of interest still show. `far_districts=1`
turns this off.

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
