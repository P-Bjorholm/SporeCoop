// Small portable decisions of the Space co-op (spacecoop.cpp, space_map.cpp, space_allies.cpp),
// kept free of game calls for the unit tests (tests/space_tests.cpp, make test).
#pragma once

#include <cstdint>
#include <iterator>
#include <map>
#include <set>
#include <vector>

namespace spacelogic {

// ---------------- the co-op jump's target (BUG-0072) ----------------
struct JumpPartner {
    uint8_t id;
    uint32_t star; // the partner's star, or the last one known; 0 = not known
};

// The partner a jump goes to: `to` = that partner, 0xFF = any partner. With `skip_mine`, partners
// in my own star system are passed over (the co-op window's button names the first partner with a
// known star that is not mine and must jump to that one). The old jump(0xFF) took the first
// partner with any known star (skip_mine = false): with 3 players and the first partner in my
// system, every press was refused.
inline bool pick_jump_target(const std::vector<JumpPartner>& partners, uint32_t here, uint8_t to, bool skip_mine,
                             uint8_t& id, uint32_t& star)
{
    for (auto& p : partners)
        if ((p.id == to || to == 0xFF) && p.star && (!skip_mine || p.star != here)) {
            id = p.id;
            star = p.star;
            return true;
        }
    return false;
}

// ---------------- raids I aborted on mirror colonies (BUG-0136) ----------------
// space_allies keeps the raids it aborted (pointers only compared) so that a watched raid that
// ends by its abort does not count as won. An aborted raid that was never watched (still accepted,
// state 2) used to stay in the set for good, also across sessions: a later raid at the recycled
// address that was won counted as aborted, and the owner's raid was never completed. After every
// look at the watched raids, only aborted raids that are watched stay.
template <typename Watched>
inline void prune_aborted(std::set<void*>& aborted, const Watched& watched)
{
    for (auto it = aborted.begin(); it != aborted.end();)
        it = watched.count(*it) ? std::next(it) : aborted.erase(it);
}

} // namespace spacelogic
