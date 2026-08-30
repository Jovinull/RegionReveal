# POI markers and terrain generation

Findings from the milestone-4 and milestone-5 investigation. Addresses are
`Cube.exe` 2013-07-20 (Alpha 0.1.1), ImageBase `0x400000`.

## The map is drawn in two passes — CONFIRMED

This was missed earlier and it changes what the mod should do.

| Pass | Site | Gate |
|---|---|---|
| Terrain | `0x4C9831` | `cell[0x10] != 0` **and** `cell[0x30] & 1` |
| Markers | `0x4CA4FB` | `cell[0x30] & 1` **only** |

The marker pass never looks at `+0x10`. RegionReveal used to skip cells whose
`+0x10` was zero, on the reasoning that the renderer would skip them anyway —
true for the terrain pass, false for the marker pass. That filter was hiding
markers and has been removed.

It also explains the first live report exactly: dungeon and castle names appeared
while the terrain stayed dark. Those were the ~25 cells per region that happened
to have `+0x10` set; every other marker stayed hidden because of the mod's own
filter.

## Marker data lives outside the ZoneTile — CONFIRMED

The marker pass does not read the type from the cell. It calls `0x6023B0` with
the coordinates divided by 8 and reads `+0x18` of what comes back.

`0x6023B0` computes:

```
index = 0x800 + (x & 7) * 8 + (y & 7)
return chunk + index * 0x68
```

`0x800 * 0x68 = 0x34000`, which is exactly where the chunk constructor builds
**64 objects of `0x68` bytes** (`0x5FAE00`, loop of 64 with stride `0x68`). So a
region carries an 8x8 grid of these records, one per 8x8 block of cells, and
`+0x18` selects what is drawn.

Observed filtering in the renderer: `0` and `0xA` are both skipped
(`0x4CA54B`, `0x4CA553`).

### POI taxonomy — incomplete on purpose

| Raw value | Observed entity | Static evidence | Runtime evidence | Confidence |
|---|---|---|---|---|
| `record+0x18 == 0` | nothing drawn | `test eax, eax / je skip` at `0x4CA54B` | — | CONFIRMED |
| `record+0x18 == 0xA` | skipped by the renderer | `cmp eax, 0xA / je skip` at `0x4CA553` | — | CONFIRMED |
| other values | a marker of some kind | the code past the guards dereferences the value as a pointer (`[eax+0xD]`, `[eax+0x10]`) | — | STRONG EVIDENCE |
| city / dungeon / castle / boss | — | — | — | **UNKNOWN** |

**No category has been named.** `+0x18` is dereferenced after the guards, so it
is more likely a pointer to a descriptor than a small enum, and no value has been
correlated with a known in-game location. Naming any of these now would be a
guess, so the table stops here. Boss in particular has not been separated from
dungeon, and there is no evidence either way yet.

What *can* be said: the mod reveals cells, and the marker pass draws whatever
those records hold. Whichever categories the game represents this way are
covered; whichever it does not are not reachable through this path at all.

## Terrain generation — the chain, and where it stops

`0x603A00` handles `tile<x>_<y>`. Its shape:

```
key = "tile" + x + "_" + y
if (Database::get(key) && ...)      // 0x4498D0 at 0x603D18
    decode the stored image         // header: version, tag, 32, 32, ...
else
    goto 0x603F32                   // the generate path
...
Database::set(key, ...)             // 0x4499C0 at 0x604E3C
```

So the game **does** generate a tile when one is absent, and **writes it back to
the save**. Two consequences follow immediately.

### The generator needs the cell's terrain resident — STRONG EVIDENCE

The generate path opens with:

```
0x603F32  test esi, esi
0x603F34  jne  0x603F51     ; only generates when esi is non-null
          ; otherwise: clean up and return without producing anything
0x603F51  mov  eax, [esi + 0xA8]
0x603F57  add  eax, 0x10
          ; nested loop, 0x100 x 0x100, stride 0x2000 per row and 0x20 per column
```

256 x 256 iterations over 32-byte records is one record per **block**: a map cell
spans 256 blocks per axis, and the resulting image is 32 x 32, so each pixel
averages an 8 x 8 patch. That is the terrain being sampled, not invented.

The array is `2 MiB` per cell. A region is 4096 cells, so a whole region's source
data would be about **8 GiB** if it were all resident at once. It plainly is not:
the `esi` guard is there precisely because the data exists only for cells the
game has loaded, which is a small neighbourhood around the player.

The identity of the object `esi` points at is **UNKNOWN** — it was not traced to a
class. What it holds is clear from the access pattern; what it is called is not.

### Classification: Result B, with a hard practical edge

- **The generator exists and is the game's own code.** Not a reimplementation.
- **It can be asked for one cell at a time**, and `0x603A00` takes `(x, y)`.
- **But it produces nothing for a cell whose terrain is not loaded**, and terrain
  residency is driven by player proximity.
- **And every tile it does produce is written to `Save/map_*.db`.**

So revealing a whole region's terrain is not a matter of calling one function
4096 times. It would require forcing the terrain for 4096 cells to be generated
and resident — that is the world generator, at roughly gigabytes of working set —
and it would mutate the save with 4096 new `tile` records.

That is **Result B**: technically reachable through original game code, with side
effects that are not hideable. Separating the visual result from the persistence
would mean detouring the `Database::set` at `0x604E3C` to drop `tile` writes,
which is possible, but the memory and CPU cost of the generation itself is not
avoidable that way.

**Not attempted in this round**, because the experiment the milestone asks for —
generate one unexplored cell without moving the player — needs the `esi` object
to be reachable for a cell the player is not near, and no path to that has been
found. That is the next experiment, not a conclusion.

## What this means for the requirement

"Full region map" splits cleanly in two:

- **markers** — reachable, and now unfiltered;
- **terrain** — reachable in principle, blocked in practice by residency, and
  costly plus save-mutating if forced.
