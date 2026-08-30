# Region Reveal

Reveals cities, dungeons, bosses and other points of interest when entering a
region in Cube World Alpha.

> **Marker reveal works, proven side by side.** In one world, from the same spot:
> the map drew **24 landmark labels with the mod loaded and 2 without it**. The
> two survivors are the region the player was standing in and a city they had
> already found. Terrain is a separate, unsolved problem - the map fills in with
> names, not ground. See [`docs/AUDIT.md`](docs/AUDIT.md).

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

Neighbouring regions are unaffected — walk into one and it reveals in turn, but
**the one behind you stops being revealed**. That is the main gap against the
intended behaviour; see [`docs/BEHAVIOUR.md`](docs/BEHAVIOUR.md).

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

The 2013-07-20 build is the one the Alpha modding ecosystem targets — the Cube
World Mod Launcher v1.5 accepts only its exact file size and calls it **Alpha
0.1.1**, and Qube-Loader's hard-coded offsets resolve in it and not in the
2013-07-02 build. Details in [`docs/QUBE_COMPATIBILITY.md`](docs/QUBE_COMPATIBILITY.md).

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

A live session with instrumentation showed the reveal firing: 38 625 cells lit
across the session, roughly 25 per redraw, from a state where the game itself had
revealed none of them. The player reported seeing dungeon and castle names appear
while the terrain stayed dark, which is consistent.

**What has still not been done is a side-by-side comparison** — the same place,
with and without the DLL — so "the mod caused this" rests on counters rather than
on two screenshots. Tests A through G in [`docs/TESTING.md`](docs/TESTING.md)
remain unrun, and test G can still falsify the no-write claim.

## Known limitations

- **Only the current region is revealed, and it reverts when you leave.** The
  mod tracks one region and has no memory of where you have been. The intended
  behaviour is that visited regions stay revealed; that is not built. See
  [`docs/BEHAVIOUR.md`](docs/BEHAVIOUR.md).
- **Terrain stays dark.** A cell with no generated 32 x 32 tile image is skipped
  by the renderer before the reveal bit is read, so markers appear but the map
  itself does not fill in.
- **The player's chunk is inferred from `discover(x, y)` calls.** Two of that
  function's three callers iterate a list, so in multiplayer the tracked chunk
  may follow something other than the local player. This is the weakest
  assumption in the mod.
- **No per-category toggles.** No field distinguishing a city from a dungeon from
  a boss has been identified in the 52-byte map cell, so `Reveal Cities` /
  `Reveal Dungeons` / `Reveal Bosses` switches are not implemented rather than
  faked against a guessed field.
- **Nothing persists.** The game's save is left exactly as vanilla left it, which
  is deliberate and stays that way. But the mod keeps no record of its own either,
  which is *not* deliberate — see the first limitation.

## Licence

MIT — see [`LICENSE`](LICENSE). No game asset or executable is redistributed
here, and no third-party mod code is included.
