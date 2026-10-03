#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace cw {
struct WorldMap;
}

namespace rr {

// The areas revealed in the current world, by seed.
//
// Filled from the cells the player is recorded as having entered: each cell is
// looked up in the running game, and only resolves once its area's storage chunk
// is resident, so a history from far away waits until the player is near it
// again. Written by the session tracking, snapshotted by the preview engine.
class RevealedAreas {
public:
    RevealedAreas();
    ~RevealedAreas();
    RevealedAreas(const RevealedAreas&) = delete;
    RevealedAreas& operator=(const RevealedAreas&) = delete;

    // A new world: forget every seed and queue the stored cells for lookup.
    void reset(const std::string& world, const std::vector<std::uint32_t>& storedCells);

    // Looks up queued cells whose area has become resident. Game thread.
    void resolve(cw::WorldMap* map);

    bool contains(std::int32_t seed) const;

    // Returns true when the seed was new.
    bool add(std::int32_t seed);

    // The seeds, and the world they belong to - a reader that has already seen
    // the next world must not apply the previous one's set to it.
    std::vector<std::int32_t> snapshot(std::string* world) const;

    // Changes whenever the set does, so readers can tell when to recompute.
    unsigned generation() const { return generation_.load(); }

private:
    mutable CRITICAL_SECTION lock_;
    std::string world_;
    std::vector<std::int32_t> seeds_;     // sorted
    std::vector<std::uint32_t> pending_;  // stored cells not yet resolved
    std::atomic<unsigned> generation_{0};
};

// Which cells around the map's view centre belong to a revealed area, as a
// bitmap. The map overlay asks once per cell per frame, so the answer has to be
// a lookup, not an area query: the preview engine computes the bitmap a few
// rows a frame and publishes it whole, and readers never take a lock.
class RevealWindow {
public:
    // Larger than the 32 cells the overlay's label pass reaches, so labels at
    // the edge of the map are covered while the engine catches up with a pan.
    static constexpr int kRadius = 40;
    static constexpr int kSpan = kRadius * 2 + 1;

    bool revealed(int x, int y) const;

    // Writer side, the preview engine only.
    void publish(int centreX, int centreY, const std::vector<bool>& cells);
    void clear();

private:
    static constexpr int kWords = (kSpan * kSpan + 31) / 32;

    struct Buffer {
        std::atomic<int> x0{0};
        std::atomic<int> y0{0};
        std::atomic<bool> valid{false};
        std::atomic<std::uint32_t> bits[kWords];
    };

    Buffer buffers_[2];
    std::atomic<int> active_{0};
};

}  // namespace rr
