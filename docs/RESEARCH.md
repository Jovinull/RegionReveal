# Research

Prior art surveyed before writing anything. **No code from any of these projects
was copied into RegionReveal** — every line here is original, and the licences
below were not individually audited because nothing was reused.

## Mod loaders

| Project | Target | Notes |
|---|---|---|
| [ChrisMiuchiz/Cube-World-Mod-Launcher](https://github.com/ChrisMiuchiz/Cube-World-Mod-Launcher) (fork of `coremaze/…`) | **Release 1.0.0-1 (2019), not Alpha** | Verified from `CubeModLoader/main.cpp`: `#define CUBE_VERSION "1.0.0-1"`, CRC32 gate (`0xC7682619` packed / `0xBA092543` unpacked), hijacks the `initterm_e` pointer at `base + 0x42CBD8` before CRT init, then `LoadLibraryA`s every DLL in `Mods\`. Mods export `ModMajorVersion`, `ModMinorVersion`, `ModPreInitialize`, `MakeMod`. |
| [coremaze/Cube-World-Server-Mod-Launcher](https://github.com/coremaze/Cube-World-Server-Mod-Launcher) | Release server | Same idea for `Server_Mods\`. |
| [zsennenga/CubeWorld-Mod-API](https://github.com/zsennenga/CubeWorld-Mod-API) | Cube World, era unclear | Described as DLL injection plus memory editing to dispatch events (`npcInteract`, `playerJump`, …). Only 4 commits; the README did not state a target build. |

**Conclusion that shaped this project:** the best-maintained loader is bound to
the 2019 release by an exact CRC32, so it will refuse an Alpha executable, and no
loader was found that names a specific Alpha build. RegionReveal therefore ships
its own minimal launcher (`tools/launcher/`) rather than depending on one. The
DLL itself is a plain `LoadLibrary` target, so any injector works.

## Reverse-engineering references

| Project | Applicability |
|---|---|
| [adamhlt/Cube-World-Reversing](https://github.com/adamhlt/Cube-World-Reversing) + [write-ups](https://adamhlt.com/cube-world-reversing-unpack-the-game/) | **None.** Targets the 2019 x64 release at ImageBase `0x140000000`. Our binaries are PE32/x86 at `0x400000`; its addresses, RTTI and offsets do not transfer. |
| [CallumCarmicheal/CubedWorld](https://github.com/CallumCarmicheal/CubedWorld) | Alpha-era research repo. No build, hash or offsets published in the README. |
| [humanova/CubeWorld-ESP](https://github.com/humanova/CubeWorld-ESP) | Alpha graphics RE. README is a single link; no offsets published. |
| [catb0t/openCW](https://github.com/catb0t/openCW) | Clean-room reimplementation aiming at "the last Alpha release", which it never pins to a build. Not a source of offsets. |
| [Cubeworld Reference Page / ModCatalogue](https://paroyer.github.io/ModCatalogue/Alpha.html) | Catalogue of Alpha files and mods; explicitly incomplete on installation details. |

A repository literally named `CubeWorld-Reversal` was searched for and **does not
appear to exist publicly**; the closest name match is `Cube-World-Reversing`
above, which targets the wrong architecture entirely.

**Net result:** no usable public offset database for Alpha `Cube.exe` was found.
Everything in `docs/REVERSE_ENGINEERING.md` was derived from scratch against the
binaries on this machine. That is also why the RTTI recovery mattered so much —
it supplied the real class names that the community documentation does not.

## Game/version references

- [Cube World — Wikipedia](https://en.wikipedia.org/wiki/Cube_World): Alpha
  released 2 July 2013, matching the `TimeDateStamp` of the `SECONDARY` build.
- [Patch notes, 23 July 2013](https://cubeworld.fandom.com/wiki/July_23rd,_2013):
  Ranger "Retreat" no longer increases hang-gliding speed; "Scout's swiftness" no
  longer crashes multiplayer servers. Three days after the `PRIMARY_TARGET`
  build's timestamp.
- [Patch notes, 5 July 2013](https://cubeworld.fandom.com/wiki/July_5th,_2013).

## Techniques used

- MSVC RTTI layout (`TypeDescriptor` / `RTTICompleteObjectLocator` /
  `RTTIClassHierarchyDescriptor`) for x86, where locator fields are absolute
  VAs and the signature word is `0`. Implemented from the layout, in
  `tools/cwtool.py`.
- Wildcarded byte-signature scanning, with an ambiguity check: a pattern that
  matches twice is rejected rather than used.
- 5-byte relative-jump detours restricted to targets whose stolen bytes are
  whole, position-independent instructions, which removes the need for a length
  disassembler.
