# Intended behaviour vs. what is built

> **Status: implemented.** The three pieces below are built — the player chain
> replaced the `discover` hook, and visited regions persist per world in
> `RegionReveal_<world>.visited`. What remains unverified is in-game behaviour;
> see `docs/AUDIT.md`.

## The gap this document was written about

**What is wanted:** entering a region for the first time marks it as discovered
by RegionReveal — permanently, from the mod's point of view. Its map, cities,
dungeons, bosses and structures become visible. Entering further regions adds
them. **Regions visited earlier stay revealed.** Regions never visited stay
hidden.

**What was built at the time:** only the region the player was standing in, and
it stopped being revealed the moment they left. The rest of this document
records that gap and the fix that closed it.

## Confirmed by reading the code

`src/region_reveal/reveal.cpp` holds one region, as two integers:

```cpp
std::atomic<int> g_region_x{-1};
std::atomic<int> g_region_y{-1};
```

`discover_detour` overwrites them on every call:

```cpp
g_region_x.store(cw::chunk_of(x), std::memory_order_relaxed);
g_region_y.store(cw::chunk_of(y), std::memory_order_relaxed);
```

and `get_cell_detour` reveals a cell only on an exact match:

```cpp
if (cw::chunk_of(x) != g_region_x.load(...) ||
    cw::chunk_of(y) != g_region_y.load(...)) {
    return cell;            // not revealed
}
```

So the moment the player crosses into a new region, the previous one no longer
matches and reverts to whatever the player genuinely explored. **Current-region
only, with no memory.** The live diagnostic is consistent with this: the
`otherRegion` counter climbed to 128 602 as the player moved, which is exactly
this branch rejecting cells.

## What the fix requires

Three pieces, none of them cosmetic.

### 1. A set of visited regions instead of one pair

The region grid is 1024 x 1024, so a bitset covering every region in the world is
`1024 * 1024 / 8` = **128 KiB** — small enough to hold in memory and write out
whole. Membership becomes one bit test in the detour, which is cheaper than the
two atomic loads it replaces.

### 2. RegionReveal's own storage, beside the game's

The game's save must stay untouched — that constraint has not changed, and
`docs/AUDIT.md` records why it matters: chunk persistence copies the cell bytes
wholesale, so anything written into a cell is saved.

A separate file (`RegionReveal.visited` next to `Cube.exe`, say) keeps the mod's
knowledge entirely outside the game's data. Deleting it resets the mod and
nothing else; deleting the DLL leaves no trace at all.

**Open problem: it has to be keyed per world.** The save already names worlds —
`Save/map_saddsa.db`, `Save/map_sdaads.db` — and `0x5FBC90` builds that path from
a string at `[WorldMap+0xAC] + 0x94`. Reading that string gives the world name to
key the file by. Until that is done, a single shared file would leak one world's
visited regions into another, which would reveal regions the player has never
been to in *that* world — precisely what the requirement forbids.

### 3. An authoritative "which region is the player in"

Today this is inferred from `WorldMap::discover(x, y)`, whose callers iterate
lists, so the coordinate may belong to another creature. With a *persistent* set
that flaw gets worse rather than better: a wrong region is no longer a transient
glitch, it is written down and stays revealed.

The chain is now known (see `docs/QUBE_COMPATIBILITY.md`):

```
GameController* gc     = *(void**)0x0076B1C8;
Creature*       player = *(void**)((char*)gc + 0x8006D0);
int64_t         posX   = *(int64_t*)((char*)player + 0x10);
int64_t         posY   = *(int64_t*)((char*)player + 0x18);
cellX = (posX / 65536) >> 8;   regionX = cellX >> 6;
```

Corroborated twice: `0x0076B1C8` is a `.data` global with 45 code references, and
the map renderer itself reads `GC + 0x8006D0` at `0x4C98C4` — reached through
`[MapOverlayWidget + 0x160]`, which is therefore the `GameController`.

That last point matters for policy: the widget already carries a pointer to the
`GameController`, so the player can be reached **without hard-coding
`0x0076B1C8`**, keeping the "no absolute addresses" rule intact. It needs the
draw method hooked to capture the widget, which is a design change, not a patch.

## Terrain still limits the outcome

Even with all three pieces done, a visited region would show its **markers**, not
its terrain. A cell whose `ZoneTile+0x10` is zero is skipped by the renderer
before the reveal bit is consulted, and that field is tied to a 32x32 `tile`
image the game generates per cell during exploration. `docs/AUDIT.md` covers the
evidence and why this is Result B rather than Result C.

So "full region map" is a separate problem from "persistent visited regions", and
neither is solved by the other.
