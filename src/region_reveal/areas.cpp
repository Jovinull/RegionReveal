#include "areas.hpp"

#include <algorithm>

#include "../game/world.hpp"
#include "visited.hpp"

namespace rr {

RevealedAreas::RevealedAreas() { InitializeCriticalSection(&lock_); }

RevealedAreas::~RevealedAreas() { DeleteCriticalSection(&lock_); }

void RevealedAreas::reset(const std::string& world, const std::vector<std::uint32_t>& storedCells) {
    EnterCriticalSection(&lock_);
    world_ = world;
    seeds_.clear();
    pending_ = storedCells;
    LeaveCriticalSection(&lock_);
    ++generation_;
}

void RevealedAreas::resolve(cw::WorldMap* map) {
    EnterCriticalSection(&lock_);
    std::vector<std::uint32_t> pending;
    pending.swap(pending_);
    LeaveCriticalSection(&lock_);
    if (pending.empty()) return;

    std::vector<std::uint32_t> still;
    for (const std::uint32_t key : pending) {
        const cw::Area area = cw::area_at_cell(map, key_x(key), key_y(key));
        if (area.valid()) {
            add(area.seed);
        } else {
            still.push_back(key);
        }
    }

    EnterCriticalSection(&lock_);
    pending_.insert(pending_.end(), still.begin(), still.end());
    LeaveCriticalSection(&lock_);
}

bool RevealedAreas::contains(std::int32_t seed) const {
    EnterCriticalSection(&lock_);
    const bool found = std::binary_search(seeds_.begin(), seeds_.end(), seed);
    LeaveCriticalSection(&lock_);
    return found;
}

bool RevealedAreas::add(std::int32_t seed) {
    EnterCriticalSection(&lock_);
    const auto at = std::lower_bound(seeds_.begin(), seeds_.end(), seed);
    const bool fresh = at == seeds_.end() || *at != seed;
    if (fresh) seeds_.insert(at, seed);
    LeaveCriticalSection(&lock_);
    if (fresh) ++generation_;
    return fresh;
}

std::vector<std::int32_t> RevealedAreas::snapshot(std::string* world) const {
    EnterCriticalSection(&lock_);
    std::vector<std::int32_t> copy = seeds_;
    *world = world_;
    LeaveCriticalSection(&lock_);
    return copy;
}

bool RevealWindow::revealed(int x, int y) const {
    const Buffer& b = buffers_[active_.load(std::memory_order_acquire)];
    if (!b.valid.load(std::memory_order_relaxed)) return false;
    const int dx = x - b.x0.load(std::memory_order_relaxed);
    const int dy = y - b.y0.load(std::memory_order_relaxed);
    if (dx < 0 || dy < 0 || dx >= kSpan || dy >= kSpan) return false;
    const int bit = dx * kSpan + dy;
    return (b.bits[bit >> 5].load(std::memory_order_relaxed) >> (bit & 31)) & 1u;
}

void RevealWindow::publish(int centreX, int centreY, const std::vector<bool>& cells) {
    // Writes the buffer readers are not using, then flips. A reader still on the
    // old one is reading a buffer nobody writes until the next publish.
    const int next = 1 - active_.load(std::memory_order_relaxed);
    Buffer& b = buffers_[next];
    b.x0.store(centreX - kRadius, std::memory_order_relaxed);
    b.y0.store(centreY - kRadius, std::memory_order_relaxed);
    for (int w = 0; w < kWords; ++w) {
        std::uint32_t word = 0;
        for (int k = 0; k < 32; ++k) {
            const int bit = w * 32 + k;
            if (bit < kSpan * kSpan && cells[bit]) word |= 1u << k;
        }
        b.bits[w].store(word, std::memory_order_relaxed);
    }
    b.valid.store(true, std::memory_order_relaxed);
    active_.store(next, std::memory_order_release);
}

void RevealWindow::clear() {
    for (Buffer& b : buffers_) b.valid.store(false, std::memory_order_relaxed);
}

}  // namespace rr
