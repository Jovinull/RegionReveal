#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rr {

// How far a visit surveys, in gameplay regions, as a Chebyshev radius. Two means
// a 5x5 block centred on the region the player actually entered.
//
// A gameplay region is one landmark, so surveying nothing but the region under
// the player reveals only the landmark they are already standing in - true to a
// strict reading of "only where you have been", and close to useless. Two puts
// the neighbouring landmarks on the map while keeping every revealed region
// anchored to somewhere the player really went.
inline constexpr int kSurveyRadius = 2;

// Open-addressed set of packed region keys. The reveal test runs on every cell
// the map draws - tens of millions per session - so it has to be O(1) rather
// than a search over the visited centres.
class RegionSet {
public:
    void reset(std::size_t expected);
    void insert(std::uint32_t key);
    void grow();
    bool contains(std::uint32_t key) const;
    std::size_t size() const { return count_; }

private:
    static constexpr std::uint32_t kEmpty = 0xFFFFFFFFu;  // no region packs to this

    std::vector<std::uint32_t> slots_;
    std::size_t mask_ = 0;
    std::size_t count_ = 0;
};

// The regions RegionReveal knows about, in its own file beside the game.
//
// Two distinct sets, deliberately not conflated:
//
//   visited  - regions the player actually walked into. This is what persists.
//   revealed - every region within kSurveyRadius of a visited one. Derived, and
//              rebuilt from the visited set, so the radius can change later
//              without invalidating anyone's history.
//
// The game's save is never involved: Cube World persists a storage chunk's cells
// wholesale, so a reveal bit written into one would be saved, which is exactly
// what this mod avoids.
class VisitedRegions {
public:
    // Switches to `world`, loading its file. An empty name closes the set.
    void open(const std::string& world);

    // True when the region falls inside any visited region's survey.
    bool revealed(int x, int y) const;

    // Records a region the player entered. Returns true when it was not already
    // known, which is also the only thing that makes the set dirty.
    bool visit(int x, int y);

    // Writes the visited centres - never the derived coverage - through a
    // temporary and a replacing rename.
    void flush();

    const std::string& world() const { return world_; }
    std::size_t centres() const { return centres_.size(); }
    std::size_t covered() const { return revealed_.size(); }

private:
    bool load();
    void rebuild();
    void survey(int x, int y);

    std::string world_;
    std::vector<std::uint32_t> centres_;  // sorted; the only thing persisted
    RegionSet revealed_;                  // derived from centres_
    bool dirty_ = false;
};

// Packs a region coordinate pair. Both axes fit in 13 bits, so 16 each leaves
// the format room to grow.
std::uint32_t region_key(int x, int y);

}  // namespace rr
