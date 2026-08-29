# Tooling

Everything the analysis actually needed is either already on this machine or is
a Python package. The heavyweight GUI tools were deliberately not installed:
`Cube.exe` ships full RTTI, which made a scripted static pass more productive
than a decompiler session, and the mod needs no memory scanning.

## Installed for this project

| Tool | Version | Why |
|---|---|---|
| Visual Studio 2022 Build Tools | 17.14 / MSVC 14.44.35207 | 32-bit `cl.exe` and the Windows SDK. MSVC matters here beyond convenience: the mod calls the game's `__thiscall` functions through function pointers and defines `__fastcall` detours to match them, which is an MSVC calling-convention idiom. Installed with `winget install Microsoft.VisualStudio.2022.BuildTools --override "--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --add Microsoft.VisualStudio.Component.Windows11SDK.26100 --add Microsoft.VisualStudio.Component.VC.CMake.Project"`. |
| `capstone` | 5.0.7 | x86 disassembly inside `tools/cwtool.py`. |
| `pefile` | 2024.8.26 | PE parsing, section/VA mapping, import resolution. |

## Already present

| Tool | Version |
|---|---|
| Python | 3.12.10 |
| Ninja | 1.13.0 (via pip) |
| Git | 2.x |
| LLVM/clang | 22.1.8 — used only as the editor's language server, not to build |

## Deliberately not installed

- **Ghidra, IDA, PE-bear, Detect It Easy** — the questions asked here (which
  classes exist, where is the reveal bit, does the pattern match both builds)
  were all answered by `tools/cwtool.py` in a few seconds each, and its output is
  reproducible in the repo rather than trapped in a project database. A
  decompiler becomes worthwhile the moment someone wants the *contents* of the
  6650-byte draw function.
- **x32dbg, ReClass.NET, Cheat Engine** — all dynamic. Nothing in this repo has
  been validated at runtime yet (see `docs/TESTING.md`), so they are the right
  tools for the *next* stage, not this one. x32dbg in particular is what the open
  questions in `docs/REVERSE_ENGINEERING.md` need.
- **MinHook** — the mod installs exactly two detours on functions whose stolen
  bytes were verified to be position-independent, so `src/hooks.cpp` is ~60 lines
  and carries no third-party licence.

## `tools/cwtool.py`

Read-only static analysis. It never opens a binary for writing.

```
python tools/cwtool.py <Cube.exe> info                 # headers and sections
python tools/cwtool.py <Cube.exe> rtti [filter]        # classes, bases, vftables
python tools/cwtool.py <Cube.exe> strings [regex]      # strings with their VAs
python tools/cwtool.py <Cube.exe> xref <va>            # code references to an address
python tools/cwtool.py <Cube.exe> dis <va> [count]     # disassembly
python tools/cwtool.py <Cube.exe> func <va>            # disassemble to the first ret
python tools/cwtool.py <Cube.exe> vft <va> [n]         # vftable slots
```

`python tools/make_signatures.py <primary Cube.exe> <other Cube.exe>` re-cuts the
byte signatures in `src/game/signatures.cpp` and refuses any that is not unique
in every build passed.

## Working on copies

The analysis ran against copies in a scratch directory, never against the
installs. To reproduce:

```sh
mkdir -p work/2013-07-20 work/2013-07-02
cp "…/Updated Cube World Alpha/Cube.exe" work/2013-07-20/
cp "…/Cube World Alpha/Cube.exe"          work/2013-07-02/
sha256sum work/*/Cube.exe   # compare against docs/TARGET_BUILD.md
```

## Building

```sh
cmake -B build -A Win32
cmake --build build --config Release
```

`-A Win32` is not optional; `CMakeLists.txt` aborts on a 64-bit configuration
because the game is PE32.

Then:

```sh
python tests/run_tests.py build/Release/signature_test.exe "<game>/Cube.exe"
```

### Antivirus

Two false positives on this machine, both understandable:

- `signature_test.exe` was quarantined right after building. The binary embeds
  long literal byte sequences copied out of `Cube.exe`, so a scanner sees a
  program carrying fragments of another executable. A second copy built directly
  with `cl` ran fine, and the suite passes with it.
- `RegionRevealLauncher.exe` is **blocked outright** — launching it fails with
  `WinError 225`. `CreateRemoteThread` plus `LoadLibraryW` into another process
  is the textbook injection pattern and heuristics reject it regardless of
  intent.

That second one is why `dinput8.dll` is the supported loader. Proxying an import
is not injection: the OS loads the DLL through its normal search order, no
foreign process is written to, and Defender raises nothing. It also happens to be
the better design — it adds a file instead of driving the process externally.

No exclusion was added and no Defender setting was changed; that is a decision
for whoever owns the machine.
