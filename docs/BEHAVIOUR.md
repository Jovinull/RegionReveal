# Region behaviour

## What is wanted

Entering a region for the first time marks it as discovered by RegionReveal —
permanently, from the mod's point of view. Its markers become visible. Entering
further regions adds them. Regions visited earlier stay revealed. Regions never
visited, and regions belonging to another world, stay hidden. The game's own
save is never touched.

## What was wrong before

The mod tracked a single region in two integers, overwritten on every
`WorldMap::discover` call, and revealed a cell only on an exact match. Crossing
into a new region made the previous one stop matching, so it reverted to whatever
the player had genuinely explored. Current-region only, with no memory.

The region also came from `discover`, whose three callers include two that
iterate lists — so the coordinate could belong to another creature. With
persistence that flaw would have got worse: a wrong region would no longer be a
transient glitch, it would be written down.

## What is built now

### The region comes from the local player

`WorldMap::getCell` is a `WorldMap` method, and the `WorldMap` is constructed in
place inside its owner at `+0x800D44` — `lea ecx, [ebx + 0x800D44]` sits directly
before the constructor call at `0x45A5B2`. So the detour recovers the owner by
subtraction from its own `this`, with no global and no second hook:

```cpp
owner  = (uint8_t*)worldMap - 0x800D44;
player = *(Creature**)(owner + 0x8006D0);
cellX  = (*(int64_t*)(player + 0x10) / 65536) / 256;
region = cellX >> 3;    // a gameplay region is 8 x 8 cells
```

`+0x8006D0` is corroborated twice: the map renderer itself reads it at
`0x4C98C4` through `[MapOverlayWidget + 0x160]`, and Qube-Loader documents the
same field as the local `Creature*`. The position conversion mirrors what the
game does at `0x488430` before calling `discover`.

The read is guarded: the pointer must be committed memory and its vftable must
point inside the loaded image. Anything else yields an invalid region and reveals
nothing.

The guard is not enough on its own. The title screen runs a live, unnamed world
whose player stands at a placeholder position — block `(8396928, 8396928)`,
region `(4100, 4100)`, observed on 2026-10-02 — so a valid-looking chain does not
mean the player is in a world. Two further conditions close that: nothing is
recorded while no world name is set, and a region is recorded only once the game
has revealed the cell the player stands on, read through the trampoline so it is
the real cell. Gameplay keeps revealing the cells around the player through
`discover`, so the condition is met almost at once; entering a world, it was
observed to cost one 250 ms recheck.

**The `discover` hook is gone**, along with its byte signature. One hook remains.

### The region is tracked whether or not the map is open

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

The chain is re-read at most every 250 ms. A region is 8 cells of 256 blocks, so
the player cannot cross one faster than that, and the per-frame callers would
otherwise walk the pointer chain for an answer that cannot have changed.

### Visited regions persist per world

`src/region_reveal/visited.cpp` keeps the regions the player actually entered —
a sorted list of packed `(x, y)` keys, format version 2 — in
`RegionReveal_<world>.visited` beside the game. What the map reveals is derived
from that list: every region within two of a visited one, a 5 x 5 survey. Only
the visited centres are written, so the survey radius can change without
invalidating a file. The version 1 format keyed on 64 x 64-cell storage chunks,
which is not what the game calls a region, and is rejected rather than
converted.

The world key is the name the game itself uses: a `std::string` at
`[WorldMap+0xAC] + 0x94`, which `0x5FBC90` concatenates as
`"Save/map_" + name + ".db"`. Using the same identity the game uses means the
mod's file and the game's save always agree about which world is loaded.

Defensive choices, all of which fail towards revealing *less*:

- the name must be alphanumeric plus `-` and `_`, so a crafted name cannot write
  outside the game folder;
- the header carries a magic, a version, the grid dimensions and the world name,
  and **any** mismatch — wrong magic, wrong version, wrong size, different world,
  short read — leaves the set empty rather than partly filled;
- writes go to a temporary and are moved into place with `MOVEFILE_REPLACE_EXISTING`,
  so an interrupted write cannot truncate the real file;
- the set is written only when a region is newly added, and once more on unload.

A corrupt file can therefore lose knowledge. It cannot invent it, and it cannot
reveal a region the player has not been to.

### Markers are no longer filtered on `+0x10`

The draw method makes two passes. The terrain pass at `0x4C9831` needs both
`cell[0x10] != 0` and the reveal bit; the marker pass at `0x4CA4FB` tests **only**
the reveal bit and takes its icon from a separate record. The mod used to skip
cells whose `+0x10` was zero, which was correct reasoning about the terrain pass
and wrong about the marker pass — it hid markers the game would have drawn. That
filter is gone. See `docs/POI_AND_TERRAIN.md`.

## What still limits the outcome

Terrain. A cell's terrain is a 32 x 32 image the game generates from a resident
256 x 256 array of block columns and then writes into the save. The generator is
guarded on that array being present, and it is present only near the player. So a
visited region shows its **markers**, not its map.

`docs/POI_AND_TERRAIN.md` has the chain and why this is Result B rather than
Result C.

## What has been observed

Watched in play on 2026-10-02, 2013-07-20 build: boundary crossings with the map
closed and open, a restart, two worlds in one session, and an A/B against the
same world with the DLL removed. `docs/TESTING.md` has the results.
