# Target builds

Two Cube World Alpha installations were found on this machine. Both are genuine
MSVC 2012 compiler output — no packer, no loader, no patched entry point — and
both are supported by RegionReveal through signature scanning.

Neither executable is modified by this project, and neither is stored in this
repository.

## Audit

| | `Cube World Alpha` | `Updated Cube World Alpha` |
|---|---|---|
| Path | `C:\Users\felip\Downloads\Cube World Alpha` | `C:\Users\felip\Downloads\Updated Cube World Alpha` |
| Role | `SECONDARY` | **`PRIMARY_TARGET`** |
| Packaging | Inno Setup install (`unins000.exe`, `CubeLauncher.exe`, `Save/`) | extracted/portable (no installer, adds `db.dat` and library licences) |

### `Cube.exe`

| | 2013-07-02 build | 2013-07-20 build |
|---|---|---|
| Size | 3 878 400 | 3 885 568 |
| SHA-256 | `a4eeb3606ad2b82e4c9b3d0db6f9472ffa6e89a4d56084a9114ff1fb3c812699` | `84a7a132a84d4282338e7ea45a1940d32066d64e39cbafed8f2418d5a6dc30bf` |
| SHA-1 | `4f253499a06e0c21037c5c1994432b5be1aa4c39` | `15d075371928fee137f638377ec1d29a00d54abe` |
| PE TimeDateStamp | `1372769948` = 2013-07-02 12:59:08 UTC | `1374328158` = 2013-07-20 13:49:18 UTC |
| Architecture | PE32, `Machine 0x14C` (x86) | PE32, `Machine 0x14C` (x86) |
| Linker | 11.0 (MSVC 2012) | 11.0 (MSVC 2012) |
| ImageBase | `0x400000` | `0x400000` |
| EntryPoint RVA | `0x28CF70` | `0x28E1E0` |
| SizeOfImage | `0x3BA000` | `0x3BC000` |
| Sections | `.text .rdata .data _RDATA .rsrc .reloc` | identical set and order |
| `.text` entropy | 6.570 | 6.567 |

### `Server.exe`

| | 2013-07-02 build | 2013-07-20 build |
|---|---|---|
| Size | 1 718 272 | 1 718 784 |
| SHA-256 | `dde8e7082907246d309e07d5a3ad4f230960e7a617bd19fb17f28a55b8c2e7b9` | `1ffbcbce251e1506a63c9ce18ab0009d5553fcbb9e6202be2bc3a7d870d34fb1` |
| SHA-1 | `50df932640e22bf41c401ce9179542a1eede0d7c` | `cfb73bd54aac497a4ac23fa86020b92023a2a90f` |
| PE TimeDateStamp | `1372769947` = 2013-07-02 12:59:07 UTC | `1374328021` = 2013-07-20 13:47:01 UTC |
| EntryPoint RVA | `0x14B2CB` | `0x14B44B` |
| Subsystem | 3 (console) | 3 (console) |

`CubeLauncher.exe` exists only in the 2013-07-02 install:
SHA-256 `6e2ca21cb5ab452f508eedeab13456ecce72da065d049ad65466f6cd2722f0d3`,
160 256 bytes, TimeDateStamp `1372525947` = 2013-06-29 17:12:27 UTC.

RegionReveal does not touch `Server.exe`; the world map lives entirely in the
client. The hashes are recorded so the pair a build belongs to is identifiable.

## Authenticity

Checked on both `Cube.exe` files:

- section names, order and characteristics are the stock MSVC set — no `UPX*`,
  `.themida`, `.vmp*` or other added section;
- `.text` entropy ≈ 6.55, normal for uncompressed x86 code;
- the entry point is the ordinary CRT stub
  (`call __security_init_cookie; jmp mainCRTStartup`) with no trampoline into a
  foreign section;
- the embedded manifest is byte-identical between the two (`asInvoker`, no
  elevation);
- RTTI is intact and complete in both, which a repacked or protected binary
  would normally have lost.

The 2013-07-20 install is a community repack rather than the original
installer (no `unins000.exe`). That is a provenance note, not evidence of
tampering — nothing in the binary itself is anomalous.

## Why 2013-07-20 is `PRIMARY_TARGET`

- It is the later of the two, and its timestamp sits three days before the
  2013-07-23 patch notes, i.e. it is the closest thing on this machine to the
  final Alpha state most players ran.
- All reverse engineering in `docs/REVERSE_ENGINEERING.md` was performed against
  it, so every address recorded there belongs to exactly this build.
- It was already frozen as the canonical binary for the separate decompilation
  effort; using the same build keeps the two projects' notes interchangeable.

The 2013-07-02 build is not a fallback: the byte signatures in
`src/game/signatures.cpp` were verified to match it uniquely too, so both builds
are hooked correctly at runtime.

## Addresses are per build — do not mix

Every function moved between the two builds:

| Function | 2013-07-02 | 2013-07-20 |
|---|---|---|
| `cube::WorldMap::getCell` | `0x600ED0` | `0x602440` |
| `cube::WorldMap::discover` | `0x5FAC10` | `0x5FC160` |
| `cube::MapOverlayWidget` draw | `0x4C9490` | `0x4C9680` |
| `cube::WorldMap` constructor | `0x5F9850` | `0x5FAE40` |
| `cube::World` area lookup | `0x478330` | `0x477E10` |
| `cube::World` terrain height | `0x5C4860` | `0x5C5E20` |
| tile image constructor | `0x4E5CF0` | `0x4E6A20` |
| tile image resize | `0x4E6650` | `0x4E75C0` |
| tile image mesh build | `0x4E6900` | `0x4E7870` |
| border-dot `push_back` | `0x600940` | `0x601EB0` |
| `std::list` clear | `0x46FB50` | `0x46F870` |

Structure offsets, by contrast, are identical in both — the signatures embed
them (`+0x8000C0`, `+0x8000BC`, `+0x30`, stride `0x34`, the tile image's `+0x30`
and `+0x44..+0x4C`) and still match, which is the evidence for that claim. The
offsets no signature covers — the owner's view cell and pan, the area's seed —
were checked by running the area reveal and previews on the 2013-07-02 build on
2026-10-03. This is why the mod resolves *functions* by pattern
and hard-codes only *field offsets*.
