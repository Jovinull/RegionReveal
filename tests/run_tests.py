"""Runs signature_test against every supported Cube.exe.

    python tests/run_tests.py <build/signature_test.exe> <Cube.exe> [<Cube.exe> ...]

Each executable is identified by SHA-256, so a file that is not one of the
supported builds is reported rather than silently checked against the wrong
expected addresses.
"""
import hashlib
import os
import subprocess
import sys

# sha256 -> (label, getCell RVA, MapOverlayWidget draw RVA)
BUILDS = {
    '84a7a132a84d4282338e7ea45a1940d32066d64e39cbafed8f2418d5a6dc30bf':
        ('Alpha 2013-07-20 (PRIMARY_TARGET)', '202440', 'c9680'),
    'a4eeb3606ad2b82e4c9b3d0db6f9472ffa6e89a4d56084a9114ff1fb3c812699':
        ('Alpha 2013-07-02', '200ed0', 'c9490'),
}


def sha256(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as handle:
        for block in iter(lambda: handle.read(1 << 20), b''):
            digest.update(block)
    return digest.hexdigest()


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    # CreateProcess does not search a relative path written with forward
    # slashes, which is how the build instructions spell it.
    tester, binaries = os.path.abspath(sys.argv[1]), sys.argv[2:]

    failed = 0
    for path in binaries:
        digest = sha256(path)
        build = BUILDS.get(digest)
        if not build:
            print(f'SKIP {path}\n  unknown build, sha256 {digest}')
            failed += 1
            continue
        label, *rvas = build
        print(f'{label}')
        if subprocess.run([tester, path, *rvas]).returncode != 0:
            failed += 1

    print('\nall signatures resolved' if not failed else f'\n{failed} target(s) failed')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
