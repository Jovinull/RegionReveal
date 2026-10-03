# Reverse engineering notes

Scope is deliberately narrow: enough to know where the world map keeps its
"revealed" state and where to intervene. This is not a decompilation.

**All addresses below are `Cube.exe`, build 2013-07-20, ImageBase `0x400000`.**
See `docs/TARGET_BUILD.md` for the equivalent addresses in the 2013-07-02 build.

Findings are tagged:

- **CONFIRMED** — read directly out of the instruction stream, unambiguous.
- **STRONG EVIDENCE** — consistent across several independent sites, but not
  observed at runtime.
- **HYPOTHESIS** — plausible reading, not yet supported enough to build on.

Some of this has since been checked against a running game and against a real
save file; `docs/AUDIT.md` records which claims survived an attempt to falsify
them and which did not.

## Method

`Cube.exe` ships with **complete MSVC RTTI**, which is unusual and made this far
easier than it should have been. `tools/cwtool.py rtti` recovers 169 type
descriptors, 176 complete object locators and 149 vftables with their class
hierarchies. Wollay's real class names are therefore known, not guessed:

```
cube::World          cube::WorldMap        cube::MapOverlayWidget
cube::Zone           cube::ZoneTile        cube::Region        (struct)
cube::WorldInfo      cube::LandscapeTile   cube::Chunk         (struct)
cube::Dungeon        cube::House           cube::Field
cube::Creature       cube::Database        cube::GameController
```

The engine underneath lives in `plasma::` (`Widget`, `Node`, `Object`,
`D3D9Engine`, `Shape`, …) with a small `abstr::` reflection layer.

From there the trail was: string constants → data xrefs → enclosing functions →
call graph.

## The world map

### `cube::WorldMap` — CONFIRMED

Constructor at `0x5FAE40`; it writes vftable `0x71DFCC`, which is the RTTI
vftable for `.?AVWorldMap@cube@@`.

| Offset | Meaning | Basis |
|---|---|---|
| `0x00` | vftable `0x71DFCC` | ctor writes it |
| `0x90`–`0x9C` | four ints, ctor sets each to `-1` | ctor; purpose unknown |
| `0xA0` | byte, ctor sets `0` | ctor |
| `0xA4` | renderer, ctor arg 1; handed to every tile image's constructor | ctor; tile loader |
| `0xA8` | ctor arg 3 | ctor |
| `0xAC` | `cube::World*`, ctor arg 2; world name at `+0x94` | ctor; `0x5FBC90` reads the name, the tile generator calls the area lookup on it |
| `0xB0` | `void* chunkGrid[1024][1024]` | index maths in `0x602440` |
| `0x4000B0` | second `1024×1024` dword grid | `0x601D87`: `shl ebx,0xA; add ebx,0x10002C` |
| `0x8000B8` | int, ctor sets `0` | ctor; purpose unknown |
| `0x8000BC` | int, count of revealed cells | incremented in `0x5FC160`, persisted as `"discovered"` |
| `0x8000C0` | `CRITICAL_SECTION` | `EnterCriticalSection` / `LeaveCriticalSection` in `0x5FC160` |
| `0x8000D8` | `CRITICAL_SECTION` guarding cells and tiles | held by `WorldMap::render`, the tile loader and the unloader; storage chunks are freed under it |
| `0x8000F0` | `cube::Database` | ctor call `0x449380`; used for all map persistence |
| `0x8000F8` | int, ctor sets `1` | ctor |
| `0x8000FC` | vector-like `{begin,end,cap}` | ctor zeroes three dwords |

`0xB0 + 0x400000 = 0x4000B0` and `0x4000B0 + 0x400000 = 0x8000B0`: the two grids
account exactly for the 8 MiB gap before the fields at `0x8000B8`.

The instance is reached from `cube::MapOverlayWidget` as
`*(widget + 0x160) + 0x800D44` (seen at `0x4C9817` and `0x488446`).

### Addressing — CONFIRMED

`cube::WorldMap::getCell(int x, int y)` at `0x602440`, `__thiscall`, `ret 8`:

```
if (x < 0 || y < 0 || x >= 0x10000 || y >= 0x10000) return nullptr;
chunk = this->chunkGrid[(x >> 6) * 1024 + (y >> 6)];
if (!chunk) return nullptr;
return (char*)chunk + ((x & 63) * 64 + (y & 63)) * 0x34;
```

So the map is **65536 × 65536 cells**, stored as a sparse **1024 × 1024 grid of
chunks**, each chunk holding **64 × 64 cells of 0x34 bytes** starting at the chunk itself.
There is no header — an earlier revision of this document claimed eight bytes of
one, which was wrong. The chunk constructor `0x5FAE00` runs the MSVC vector
constructor iterator over `0x1000` elements of size `0x34` at the chunk base,
then builds 64 objects of `0x68` at `chunk + 0x34000`; `0x34000 + 64*0x68 =
0x35A00`, exactly the allocation at `0x6032CB`.

A second accessor at `0x6023B0` takes coordinates in units of 8 cells
(bounds `0x2000`), i.e. a coarser view of the same grid. Only the map draw
calls it.

### The map cell is `cube::ZoneTile` — CONFIRMED

The cell constructor at `0x5FB7F0` writes vftable `0x71DFBC`, which the RTTI
scan resolves to `.?AVZoneTile@cube@@`. The cell is a named game class, not an
anonymous record.

`0x34` bytes. The fields RegionReveal relies on:

| Offset | Meaning |
|---|---|
| `0x04` | the tile's lowest voxel layer, in 8-block units |
| `0x08` | the cell's tile image, or null — this is the terrain |
| `0x10` | point-of-interest type byte, `0x18` its level |
| `0x20` | `std::list` of area-border dots |
| `0x2C` | tile fade-in countdown |
| `0x30` | flags; **bit 0 = revealed on the world map**, bit 1 = a `tile` record exists in the save |

`docs/POI_AND_TERRAIN.md` has the evidence for each. An earlier revision guessed
`0x10` was a tile handle; it is not, and the tile is at `0x08`.

### Revealing — CONFIRMED

`cube::WorldMap::discover(int x, int y)` at `0x5FC160`:

```
EnterCriticalSection(this + 0x8000C0);
cell = getCell(x, y);
if (cell && !(cell[0x30] & 1)) {
    cell[0x30] |= 1;
    ++this->revealCount;        // +0x8000BC
}
LeaveCriticalSection(this + 0x8000C0);
```

This is the game's own "the player has now seen this cell" call, and it is the
single writer of the reveal bit.

### The visibility gate — CONFIRMED

`cube::MapOverlayWidget` overrides exactly one virtual, slot 1, at `0x4C9680`
(6650 bytes). Its per-cell test reads:

```
0x4C9817  add  ecx, 0x800D44          ; this->game->worldMap
0x4C981E  call 0x602440               ; getCell(x, y)
0x4C9829  test eax, eax    / je skip  ; no cell
0x4C9831  cmp  byte [eax+0x10], 0 / je skip
0x4C9844  test byte [eax+0x30], 1 / je skip     <-- the reveal gate
          ... draw ...
```

This is the equivalent of the hypothetical `MapShouldDisplayPOI(poi)`: it is an
**inline bit test**, not a call, so it cannot be hooked as a function. That is
what shapes the design in `docs/../README.md`.

This is the first of two **label** passes. The ground is drawn elsewhere, by
`WorldMap::render` at `0x5FC1B0`, from the tile image at `+0x08` and without
looking at the reveal bit except to colour placeholders; see
`docs/POI_AND_TERRAIN.md`.

### Persistence — CONFIRMED (partially)

`0x5FBC90` builds the path `"Save/map_" + <name from [this+0xAC]+0x94>`, opens a
`cube::Database` (a SQLite `blobs(key TEXT PRIMARY KEY, value BLOB)` store —
confirmed by opening the shipped `Save/worlds.db`), and reads the key
`"discovered"` into `this+0x8000BC`. `0x601F80` writes the same key back.

So the *counter* is persisted under `"discovered"`. The rest of the schema is now
known from reading a real save: `0x603230` loads and `0x605420` stores
`reg<x>_<y>` (the chunk), `0x6024D0` handles `land<x>_<y>` and `0x603A00`
handles `tile<x>_<y>`. Chunks **are** persisted, whole — `0x605420` serialises
across `0x34000` bytes — so a written reveal bit would reach the save file.

**This is why RegionReveal never writes to a cell** — and the reason is now
established rather than precautionary: chunk persistence is mapped, and it copies
the cell bytes wholesale, so a reveal bit written into a cell would be saved.

## Areas — CONFIRMED

The dotted lines on the map bound named areas, and `cube::World` is what knows
them. These are the functions RegionReveal calls; all are found by signature in
both builds (`src/game/signatures.cpp`).

| Function | 2013-07-20 | Notes |
|---|---|---|
| area lookup `(world, blockX, blockY)` | `0x477E10` | noise warp `0x5EEFA0`, then the nearest of one centre per storage chunk at `World+0x4000BC`; returns null while that chunk is not generated. Area: name seed `+0x14`, kind `+0x18` (negative = ocean) |
| terrain height `(world, blockX, blockY, zone)` | `0x5C5E20` | `ret 0xC`, float in `ST0`; analytic, needs no resident zone |
| tile image constructor `(renderer, 0)` | `0x4E6A20` | `0x60`-byte object, `ret 8` |
| tile image resize `(w, h, d)` | `0x4E75C0` | RGB voxels at `+0x30`, dimensions at `+0x44..+0x4C` |
| tile image mesh build | `0x4E7870` | |
| border-dot `push_back` | `0x601EB0` | the list at `ZoneTile+0x20` |
| `std::list` clear | `0x46F870` | one body folded across element types |

The tile image is allocated with the game's own `operator new` from
`msvcr110.dll`, so the game can free it with its virtual destructor.

Where the map is looking lives in the `WorldMap`'s owner: a view cell at
`owner+0x2BC` (two ints) and the player's pan at `owner+0x1000E4C` (two floats,
in blocks). The map data worker computes its centre as cell + pan / 256, and so
does the mod.

The rest of the tile pipeline — loader `0x469590`, `0x603A00`, zone manager
`0x46A8A0`, unloader `0x5FBED0` — is in `docs/POI_AND_TERRAIN.md`.

## Regions

`cube::Region` is a struct (`.?AURegion@cube@@`, vftable `0x71DCCC`), constructed
at `0x5C3AC0`. The constructor fills **4096 entries of 16 bytes** starting at
`+0x18`, then a second array of 4096 dwords ending at `+0x14018`.

4096 = 64 × 64, which is the same shape as a world-map chunk.

**CONFIRMED, by a different route:** the game's own save names map chunks
`reg<x>_<y>`. A live save holds `reg509_509` through `reg515_515` while the
player's chunk was `(512, 512)`, and `32768 >> 6 == 512`. The 64 × 64 map chunk
*is* what Cube World calls a region.

The `cube::Region` **class** is a separate matter: far larger, and no code path
links it to a chunk index. RegionReveal does not use it. The shape coincidence
that prompted the original hypothesis is noted and left at that.

### Player position → map cell — STRONG EVIDENCE

At `0x488430`, feeding `discover`:

```
push 0x10000 ; push hi ; push lo ; call __alldiv   ; position / 65536
cdq ; and edx, 0xFF ; add eax, edx ; sar eax, 8    ; floor(/ 256)
```

World position is a 64-bit fixed-point value with 16 fractional bits; the block
coordinate is `pos >> 16`, and one map cell spans 256 blocks. The same shape
appears at `0x48CFF7`.

`discover` is called from three functions (`0x4882E0`, `0x4886E1`, `0x48CF07`),
two of which iterate a list (strides `0x68` and `0x100`). **It is therefore not
proven that every `discover` call carries the local player's position** — in
multiplayer it may well be driven per creature. This is the weakest link in the
mod and is called out in the README's limitations.

## What is still unverified

- Which serialized byte of a `reg` record holds the reveal bit. The record format
  resisted decoding and a falsification test failed; see `docs/AUDIT.md`.
- Who writes `ZoneTile+0x10` — the world generator, by every indication, but the
  write was never traced.
- `WorldMap+0x90..0x9C` is a **closed** lead: written only by the constructor,
  to `-1`, and by nothing else in the translation unit.
- Whether `discover(x, y)` always refers to the local player.
- The full point-of-interest and landmark taxonomy. `+0x10` type 1 is a city
  and four landmark values are pinned; dungeon and boss are not separated, so
  per-category configuration is not implemented rather than faked.

Most of the rest has since been exercised in the running game; see
`docs/TESTING.md`.
