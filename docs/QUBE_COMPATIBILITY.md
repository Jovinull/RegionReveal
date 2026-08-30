# Loader strategy, and the Qube-Loader question

## Qube-Loader could not be found

This was asked for specifically, so the negative result is worth stating
plainly: **searching did not turn up a project called Qube-Loader**, for Cube
World Alpha or otherwise. Two searches returned only the same cluster of known
projects (`ChrisMiuchiz/Cube-World-Mod-Launcher` and its forks,
`zsennenga/CubeWorld-Mod-API`, `gijsgroenewegen/*`, the ModCatalogue pages).

Nothing was written here about its supported build, expected RVAs, exposed
structures or API, because none of that could be read. Inventing a compatibility
assessment for a project that was never opened would be worth less than nothing.

**If it exists, a link would settle it in one pass.** The comparison worth making
then is: which Alpha build it targets, whether that is our 2013-07-20 primary,
whether it already exposes World/Map/Region/Player, and whether the findings in
`docs/AUDIT.md` — `cube::ZoneTile` as the map cell, the `reg`/`land`/`tile` save
schema, the reveal bit at `+0x30` — would be useful upstream.

## What the loader options actually are

| Option | Contract | Status here |
|---|---|---|
| **dinput8 proxy** (current) | The OS loads it through the normal search order because `Cube.exe` imports one function from `dinput8.dll`. No injector, no launcher, one added file. | Working, verified on both builds. |
| **Classic Cube World Mod Launcher** | Injects, then loads DLLs from `Mods\`, and requires exports `ModMajorVersion`, `ModMinorVersion`, `ModPreInitialize`, `MakeMod`. | **Rejected for Alpha.** Its source gates on `CUBE_VERSION "1.0.0-1"` with CRC32 `0xC7682619` / `0xBA092543`, which is the 2019 release. It would refuse our binaries. |
| **Own injector** (`RegionRevealLauncher.exe`) | Starts the game suspended, `CreateRemoteThread` + `LoadLibraryW`. | Built and working, but blocked outright by the scanner. Kept only as a fallback. |

## Proxy audit

Checked against `Cube.exe`'s import table:

- Cube.exe imports exactly **one** function from `dinput8.dll`,
  `DirectInput8Create`, so a one-export proxy is complete for this consumer.
- The proxy resolves the real DLL through `GetSystemDirectoryW`, i.e. an absolute
  path into `System32` (`SysWOW64` for a 32-bit process, which the OS redirects
  transparently). It never calls `LoadLibraryW("dinput8.dll")` by bare name, so
  **it cannot load itself recursively**.
- `proxy_attach()` returns false if either the load or the `GetProcAddress`
  fails, and `DllMain` then returns `FALSE`, so the game refuses to start rather
  than running with dead input.
- `proxy_detach()` releases the handle on `DLL_PROCESS_DETACH`.

Known limits, not yet addressed:

- **Only one export is forwarded.** Anything else in the process that loads
  `dinput8.dll` and wants `DllCanUnloadNow`, `DllGetClassObject`,
  `DllRegisterServer` or `DllUnregisterServer` would get a missing export. No
  such consumer exists in this game, but it makes the proxy non-general.
- **It collides with any other `dinput8` proxy** — ReShade and ENB commonly take
  the same slot. Two mods cannot both be `dinput8.dll`. That is the strongest
  argument for eventually supporting a real loader instead.

## Architectural consequence

The mod's logic and its loading mechanism should not be entangled, so that a
future move to another loader is a new file rather than a rewrite. The source is
already close to that shape:

```
src/game/          Cube World structures and signature scanning
src/region_reveal/ the reveal logic and its hooks
src/hooks.*        the detour primitive
src/loader/        proxy_dinput8.cpp  <- the only loader-aware file
src/mod.cpp        DllMain, guarded by REGIONREVEAL_PROXY_DINPUT8
```

`rr::initialize()` and `rr::shutdown()` are the whole interface a loader needs,
and nothing under `src/region_reveal/` or `src/game/` knows how the DLL got into
the process. Adding a second loader means adding a file under `src/loader/` and a
CMake target — no change to the core.
