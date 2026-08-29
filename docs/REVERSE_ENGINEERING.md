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

Nothing here has been validated dynamically; see "What is still unverified".

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
| `0xA4` | pointer, ctor arg 1 | ctor |
| `0xA8` | ctor arg 3 | ctor |
| `0xAC` | pointer, ctor arg 2; owner object | ctor, and `0x5FBC90` reads `[+0xAC]` |
| `0xB0` | `void* chunkGrid[1024][1024]` | index maths in `0x602440` |
| `0x4000B0` | second `1024×1024` dword grid | `0x601D87`: `shl ebx,0xA; add ebx,0x10002C` |
| `0x8000B8` | int, ctor sets `0` | ctor; purpose unknown |
| `0x8000BC` | int, count of revealed cells | incremented in `0x5FC160`, persisted as `"discovered"` |
| `0x8000C0` | `CRITICAL_SECTION` | `EnterCriticalSection` / `LeaveCriticalSection` in `0x5FC160` |
| `0x8000D8` | `CRITICAL_SECTION` | immediately follows, 0x18 apart |
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
return (char*)chunk + 8 + ((x & 63) * 64 + (y & 63)) * 0x34;
```

So the map is **65536 × 65536 cells**, stored as a sparse **1024 × 1024 grid of
chunks**, each chunk holding **64 × 64 cells of 0x34 bytes** after an 8-byte
header.

A second accessor at `0x6023B0` takes coordinates in units of 8 cells
(bounds `0x2000`), i.e. a coarser view of the same grid. Only the map draw
calls it.

### Map cell fields — CONFIRMED for `0x30`, partial otherwise

`0x34` bytes. Two fields are established:

| Offset | Meaning |
|---|---|
| `0x10` | non-zero for cells the renderer will draw; zero means "nothing here" |
| `0x30` | flags; **bit 0 = revealed on the world map** |

The remaining 50 bytes are not identified and are deliberately left unnamed.

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

### Persistence — CONFIRMED (partially)

`0x5FBC90` builds the path `"Save/map_" + <name from [this+0xAC]+0x94>`, opens a
`cube::Database` (a SQLite `blobs(key TEXT PRIMARY KEY, value BLOB)` store —
confirmed by opening the shipped `Save/worlds.db`), and reads the key
`"discovered"` into `this+0x8000BC`. `0x601F80` writes the same key back.

So the *counter* is persisted under `"discovered"`. Where the per-cell `0x30`
bits are written is **not yet established** — `0x6024D0`, `0x6033E2`, `0x603645`
and `0x603A00` all call the same `Database` get/set pair and are the obvious
candidates.

**This is why RegionReveal never writes to a cell.** Until chunk persistence is
mapped, setting a reveal bit must be assumed to reach the save file.

## Regions

`cube::Region` is a struct (`.?AURegion@cube@@`, vftable `0x71DCCC`), constructed
at `0x5C3AC0`. The constructor fills **4096 entries of 16 bytes** starting at
`+0x18`, then a second array of 4096 dwords ending at `+0x14018`.

4096 = 64 × 64, which is the same shape as a world-map chunk.

**HYPOTHESIS:** one `cube::Region` corresponds to one 64 × 64 map chunk. The
shapes match and nothing contradicts it, but no code path linking a `Region` to
a chunk index has been found. RegionReveal therefore does **not** use
`cube::Region` at all; it works in terms of the chunk, which is a structure the
map itself is built from.

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

- Whether cell `0x30` bits are written to `Save/map_*`.
- What the four `-1` ints at `WorldMap+0x90` track — a "current region" field
  there would be a much better region source than the `discover` hook.
- Whether `discover(x, y)` always refers to the local player.
- Which cell field distinguishes a city from a dungeon from a boss. **No POI
  type field has been identified**, so per-category configuration is not
  implemented rather than faked.
- Everything at runtime: no breakpoint has ever been hit, no cell has been
  observed changing. See `docs/TESTING.md`.
