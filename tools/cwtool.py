"""Static analysis helper for Cube World Alpha (PE32, MSVC 2012).

Read-only: this never writes to the analysed binary.

Commands:
  info                      PE header summary
  strings [regex]           printable strings, optionally filtered
  rtti [filter]             RTTI classes, hierarchies and vftable VAs
  xref <va>                 code references to an address (imm32 + rel32 call/jmp)
  dis <va> [count]          linear disassembly
  func <va>                 disassemble until the first ret
  vft <va> [n]              dump a vftable's slots
"""
import re
import sys

import capstone
import pefile

MANGLED = re.compile(rb'\.\?A[VU][A-Za-z0-9_@?$]{1,250}@@\x00')


class Image:
    def __init__(self, path):
        self.path = path
        self.pe = pefile.PE(path, fast_load=True)
        self.data = self.pe.__data__
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        self.md.detail = True

    def sections(self):
        for s in self.pe.sections:
            yield (s.Name.rstrip(b'\x00').decode('latin1'),
                   self.base + s.VirtualAddress, s.Misc_VirtualSize,
                   s.PointerToRawData, s.SizeOfRawData)

    def va2off(self, va):
        for _, sva, _vsz, ptr, rsz in self.sections():
            if rsz and sva <= va < sva + rsz:
                return ptr + (va - sva)
        return None

    def off2va(self, off):
        for _, sva, _vsz, ptr, rsz in self.sections():
            if rsz and ptr <= off < ptr + rsz:
                return sva + (off - ptr)
        return None

    def section_of(self, va):
        for name, sva, vsz, ptr, rsz in self.sections():
            if sva <= va < sva + max(vsz, rsz):
                return name
        return None

    def read(self, va, n):
        off = self.va2off(va)
        return self.data[off:off + n] if off is not None else b''

    def dword(self, va):
        b = self.read(va, 4)
        return int.from_bytes(b, 'little') if len(b) == 4 else None

    def text_range(self):
        for name, sva, _vsz, ptr, rsz in self.sections():
            if name == '.text':
                return sva, ptr, rsz
        raise RuntimeError('no .text section')


def demangle(name):
    m = re.match(r'^\.\?A[VU]([^@]+)((?:@[^@]+)*)@@$', name)
    if not m:
        return name
    parts = [m.group(1)] + [p for p in m.group(2).split('@') if p]
    return '::'.join(reversed(parts))


def rtti(img):
    """Recover MSVC RTTI: type descriptors, object locators, vftables, hierarchy."""
    tds = {}
    for m in MANGLED.finditer(img.data):
        va = img.off2va(m.start() - 8)
        if va:
            tds[va] = m.group(0)[:-1].decode('latin1')

    cols = {}
    for name, _sva, _vsz, ptr, rsz in img.sections():
        if name not in ('.rdata', '.data', '_RDATA'):
            continue
        for off in range(ptr, ptr + rsz - 4, 4):
            val = int.from_bytes(img.data[off:off + 4], 'little')
            if val not in tds:
                continue
            col = off - 0x0C
            if col < ptr:
                continue
            if int.from_bytes(img.data[col:col + 4], 'little') != 0:
                continue
            chd = int.from_bytes(img.data[col + 0x10:col + 0x14], 'little')
            if img.section_of(chd) is None:
                continue
            offset = int.from_bytes(img.data[col + 4:col + 8], 'little')
            cols[img.off2va(col)] = (val, chd, offset)

    vfts = {}
    for name, _sva, _vsz, ptr, rsz in img.sections():
        if name not in ('.rdata', '.data', '_RDATA'):
            continue
        for off in range(ptr, ptr + rsz - 4, 4):
            val = int.from_bytes(img.data[off:off + 4], 'little')
            if val in cols:
                td, _chd, offset = cols[val]
                vfts.setdefault(demangle(tds[td]), []).append((img.off2va(off + 4), offset))

    hier = {}
    for td, chd, _off in cols.values():
        cname = demangle(tds[td])
        if cname in hier:
            continue
        num = img.dword(chd + 8)
        arr = img.dword(chd + 0x0C)
        if not num or not arr or num > 64:
            continue
        bases = []
        for i in range(num):
            bcd = img.dword(arr + i * 4)
            btd = img.dword(bcd) if bcd else None
            if btd in tds:
                bases.append(demangle(tds[btd]))
        hier[cname] = bases
    return tds, cols, vfts, hier


def vft_slots(img, va, limit=200):
    out = []
    for i in range(limit):
        v = img.dword(va + i * 4)
        if v is None or img.section_of(v) != '.text':
            break
        out.append(v)
    return out


def xrefs(img, target):
    """Code references to `target`: absolute imm32 operands and rel32 call/jmp."""
    sva, ptr, rsz = img.text_range()
    blob = img.data[ptr:ptr + rsz]
    hits = []

    needle = target.to_bytes(4, 'little')
    start = 0
    while True:
        i = blob.find(needle, start)
        if i < 0:
            break
        hits.append(('abs', sva + i))
        start = i + 1

    for i in range(len(blob) - 5):
        if blob[i] in (0xE8, 0xE9):
            rel = int.from_bytes(blob[i + 1:i + 5], 'little', signed=True)
            if sva + i + 5 + rel == target:
                hits.append(('call' if blob[i] == 0xE8 else 'jmp', sva + i))
    return hits


def fmt(ins):
    return f"  0x{ins.address:08x}  {ins.bytes.hex():<20} {ins.mnemonic} {ins.op_str}"


def main():
    path, cmd = sys.argv[1], sys.argv[2]
    img = Image(path)
    args = sys.argv[3:]

    if cmd == 'info':
        ep = img.base + img.pe.OPTIONAL_HEADER.AddressOfEntryPoint
        print(f"{path}\n  ImageBase 0x{img.base:x}  EP 0x{ep:x}  "
              f"TimeDateStamp {img.pe.FILE_HEADER.TimeDateStamp}")
        for n, sva, vsz, ptr, rsz in img.sections():
            print(f"  {n:<10} VA 0x{sva:08x} vsize 0x{vsz:06x} raw 0x{ptr:06x}/0x{rsz:06x}")

    elif cmd == 'strings':
        pat = re.compile(args[0], re.I) if args else None
        seen = set()
        for m in re.finditer(rb'[\x20-\x7e]{4,200}', img.data):
            s = m.group(0).decode('latin1')
            if s in seen or (pat and not pat.search(s)):
                continue
            seen.add(s)
            print(f"  0x{img.off2va(m.start()) or 0:08x}  {s}")

    elif cmd == 'rtti':
        _tds, _cols, vfts, hier = rtti(img)
        flt = args[0].lower() if args else None
        for cname in sorted(vfts):
            if flt and flt not in cname.lower():
                continue
            bases = [b for b in hier.get(cname, []) if b != cname]
            print(cname + (f" : {', '.join(bases)}" if bases else ""))
            for vva, off in sorted(vfts[cname]):
                print(f"    vftable 0x{vva:08x} (RVA 0x{vva - img.base:06x}) "
                      f"thisOff={off} slots={len(vft_slots(img, vva))}")

    elif cmd == 'xref':
        for kind, va in xrefs(img, int(args[0], 0)):
            print(f"  {kind:<5} 0x{va:08x}")

    elif cmd == 'dis':
        count = int(args[1]) if len(args) > 1 else 40
        va = int(args[0], 0)
        for i, ins in enumerate(img.md.disasm(img.read(va, count * 8 + 64), va)):
            if i >= count:
                break
            print(fmt(ins))

    elif cmd == 'func':
        va = int(args[0], 0)
        for ins in img.md.disasm(img.read(va, 8192), va):
            print(fmt(ins))
            if ins.mnemonic == 'ret':
                break

    elif cmd == 'vft':
        va = int(args[0], 0)
        for i, s in enumerate(vft_slots(img, va, int(args[1]) if len(args) > 1 else 200)):
            print(f"  [{i:3}] 0x{s:08x}")

    else:
        print(__doc__)


if __name__ == '__main__':
    main()
