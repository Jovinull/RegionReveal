# Labels and terrain on the world map

How the world map draws its labels and its ground, and how RegionReveal fills in
both for an area the player has entered. Addresses are `Cube.exe` 2013-07-20
(Alpha 0.1.1), ImageBase `0x400000`; `docs/TARGET_BUILD.md` maps them to the
2013-07-02 build.

An earlier revision of this document called the pass at `0x4C9831` the terrain
pass and concluded that terrain could not be revealed without making the game
generate, and save, thousands of tiles. Both were wrong, and are corrected below.

## Labels: two passes in the overlay — CONFIRMED

`cube::MapOverlayWidget`'s draw method (`0x4C9680`) draws labels in two passes
over the cells around the view centre:

| Pass | Site | Gate | Draws |
|---|---|---|---|
| Points of interest | `0x4C9831` | `cell[0x10] != 0` **and** `cell[0x30] & 1` | the cell's own point of interest: type byte at `+0x10` (1 = city, drawn white), level at `+0x18`, coloured against the player's |
| Landmarks | `0x4CA4FB` | `cell[0x30] & 1` **only** | the label of the 8 x 8-cell block's `0x68` record |

Neither pass draws ground. RegionReveal used to skip cells whose `+0x10` was
zero, on the reasoning that the renderer would skip them anyway — true for the
first pass, false for the second, and it hid every landmark label. That filter
was removed.

### Landmark records live outside the ZoneTile — CONFIRMED

The landmark pass does not read the type from the cell. It calls `0x6023B0` with
the coordinates divided by 8 and reads `+0x18` of what comes back:

```
index = 0x800 + (x & 7) * 8 + (y & 7)
return chunk + index * 0x68
```

`0x800 * 0x68 = 0x34000`, which is exactly where the chunk constructor builds
**64 objects of `0x68` bytes** (`0x5FAE00`, loop of 64 with stride `0x68`). So a
storage chunk carries an 8 x 8 grid of these records, one per 8 x 8 block of
cells, and `+0x18` selects what is drawn. `0` and `0xA` are skipped
(`0x4CA54B`, `0x4CA553`).

Four values were pinned by counting a screenshot against the save: 1 = City,
2 = Mountain, 3 = Forest, 4 = Lake. The rest of the name table did not match
what the map drew and was withdrawn; see `src/game/landmarks.hpp`. **Boss** has
not been separated from anything, and no claim is made about it.

## Terrain: the tile image — CONFIRMED

The ground is drawn by `WorldMap::render` (`0x5FC1B0`), not by the overlay's
passes. The overlay calls it with a radius pushed as a literal, `push 0x10`, so
it covers 16 cells around the view centre. For each cell it draws the **tile
image** at `ZoneTile+0x08` whenever there is one, **whether or not the reveal bit
is set**. The bit only changes the colour of the placeholder square drawn where
there is no tile.

That is the whole reason revealing cells never revealed ground: almost no cell of
an area has a tile.

### The ZoneTile, as far as it is known

| Offset | Meaning | Basis |
|---|---|---|
| `0x00` | vftable `0x71DFBC`, `cube::ZoneTile` | constructor `0x5FB7F0` |
| `0x04` | the tile's lowest voxel layer, in 8-block units | written by the tile loader with `+0x08`; the renderer places the image by it |
| `0x08` | tile image, or null | loader writes it; renderer draws it |
| `0x10`–`0x1F` | point of interest: type byte at `+0x10`, level at `+0x18` | first label pass |
| `0x20` | `std::list` of area-border dots `{x, y, z}` in blocks, 20-byte nodes | tile generator pushes them (`0x601EB0`); renderer draws them |
| `0x28` | dirty: rebuild the image from the zone | set by the zone manager |
| `0x2C` | fade-in countdown, `250` when a tile appears | loader |
| `0x30` | flags: bit 0 revealed, bit 1 a `tile` record exists in the save | `discover`; loader |

### The tile image — CONFIRMED

A `0x60`-byte object: constructor `0x4E6A20(renderer, 0)`, resize
`0x4E75C0(w, h, d)` allocating `w * h * d * 3` zeroed bytes at `+0x30`, mesh
build `0x4E7870`, and a virtual deleting destructor in slot 0. The renderer
handed to it is `WorldMap+0xA4`. Voxels are RGB at `((z * h + y) * w + x) * 3`,
black meaning empty; each voxel stands for an 8 x 8 x 8 cube of blocks, and the
map scales by that fixed 8, so a cell's tile is always 32 x 32.

### Where tiles come from — CONFIRMED

- **The loader** (worker thread `0x469590`) loads `reg` and `land` records around
  the map's view centre and, ten at a time, the nearest cells within 10 that
  lack a tile, through `0x603A00`. That function decodes the cell's `tile` record
  from the save if there is one; otherwise it generates the image **from the
  resident zone**, the 3D terrain the game builds for play, and writes it back
  to the save (`0x604E3C`). With no zone, it produces nothing.
- **The zone manager** (worker thread `0x46A8A0`) generates zones within 3 of the
  players and unloads them beyond 4, marking cells dirty so their images are
  rebuilt. A zone takes about 1.4 s to generate.
- **The unloader** (`0x5FBED0`, once a second) frees every tile more than 10
  cells from the view centre (`0x6022D0`) and every `land` record more than 8
  chunks away.

So real ground exists only for cells near where the player has been, and is shown
only within 10 cells of the map's centre.

## Areas and the dotted lines — CONFIRMED

`cube::World`'s area lookup, `0x477E10(world, blockX, blockY)`, warps the
position with noise (`0x5EEFA0`) and returns the nearest of one area centre per
storage chunk (`World+0x4000BC`): a Voronoi diagram of named areas. An area has a
name seed at `+0x14` and a kind at `+0x18`, negative for oceans.

The tile generator samples the lookup every 32 blocks, every fourth voxel, and
pushes a border dot wherever a sample's area differs from its neighbour's on
either axis. The renderer draws those dots: they are the dotted lines.

## How RegionReveal draws the ground

The game's height function, `0x5C5E20(world, blockX, blockY, zone)`, is analytic:
it computes terrain height from noise and needs no zone to be resident. The mod
builds the missing tiles from it:

1. **Pick** the cell nearest the view centre, within the preview radius, that is
   loaded, belongs to a revealed area, has no tile and has no `tile` record.
2. **Sample** 34 x 34 heights at voxel centres, the 32 x 32 columns plus a ring,
   a few rows per frame.
3. **Synthesize** the voxels (`src/region_reveal/preview_tile.cpp`): each column
   is filled from its top voxel down to just above its lowest neighbour, so slopes
   show sides rather than gaps; colour by height and slope - grass from low to
   high, sand within 2 blocks of sea level, rock where a neighbour rises 12 or
   more, snow from 380 - and water as one layer at sea level, deeper blue with
   depth. A small hash jitter keeps flat ground from reading as one slab.
4. **Border dots** come from the area lookup on the generator's 32-block grid.
5. **Build** the image with the game's constructor, resize and mesh builder, so
   it is allocated by the game's allocator and the game can destroy it.
6. **Attach** it under `WorldMap+0x8000D8`, the lock the renderer and loader
   hold, after checking again that the cell still wants it: set `+0x04`, `+0x08`,
   the fade at `+0x2C`, and the dots.

From then on the game treats it as any tile. When a zone is generated over the
cell, the zone manager's dirty flag makes the game rebuild the image from real
terrain, freeing the previous image through its destructor - read from the code,
not yet watched happening to a preview. The unloader frees it like any tile. The
mod keeps a list of what it attached, checks it each pass against the cell, and
frees its own previews when the view moves away or the map closes.

The save is never involved. A preview never sets the saved-tile bit, and the
game writes a `tile` record only when it generates one from a zone. Checked on
2026-10-03: after a session that built more than 150 previews, `map_oldbuild.db` held 26
`tile` records, all within 3 cells of where the player stood.

### Cost and limits

About 10 ms of work per preview, spread over two or three frames, and about
230 KB each, mostly mesh: the game's builder emits a face for every exposed voxel
side, column bottoms included, exactly as for real tiles. In a 32-bit process
that is not large-address-aware and already runs close to its 2 GB address space,
that is what sets the limits in `docs/BEHAVIOUR.md`: previews only while the map
is open, 6 cells around the centre by default, building paused under 400 MB of
free address space.
