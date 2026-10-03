# Reveal behaviour

## What is wanted

Entering a named area for the first time reveals all of it on the world map:
every label the game has for it and the shape of its ground, out to the dotted
lines that bound it. Entering further areas adds them. Areas entered earlier stay
revealed. Areas never entered, and areas belonging to another world, stay
hidden. The game's own save is never touched.

## What was wrong in the first version

The mod tracked a single region in two integers, overwritten on every
`WorldMap::discover` call, and revealed a cell only on an exact match. Crossing
into a new region made the previous one stop matching, so it reverted to whatever
the player had genuinely explored. Current-region only, with no memory.

The region also came from `discover`, whose three callers include two that
iterate lists — so the coordinate could belong to another creature. With
persistence that flaw would have got worse: a wrong region would no longer be a
transient glitch, it would be written down.

## What is built now

### The reveal unit is the named area

Earlier rounds revealed a 5 x 5 block of "gameplay regions", 8 x 8 cells each,
around every region entered. That was a guess at the game's unit, and it was the
wrong one: the map's dotted lines are not on that grid at all. They are the
borders between **named areas**, the "Lands of Asmi" and "Damarok Ocean" the HUD
shows, and they are what the game itself draws a border along.

`cube::World` answers "which area is this block in" with the function at
`0x477E10` (2013-07-20): it warps the position with noise (`0x5EEFA0`) and
returns the nearest of one area centre per storage chunk, a Voronoi diagram. An
area object carries a name seed at `+0x14`, distinct per area and stable across
sessions, and a kind at `+0x18` that is negative for oceans. The game's tile
generator samples the same function every 32 blocks and stores a dot wherever two
neighbouring samples differ; those dots are the dotted lines. Sampling a 193 x 193
window put typical areas at 2 000 to 6 000 cells, and one lookup at about half a
microsecond.

So the mod tracks the area, by seed, and reveals every cell of it.

### The position comes from the local player

`WorldMap::getCell` is a `WorldMap` method, and the `WorldMap` is constructed in
place inside its owner at `+0x800D44` — `lea ecx, [ebx + 0x800D44]` sits directly
before the constructor call at `0x45A5B2`. So the detour recovers the owner by
subtraction from its own `this`, with no global and no second hook:

```cpp
owner  = (uint8_t*)worldMap - 0x800D44;
player = *(Creature**)(owner + 0x8006D0);
cellX  = (*(int64_t*)(player + 0x10) / 65536) / 256;
area   = areaAt(world, cellX * 256 + 128, cellY * 256 + 128);  // the cell's centre
```

`+0x8006D0` is corroborated twice: the map renderer itself reads it at
`0x4C98C4` through `[MapOverlayWidget + 0x160]`, and Qube-Loader documents the
same field as the local `Creature*`. The position conversion mirrors what the
game does at `0x488430` before calling `discover`.

The read is guarded: the pointer must be committed memory and its vftable must
point inside the loaded image. Anything else yields an invalid cell and reveals
nothing. The area lookup returns null while the area's storage chunk has not been
generated, which also reveals nothing.

The guard is not enough on its own. The title screen runs a live, unnamed world
whose player stands at a placeholder position — block `(8396928, 8396928)`,
region `(4100, 4100)`, observed on 2026-10-02 — so a valid-looking chain does not
mean the player is in a world. Two further conditions close that: nothing is
recorded while no world name is set, and an area is recorded only once the game
has revealed the cell the player stands on, read through the trampoline so it is
the real cell. Gameplay keeps revealing the cells around the player through
`discover`, so the condition is met almost at once; entering a world, it was
observed to cost one 250 ms recheck.

**The `discover` hook is gone**, along with its byte signature. One hook remains.

### The area is tracked whether or not the map is open

The first build of this read the region only from inside the map draw, so a
region counted as visited only if the map happened to be open while the player
stood in it. Walking through one with the map closed — the normal case — left it
out of the history for good. Observed on 2026-10-02: the player crossed into
`(4102,4101)` and the `.visited` file did not change until the map was opened.

The same `getCell` detour now also tracks from gameplay calls, filtered by
thread rather than by caller. Logging every distinct caller and thread in a live
session showed:

| Caller (return address) | Thread |
|---|---|
| `0x4CA4F3`, map draw marker pass | game thread |
| `0x5FC181`, inside `WorldMap::discover` | game thread |
| `0x469A0C` | worker |
| `0x46ACF4` | worker |

`discover` keeps running with the local player's position, on the same thread as
the map draw, so tracking only on that thread keeps every read and write of the
visited set on one thread and the detour still needs no lock. The thread is
taken from `DllMain` when loaded as the `dinput8.dll` proxy — an import-table
dependency is initialised on the main thread before the entry point — and is
also learnt from the map renderer, which covers the injector, whose `DllMain`
runs on a remote thread.

The chain is re-read at most every 250 ms. An area spans dozens of cells of 256
blocks, so the player cannot cross one faster than that, and the per-frame
callers would otherwise walk the pointer chain for an answer that cannot have
changed.

### Visited areas persist per world

`src/region_reveal/visited.cpp` keeps, for each area the player entered, the
cell they were standing on when they entered it — a sorted list of packed
`(x, y)` cell keys, format version 3 — in `RegionReveal_<world>.visited` beside
the game.

A cell rather than the area's seed, because an area can only be looked up while
its storage chunk is resident. On load every stored cell is queued, and resolved
to its area's seed as soon as the game has generated that chunk; until then the
area simply is not revealed yet. Storing a place also keeps the file independent
of how areas are numbered.

Version 2 files, which kept 8 x 8-cell region centres, are converted on load:
each region becomes the cell at its centre, which lies in the area the player
entered there, and the file is rewritten as version 3 on the next flush.
Observed on 2026-10-03: the `saddsa` history converted to one area of 3 389
cells. Version 1, keyed on 64 x 64-cell storage chunks, is still rejected.

The world key is the name the game itself uses: a `std::string` at
`[WorldMap+0xAC] + 0x94`, which `0x5FBC90` concatenates as
`"Save/map_" + name + ".db"`. Using the same identity the game uses means the
mod's file and the game's save always agree about which world is loaded.

Defensive choices, all of which fail towards revealing *less*:

- the name must be alphanumeric plus `-` and `_`, at most 64 characters, so a
  crafted name cannot write outside the game folder;
- the header carries a magic, a version, the grid dimensions and the world name,
  and **any** mismatch — wrong magic, wrong version, wrong size, different world,
  short read, keys out of order — leaves the set empty rather than partly filled;
- writes go to a temporary and are moved into place with `MOVEFILE_REPLACE_EXISTING`,
  so an interrupted write cannot truncate the real file;
- the set is written only when an area is newly added, and once more on unload.

A corrupt file can therefore lose knowledge. It cannot invent it, and it cannot
reveal an area the player has not been to.

### Revealing a whole area

The map asks `getCell` for every cell it draws, every frame, so the answer has to
be a lookup rather than an area query. Around the map's view centre — the game's
view cell at `owner+0x2BC` plus the player's pan at `owner+0x1000E4C`, divided by
256, which is exactly how the game's own map data worker computes it — the mod
keeps an 81 x 81 bitmap of the cells that belong to a revealed area. It is
recomputed when the centre moves four cells, when an area is added, and every two
seconds so newly generated chunks resolve, a few rows per frame, and published
whole into a double buffer that the detour reads without a lock.

81 cells covers the 64 x 64 window the label pass walks with room for a pan. A
cell in the bitmap is answered with a copy that has the reveal bit set; the real
cell is never written, for the reason in `docs/REVERSE_ENGINEERING.md`,
"Persistence".

### Terrain previews

The reveal bit does not draw terrain: the map draws a cell's tile image whenever
it exists, and the bit only changes the colour of the placeholder drawn where no
tile exists. So the ground of a revealed area appears only if its cells have
tiles, and the game builds a tile only from a zone it has generated, which means
only near the player.

The mod therefore builds the missing tiles itself, from the world generator's
own height function, with the game's own tile image code, and only in memory.
`docs/POI_AND_TERRAIN.md` has the pipeline and the reverse engineering; in short:

- a preview is built only for a loaded cell of a revealed area that has no tile
  and no tile in the save, nearest the map's centre first;
- 34 x 34 heights are sampled — the 32 x 32 voxel columns plus a ring for the
  slopes — and turned into voxels: grass shaded by height, sand at the shore,
  rock on steep rises, snow high up, water at sea level;
- the area-border dots are computed the way the game's tile generator computes
  them, so the dotted lines run across previews too;
- the image is attached under the lock the map renderer holds, and the game owns
  it from then on: it destroys the preview itself when it builds the real tile
  for that cell, or when it unloads tiles far from the view.

All of it runs on the game thread from the `getCell` detour, in slices of at most
5 ms with the map open and 2 ms with it closed, at most one slice per 15 ms. A
preview takes about 10 ms of work, so two or three frames.

Previews exist only while the map is open, plus five seconds so closing it
briefly does not throw them away, and only within 6 cells of the view centre by
default (`RegionReveal.ini`, `[preview] radius`, at most 9). Both limits come from
the process, not from taste:

- the game frees every tile more than 10 cells from the view centre once a
  second (`0x5FBED0`), so anything further out is built only to be destroyed;
- `Cube.exe` is 32-bit and not large-address-aware. It runs with a few hundred MB
  of its 2 GB address space left, and walking costs another 200 MB or so as
  zones generate. Two crashes in testing were the game's own mesh allocation
  failing at about 1.75 GB private. Each preview costs about 230 KB, mostly
  mesh, so building pauses when less than 400 MB of address space is left and
  previews are released below 250 MB.

An earlier version built previews on a thread of its own and crashed on exit
twice, in `ntdll`, from that thread taking a `WorldMap` lock the game had already
destroyed. The game stops its own workers before tearing the map down and knew
nothing of the mod's. On the game thread there is nothing to race: when the game
stops calling `getCell`, the mod stops running.

### Labels are not filtered on `+0x10`

The draw method makes two label passes. The one at `0x4C9831` draws a cell's
point of interest, the type byte at `+0x10`, and needs both that byte and the
reveal bit; the one at `0x4CA4FB` tests **only** the reveal bit and takes its
label from the 8 x 8 block's `0x68` record. The mod once skipped cells whose
`+0x10` was zero, which hid every label of the second pass. That filter is gone.

## What still limits the outcome

- Terrain shows around the map's centre, not across the whole area at once; the
  labels do cover the whole area.
- A label exists only once the world generator has produced the storage chunk
  carrying it. A distant corner of a large area fills in as the game reaches it.
- Preview colours come from height and slope, not from the game's biomes.

## What has been observed

Watched in play on 2026-10-02, 2013-07-20 build, for the region-based version:
boundary crossings with the map closed and open, a restart, two worlds in one
session, and an A/B against the same world with the DLL removed.

On 2026-10-03, for the area-based version: on 2013-07-20, area entry and
re-entry, the v2 conversion, previews filling in and being released, and a
stress run of teleports between areas with the map toggled; on
2013-07-02, a new character and world, the area recorded on entry and the whole
of "Lands of Ikokor" revealed with previews. `docs/TESTING.md` has the results.
