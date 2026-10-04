#include "areas.hpp"

#include <algorithm>
#include <cstring>

#include "visited.hpp"

namespace rr {

void RevealedAreas::reset(const std::vector<std::uint32_t>& storedCells) {
    areas_.clear();
    pending_ = storedCells;
    ++generation_;
}

void RevealedAreas::resolve(AreaLookupFn lookup, void* context) {
    std::size_t kept = 0;
    for (const std::uint32_t key : pending_) {
        const cw::AreaLookup found = lookup(context, key_x(key), key_y(key));
        if (found.known) {
            add(found.area);
        } else {
            pending_[kept++] = key;
        }
    }
    pending_.resize(kept);
}

bool RevealedAreas::add(cw::AreaId area) {
    const auto at = std::lower_bound(areas_.begin(), areas_.end(), area);
    if (at != areas_.end() && *at == area) return false;
    areas_.insert(at, area);
    ++generation_;
    return true;
}

bool RevealedAreas::contains(cw::AreaId area) const {
    return std::binary_search(areas_.begin(), areas_.end(), area);
}

void LabelCells::set_radius(int radius) {
    int side = 64;
    while (side < 2 * radius) side *= 2;
    if (side == side_) return;
    side_ = side;
    slots_.clear();
    slots_.shrink_to_fit();
}

LabelCells::Slot& LabelCells::slot(int x, int y) {
    if (slots_.empty()) slots_.resize(static_cast<std::size_t>(side_) * side_);
    const int mask = side_ - 1;
    Slot& s = slots_[static_cast<std::size_t>(x & mask) * side_ + (y & mask)];
    if (s.epoch != epoch_ || s.x != x || s.y != y) {
        s.epoch = epoch_;
        s.x = x;
        s.y = y;
        s.state = State::Unasked;
    }
    return s;
}

bool LabelCells::decide(Slot& s, std::uint32_t now, const RevealedAreas& areas, AreaLookupFn lookup,
                        void* context) {
    if (s.state != State::Known) {
        if (s.state == State::Unknown && now - s.checkedAt < kRetryMs) return false;
        const cw::AreaLookup found = lookup(context, s.x, s.y);
        s.checkedAt = now;
        if (!found.known) {
            s.state = State::Unknown;
            return false;
        }
        s.state = State::Known;
        s.area = found.area;
        s.memberGeneration = areas.generation() - 1;  // forces the check below
    }
    if (s.memberGeneration != areas.generation()) {
        s.member = areas.contains(s.area);
        s.memberGeneration = areas.generation();
    }
    return s.member;
}

bool LabelCells::revealed(int x, int y, std::uint32_t now, const RevealedAreas& areas, AreaLookupFn lookup,
                          void* context) {
    return decide(slot(x, y), now, areas, lookup, context);
}

cw::MapCell* LabelCells::view(cw::MapCell* cell, int x, int y, std::uint32_t now, const RevealedAreas& areas,
                              AreaLookupFn lookup, void* context) {
    Slot& s = slot(x, y);
    if (!decide(s, now, areas, lookup, context)) return cell;
    std::memcpy(s.copy, cell, sizeof(s.copy));
    s.copy[cw::kCellFlags] |= cw::kRevealedBit;
    return reinterpret_cast<cw::MapCell*>(s.copy);
}

void LabelCells::clear() {
    if (++epoch_ == 0) {
        // Wrapped after four billion worlds: start the slots over rather than
        // let an old epoch match again.
        for (Slot& s : slots_) s.epoch = 0;
        epoch_ = 1;
    }
}

}  // namespace rr
