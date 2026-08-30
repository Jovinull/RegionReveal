"""Enumerates gameplay-region landmarks straight from a saved world.

The `reg<x>_<y>` blob turns out to be 4096 cell records of 14 bytes followed by
64 region records of 44, plus a trailing dword: 4096*14 + 64*44 + 4 = 60164,
which is every blob's exact size. Byte +4 of a region record is the landmark
type, anchored by a live probe that read 3 from the record while the player was
in a forest.

    python tools/read_landmarks.py <Save/map_<world>.db> [centreX centreY radius]
"""
import collections
import sqlite3
import sys

CELLS = 4096 * 14
RECORD = 44

# Only values confirmed against a screenshot of the map are named. Everything
# else prints as UNKNOWN rather than a guess from the name generator's
# registration order, which stopped matching past the natural landmarks.
NAMES = {0: 'none [confirmed skipped]', 1: 'City [confirmed]', 2: 'Mountain [confirmed]',
         3: 'Forest [confirmed]', 4: 'Lake [confirmed]', 10: 'none [confirmed skipped]'}


def name_of(raw):
    return NAMES.get(raw, f'UNKNOWN raw={raw}')


def load(path):
    db = sqlite3.connect('file:' + path + '?mode=ro', uri=True)
    return {k: v for k, v in db.execute("select key, value from blobs where key like 'reg%'")}


def landmark(blobs, rx, ry):
    blob = blobs.get(f'reg{rx >> 3}_{ry >> 3}')
    if blob is None:
        return None
    index = (rx & 7) * 8 + (ry & 7)
    at = CELLS + index * RECORD + 4
    return int.from_bytes(blob[at:at + 4], 'little')


def main():
    blobs = load(sys.argv[1])
    if len(sys.argv) >= 4:
        cx, cy = int(sys.argv[2]), int(sys.argv[3])
        radius = int(sys.argv[4]) if len(sys.argv) > 4 else 2
        for rx in range(cx - radius, cx + radius + 1):
            for ry in range(cy - radius, cy + radius + 1):
                raw = landmark(blobs, rx, ry)
                label = 'NO RECORD' if raw is None else f'{raw:3} {name_of(raw)}'
                print(f'  ({rx},{ry})  {label}')
        return

    histogram = collections.Counter()
    for blob in blobs.values():
        for i in range(64):
            at = CELLS + i * RECORD + 4
            histogram[int.from_bytes(blob[at:at + 4], 'little')] += 1
    total = sum(histogram.values())
    print(f'{total} regions across {len(blobs)} saved chunks')
    for raw, count in sorted(histogram.items()):
        print(f'  raw {raw:3}  {count:6}  {100 * count / total:5.1f}%  {name_of(raw)}')


if __name__ == '__main__':
    main()
