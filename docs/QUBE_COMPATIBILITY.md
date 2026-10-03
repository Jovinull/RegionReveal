# Loader ecosystem

An earlier revision of this file said Qube-Loader could not be found and that the
Classic launcher targets the 2019 release. **Both statements were wrong.** The
first was a failed search reported as a negative result; the second came from
reading the launcher's `master` branch instead of the version the Alpha
ecosystem actually uses. Corrected below.

## Our build is the one everything targets — CONFIRMED

Three independent lines of evidence, none of which depend on each other:

### 1. Qube-Loader's offsets resolve in our 2013-07-20 build and not the other

[`qad3n/Qube-Loader`](https://github.com/qad3n/Qube-Loader) hard-codes absolute
VAs at `ImageBase 0x400000` in `modloader/src/game/offsets.h`. Checked against
both installs:

| Qube constant | Value | 2013-07-20 | 2013-07-02 |
|---|---|---|---|
| `kCreatureVtable` | `0x006FD8CC` | **exactly `cube::Creature`'s vftable**, recovered independently by `tools/cwtool.py rtti` | `0x006FC8CC` — different address |
| `kDbLoadBlobByKey` | `0x00449810` | `55 8B EC` — function prologue | `06 2B F1` — mid-instruction |
| `kGetAttackWindupFn` | `0x0043CAA0` | `55 8B EC` — function prologue | `00 00 89` — mid-instruction |
| `kOperatorNew` | `0x0068D652` | `55 8B EC` — function prologue | `6F 00 FF` — mid-instruction |
| `kGameControllerVfunc0` | `0x0040CBD0` | `8B 41 04 C3` — a one-line getter | `CA C6 46` — mid-instruction |

The `cube::Creature` row is the strongest: that address was derived here from
RTTI, with no knowledge of Qube, and lands on the same byte.

### 2. Cube World Mod Launcher v1.5 gates on our exact file size

The Alpha ecosystem uses the 2018
[`v1.5`](https://github.com/coremaze/Cube-World-Mod-Launcher/releases/tag/v1.5)
release, which [`Gapagapi1/Cube-World-Alpha-Mods`](https://github.com/Gapagapi1/Cube-World-Alpha-Mods)
names as the requirement for its client mods. Its `main.cpp` reads:

```cpp
const int CUBE_SIZE = 3885568;
if (fileSize != CUBE_SIZE) {
    printf("Cube World was found, but it is not version 0.1.1. Please update your game.\n");
```

`3 885 568` is our 2013-07-20 `Cube.exe` to the byte. The 2013-07-02 build
(3 878 400) would be rejected with "please update your game".

**This also gives the build a name the community uses: Alpha 0.1.1.**

The later tags in that repository (`1.0.0-1_*`, `0.9.1-*`) are for the 2019
release and are what the earlier revision of this document mistakenly analysed.

### 3. CubeWorld-Reversal describes the same binary

[`qad3n/CubeWorld-Reversal`](https://github.com/qad3n/CubeWorld-Reversal) is a
Ghidra decompilation of the 2013 Alpha, and reports the recovered PDB path
`C:\Users\funck\Projects\Cube\OptimizedXP\Cube.pdb`. Both of our `Cube.exe`
files embed exactly that string, so **this evidence does not discriminate between
the two builds** — it only confirms the family. Recorded as such rather than
counted as a third proof.

Its class counts do line up with ours: it reports 55 recovered game classes plus
roughly 95 `plasma::` engine classes, against the 149 vftable-bearing classes
`tools/cwtool.py` recovers here.

## What this changes

`docs/TARGET_BUILD.md` chose 2013-07-20 on internal evidence — later timestamp,
proximity to the 2013-07-23 patch notes. That choice is now externally
corroborated twice over. **`PRIMARY_TARGET` stands, on much firmer ground.**

Support for 2013-07-02 stays: signature scanning already resolves it, it costs
nothing, and no ecosystem tool would touch it.

## Coexistence

| Loader | Mechanism | Conflicts with our proxy? |
|---|---|---|
| **Qube-Loader** | `inject.exe` does `CreateRemoteThread` + `LoadLibraryA` of `cube_mod.dll`. Its DirectInput work is a **vtable hook on `IDirectInputDevice8::GetDeviceState` (slot 9)**, not a proxy DLL. | **No.** Qube ships no `dinput8.dll`, so there is no filename collision. Our proxy forwards `DirectInput8Create` to `System32` and Qube hooks the device object that comes back — the two compose. |
| **Mod Launcher v1.5** | `CreateProcess(CREATE_SUSPENDED)`, then injects `CallbackManager.dll` plus everything in `Mods\`. | **No.** Our DLL is pulled in by the import table when the game resumes; the launcher's injection is independent. RegionReveal does not need the launcher and does not register with it. |
| **ReShade / ENB** | Commonly install *as* `dinput8.dll`. | **Yes — direct collision.** Only one file can hold that name. This remains the strongest argument for eventually supporting a real loader. |

## Should RegionReveal move onto Qube?

Not yet, and the reason is not inertia.

**In favour:** Qube already exposes player, world, creature and camera state, an
event and hook bus, config/storage services, and a shared ImGui overlay. Its
local-player chain — `kLocalPlayerPtr 0x0076B1C8` to `GameController*`, then
`+0x8006D0` to the local `Creature*`, position as int64 fixed point at
`+0x10`/`+0x18` divided by 65536 — is the one RegionReveal reads, reached here
from the `WorldMap` instead of the global; the map renderer reads the same
`GC + 0x8006D0` at `0x4C98C4`.

**Against:** Qube is GPL-3.0 and RegionReveal is MIT, so vendoring it would force
a licence change. It describes itself as an early proof of concept with an
evolving API. And it needs an injector, which is the thing the scanner on this
machine rejects outright.

**Recommendation:** keep the core loader-agnostic — `rr::initialize()` and
`rr::adopt_game_thread()` are the entire contract, and nothing under `src/game/`
or `src/region_reveal/` knows how the DLL arrived. A Qube backend would be a new
file under `src/loader/`, with one caveat: injected after the game has started,
the five-byte patch is no longer written before any game thread exists.

**Worth contributing upstream regardless of that decision:** the world-map
findings in `docs/AUDIT.md` — `cube::ZoneTile` as the map cell, the
`land`/`reg`/`tile`/`discovered` save schema, the reveal bit at `ZoneTile+0x30`,
`WorldMap::getCell` at `0x602440`, `WorldMap::discover` at `0x5FC160`, the map's
two label passes and `cube::World`'s area lookup at `0x477E10` — do not appear
in Qube's `offsets.h`, which has no world-map coverage at all.
