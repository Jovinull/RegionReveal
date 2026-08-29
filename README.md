# Region Reveal

Reveals cities, dungeons, bosses and other points of interest when entering a
region in Cube World Alpha.

> **Status: loads and hooks cleanly in the running game; the reveal itself is
> still untested.** See [Status](#status) before using it.

## What it does

Cube World Alpha's world map draws a cell only when bit 0 of that cell's flags
is set — the bit the game sets as you walk over the terrain. RegionReveal makes
the map *report* that bit as set for the whole 64 × 64-cell chunk the player is
currently in, so the cities, dungeons and other markers already generated in that
chunk appear at once instead of one square at a time.

It does this **without writing to the game's memory or save files**. The detour
returns a copy of the cell with the bit flipped, and only to the map renderer;
the real cell is never touched, so nothing is persisted and nothing outside the
map ever sees a different answer.

Neighbouring chunks are unaffected — walk into one and it reveals in turn.

## What it does not do

Quests, bosses, loot, fast travel and world generation are untouched. The mod
only changes the answer to "should this map cell be drawn", and only while the
map is being drawn.

It cannot invent points of interest. A marker appears only if the game has
already generated it into that map cell; anything the world generator has not
produced yet stays absent.

## Supported builds

Both Alpha builds are supported. Functions are located by byte signature at
startup, so no absolute address is hard-coded.

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

Produces `RegionReveal.dll` and `RegionRevealLauncher.exe`.

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

## How it works

Two 5-byte detours, both on `cube::WorldMap` methods found by signature:

| Hook | Purpose |
|---|---|
| `WorldMap::getCell(x, y)` | Calls the original. If the caller is inside the map draw method **and** the cell is in the player's current chunk, returns a copy with the reveal bit set. Otherwise returns the original cell untouched. |
| `WorldMap::discover(x, y)` | Pass-through. Only reads `x, y` to learn which chunk the player is in. |

The return-address check matters: `getCell` has nine callers, and only the map
renderer should see the modified answer. Gameplay code asking the same question
keeps getting the truth, which is what stops the mod from leaking into fast
travel or progression.

Details and evidence:
[`docs/REVERSE_ENGINEERING.md`](docs/REVERSE_ENGINEERING.md).

## Status

Verified in the running game, on both supported builds:

- the proxy loads before the entry point and the game runs normally;
- signature scanning resolves all three functions in the **live mapped image**,
  not just the file on disk;
- both detours install, and a startup self-check calls `getCell` back through
  its trampoline and gets the expected result, so the stolen prologue and the
  jump back are correct;
- the game stays up with the hooks in place.

**The reveal itself has never been seen.** That needs someone to create a
character, walk into a region and open the map — tests A through G in
[`docs/TESTING.md`](docs/TESTING.md), none of which have been run. Everything
above only shows the plumbing is sound; whether the right cells light up is
still an open question, and test G can still falsify the no-write claim.

## Known limitations

- **Region granularity is the map chunk, not `cube::Region`.** A chunk is 64 × 64
  map cells and is a confirmed structure of the map itself. Whether it lines up
  with what the game calls a region is an untested hypothesis
  (`docs/REVERSE_ENGINEERING.md`, "Regions").
- **The player's chunk is inferred from `discover(x, y)` calls.** Two of that
  function's three callers iterate a list, so in multiplayer the tracked chunk
  may follow something other than the local player. This is the weakest
  assumption in the mod.
- **No per-category toggles.** No field distinguishing a city from a dungeon from
  a boss has been identified in the 52-byte map cell, so `Reveal Cities` /
  `Reveal Dungeons` / `Reveal Bosses` switches are not implemented rather than
  faked against a guessed field.
- **Nothing persists.** Leave the region and its unexplored cells go dark again;
  quit and the save is exactly as the vanilla game left it. That is the intended
  design, but it is worth knowing it is not "permanent discovery".

## Licence

MIT — see [`LICENSE`](LICENSE). No game asset or executable is redistributed
here, and no third-party mod code is included.
