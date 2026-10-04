# Reveal behaviour

## What is wanted

Entering a named area for the first time shows every label of that area on the
world map — out to the dotted lines that bound it — and nothing else: no
terrain, no labels of areas never entered. Entering further areas adds them.
Areas entered earlier stay revealed, per world, across sessions. The game's own
save is never touched.

## The unit is the named area

The map's dotted lines are the borders between **named areas**, the "Lands of
Asmi" and "Damarok Ocean" the HUD shows. `cube::World` answers "which area is
this block in" with its area lookup (`0x477E10` in the 2013-07-20 build): it
warps the position with noise and returns the nearest of the area centres in
the 3 × 3 storage chunks around it, one centre per chunk — a Voronoi diagram.
The game's tile generator samples the same function to draw the dotted lines.
Typical areas cover 2 000 to 6 000 cells.

Earlier versions of the mod revealed 8 × 8-cell blocks around the player, a
guess at the game's unit that did not match the borders on the map.

### Naming an area

An area is named by the storage chunk that holds its centre. The game keeps
exactly one centre per chunk, in a 1024 × 1024 table at `World+0x4000BC`, so
the chunk is a unique, stable identity, and finding it needs nothing more than
comparing the lookup's result with the table entries it was chosen from. No
field of the area object itself is read.

### Never guessing

The lookup only compares centres the world generator has already produced, and
the generator produces them around the zones it builds near the player
(`0x5E4850` generates the world regions around a zone, `0x5DA280` the area
centres two chunks around each region). Far from where the player has been,
some candidates are missing, and the lookup returns the nearest of the rest —
possibly the wrong area.

So the mod checks first that every centre the lookup will compare exists. If
one is missing, the cell is undecided: it is not revealed, and it is asked again
a second later. Centres are never freed while a world is loaded, so once an
answer is known it stays right.

Observed on 2026-10-03, right after entering a new world: 21 121 of the cells
within 160 of the player were undecided; twenty seconds later, 641 were, all at
the very edge of that square. The area the player stood in was fully decided in
both cases.

## Following the player

The local player is read from the controller that owns the `WorldMap`, which
the detour reaches by subtraction from its own `this`:

```cpp
owner  = (uint8_t*)worldMap - 0x800D44;
player = *(Creature**)(owner + 0x8006D0);
cellX  = (*(int64_t*)(player + 0x10) / 65536) / 256;
```

`+0x8006D0` is corroborated twice: the map renderer reads it at `0x4C98C4`, and
Qube-Loader documents the same field as the local `Creature*`. Every read is
guarded: the pointer must be committed memory and its vftable must point inside
the game's image.

Three conditions keep a wrong area out of the history:

- nothing is recorded while no world name is set — the title screen runs a
  live, unnamed world whose player stands at a placeholder position;
- the player's area must be decided, as above;
- the cell under the player must already be revealed by the game itself, read
  through the trampoline so it is the real cell. The game reveals the cells
  around the local player as they move, so this is proof the player is really
  there, and it costs at most one quarter-second check after a world loads.

Tracking runs on the game's main thread, four times a second. That thread draws
the map, and `WorldMap::discover` keeps calling `getCell` on it with the cells
around the player whether the map is open or not, so an area counts as entered
when the player walks into it, not when the map happens to be open. `getCell`'s
other callers are worker threads and are ignored.

## Revealing labels

The map overlay's draw method calls `getCell` from exactly two places, one per
label pass (`docs/MAP_LABELS.md`). Both draw a label only when the cell's
reveal bit is set. The mod finds those two call sites at startup and compares
each `getCell` call's return address against them:

- any other caller — gameplay, fast travel, the terrain renderer, worker
  threads — gets the real cell;
- a label pass asking for a cell in a revealed area gets a copy of the cell with
  the reveal bit set.

The cell itself is never written. Storage chunks are saved byte for byte, so a
bit written into a cell would reach the save (`docs/REVERSE_ENGINEERING.md`,
"Persistence").

The passes ask for every cell within 32 of the map's centre, twice a frame.
Each answer is cached per cell in a table indexed by the cell's coordinates: the cell's area once decided, and whether it is revealed. The
game's lookup therefore runs once per cell rather than half a million times a
second, and entering a new area updates every cached answer at once without
asking the game again. The copy handed to the label pass is taken afresh on
every call, so it is never stale, and no two cells of the window share a slot,
so a copy stays valid for the whole frame.

## Lifting the game's label limits

The two label passes have limits of their own, independent of the reveal bit
(`docs/MAP_LABELS.md`): the point-of-interest pass is skipped unless the map is
zoomed in, and both walk only the 64 × 64 cells around the map's centre — less
than an area, which is 45 to 80 cells across. By default the mod lifts both, so
entering an area shows all of it:

- the conditional jump that skips the point-of-interest pass becomes a six-byte
  no-op;
- the radius of both passes, written as an 8-bit `± 0x20` in eleven places,
  becomes 96 by default, a 192 × 192 window, so the whole area is in it even
  when the map's centre is on one of its borders. Being an 8-bit value, it can
  go up to 127.

The draw method is the same 6650 bytes in both builds, differing only in
absolute addresses, so the twelve edits sit at the same offsets in each. Every
byte to be replaced is compared with what it must be before anything is written,
and if any differs nothing is: labels just keep the game's limits, and the
reveal works as before. `tests/signature_test.cpp` checks the same bytes in both
`Cube.exe` files on disk. `RegionReveal.ini` sets both (`[labels] any_zoom=0`
keeps the zoom check, `range=32` to `127` sets the radius); the file is read
once, while the DLL loads, because the bytes are only ever written before the
game has started.

The per-cell answer table grows with the radius — its side is the smallest
power of two at least as wide as the window, 256 for the default — so the cells
a pass asks for in one frame still never share a slot.

Cost, measured on 2026-10-04 with the map open, the label passes running only
then, by a build that counted frames and nothing else:

| Label range (cells each way) | 32, the game's | 64 | **96, the default** | 127, the most |
|---|---|---|---|---|
| Map screen | about 80 fps | about 66 fps | about 60 fps | about 49 fps |

The zoom change on its own costs nothing measurable. Most of the range's cost
is the game's own work: both passes visit every cell of the window, 36 864 of
them at 96 against 4 096 at the game's 32. Gameplay with the map closed is
unaffected.

## Persistence

`RegionReveal_<world>.visited`, beside the game, keeps one cell per area
entered: the cell the player stood on when entering it — a sorted list of
packed `(x, y)` keys, format version 3. A place rather than an area identity,
because an area can only be identified once the generator has produced its
surroundings, and the file has to make sense before that. On load every stored
cell is queued and turned into its area as soon as that area can be decided;
until then it is not revealed.

Version 2 files, which stored the 8 × 8-cell blocks of the earlier design, are
converted on load to the cell at each block's middle and rewritten at once.
Version 1 files, keyed on 64 × 64-cell storage chunks, are rejected.

The world key is the name the game itself uses, a `std::string` at
`World+0x94` that `0x5FBC90` turns into `"Save/map_" + name + ".db"`. The game
lower-cases it. Any printable ASCII name is accepted except characters that are
not allowed in a file name or could leave the game folder (`\ / : * ? " < > |`),
up to 64 characters.

Every choice fails towards revealing less:

- the header carries a magic, a version, the grid size and the world name, and
  any mismatch — or trailing bytes, or keys out of order — leaves the set empty
  rather than partly filled;
- writes go to a temporary file that replaces the real one in a single rename,
  so an interrupted write cannot damage the previous copy;
- the file is written when an area is entered for the first time or an old
  file is converted, and at no other time, so there is nothing to lose when the
  game exits.

A damaged file can lose history. It cannot invent it, and it cannot reveal an
area the player has not been to.

## Start-up and lifetime

`Cube.exe` imports `dinput8.dll`, so the mod's `DllMain` runs on the game's main
thread while the imports load, before the entry point. Everything is set up
there — about 10 ms of pattern scanning — so the five patched bytes are written
before any game thread exists to execute them. Once the hook is in, the DLL pins
itself so it can never be unloaded while the game calls into it. The real
`dinput8.dll` is loaded on the first `DirectInput8Create` call rather than from
`DllMain`.
