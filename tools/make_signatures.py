"""Cut byte signatures for the functions RegionReveal hooks.

A signature is taken from one build and only accepted when it matches exactly
once in *every* supported build. Operands that legitimately move between builds
(absolute virtual addresses, rel32 branch targets) are wildcarded; everything
else - opcodes, register encodings and structure offsets - must match, which is
what makes the signature evidence that the two builds share a layout.

Usage:
    python tools/make_signatures.py <Cube.exe> [<other Cube.exe> ...]
"""
import re
import sys

from cwtool import Image

# Function starts in the 2013-07-20 build, recovered in docs/REVERSE_ENGINEERING.md.
TARGETS = [
    (0x602440, 'kSigWorldMapGetCell', 'cube::WorldMap::getCell(int,int)'),
    (0x4C9680, 'kSigMapOverlayDraw', 'cube::MapOverlayWidget virtual slot 1 (draw)'),
    (0x477E10, 'kSigWorldAreaAt', 'cube::World area lookup (block x, block y)'),
    (0x5C5E20, 'kSigWorldTerrainHeight', 'cube::World terrain height (block x, block y, zone)'),
    (0x4E6A20, 'kSigVoxelImageCtor', 'tile image constructor (renderer, flag)'),
    (0x4E75C0, 'kSigVoxelImageResize', 'tile image resize (w, h, d)'),
    (0x4E7870, 'kSigVoxelImageBuild', 'tile image mesh build'),
    (0x601EB0, 'kSigDotListPushBack', 'std::list<border dot>::push_back'),
    (0x46F870, 'kSigListClear', 'std::list clear (folded across element types)'),
]


def cut(img, va, nbytes):
    raw = bytearray(img.read(va, nbytes))
    keep = bytearray(b'\x01' * len(raw))
    lo = img.base
    hi = img.base + img.pe.OPTIONAL_HEADER.SizeOfImage
    end = 0
    for ins in img.md.disasm(bytes(raw), va):
        off = ins.address - va
        if off + ins.size > len(raw):
            break
        end = off + ins.size
        if ins.mnemonic in ('call', 'jmp') and ins.size == 5 and raw[off] in (0xE8, 0xE9):
            keep[off + 1:off + 5] = b'\x00' * 4
            continue
        for k in range(max(0, ins.size - 3)):
            word = int.from_bytes(raw[off + k:off + k + 4], 'little')
            if lo <= word < hi:
                keep[off + k:off + k + 4] = b'\x00' * 4
    # Stop on an instruction boundary: a cut through the middle of an
    # instruction can leave part of an absolute address unwildcarded.
    return bytes(raw[:end]), bytes(keep[:end])


def render(raw, keep):
    return ' '.join(f'{b:02X}' if k else '??' for b, k in zip(raw, keep))


def matches(img, raw, keep):
    sva, ptr, size = img.text_range()
    blob = img.data[ptr:ptr + size]
    rx = b''.join(re.escape(bytes([b])) if k else b'.' for b, k in zip(raw, keep))
    return [sva + m.start() for m in re.finditer(rx, blob, re.S)]


def main():
    images = [Image(p) for p in sys.argv[1:]]
    if not images:
        raise SystemExit(__doc__)
    reference = images[0]

    for va, name, desc in TARGETS:
        raw, keep = cut(reference, va, 64)
        hits = {img.path: matches(img, raw, keep) for img in images}
        unique = all(len(h) == 1 for h in hits.values())
        print(f'// {desc}')
        print(f'{"" if unique else "// NOT UNIQUE - do not ship: "}{name} = "{render(raw, keep)}"')
        for path, h in hits.items():
            print(f'//   {path}: {[hex(x) for x in h]}')
        print()


if __name__ == '__main__':
    main()
