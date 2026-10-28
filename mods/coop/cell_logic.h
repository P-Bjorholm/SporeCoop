// Portable decisions of the Cell stage sync (cellsync.cpp, modes.cpp), unit-tested in
// tests/cell_tests.cpp (make test). Each takes the switch of its fix (`coop.pool fix <name> 0|1`):
// false gives the old behaviour, so the unit test shows that it catches the bug.
#pragma once

#include <algorithm>
#include <bitset>
#include <cmath>
#include <cstdint>
#include <deque>
#include <utility>

namespace cell_logic {

// BUG-0083: a cell pool slot is live only in [0, highWater) and inside the allocation [0, capacity).
// The old test (`slot > highWater`) let slot == highWater through, one object past the used range.
inline bool slot_in_pool(uint32_t slot, uint32_t high_water, uint32_t capacity, bool fix)
{
    if (!fix)
        return slot <= high_water;
    return slot < high_water && slot < capacity;
}

// BUG-0032: the chunks of the host's state rounds (MSG_CELL_STATE {cycle, chunk, chunks}), unreliable.
// The guest removes the mirrors a round did not list only when it saw every chunk of that round; a
// chunk of an older round (reordered) is dropped instead of starting the round again.
struct Rounds {
    uint16_t cycle = 0xFFFF;
    bool started = false;
    std::bitset<256> got;

    enum Verdict { NEW_ROUND, SAME_ROUND, OLD_CHUNK };
    // a chunk arrived: which round it belongs to (the caller clears its "seen" set on NEW_ROUND)
    Verdict on_chunk(uint16_t c, uint8_t chunk, bool fix)
    {
        Verdict v = SAME_ROUND;
        if (!started || c != cycle) {
            // within 64 rounds behind (6 s at 10 Hz): a late chunk. Further back: the host started
            // counting again (a new session), take it as new.
            int16_t d = (int16_t)(uint16_t)(c - cycle);
            if (fix && started && d < 0 && d > -64)
                return OLD_CHUNK;
            cycle = c;
            started = true;
            got.reset();
            v = NEW_ROUND;
        }
        got.set(chunk);
        return v;
    }
    // the last chunk of the round arrived: may the unlisted mirrors be removed?
    bool complete(uint8_t chunks, bool fix) const
    {
        if (!fix)
            return true;
        for (unsigned i = 0; i < chunks; i++)
            if (!got.test(i))
                return false;
        return true;
    }
    void reset() { *this = Rounds{}; }
};

// BUG-0051: the guest's meals sent to the host and not yet in a total the host sent back.
// The host echoes the last meal number it applied (MSG_CELL_FOOD ack); the guest sets its total to
// host total + what is still on the way, instead of the host total alone (which took a fresh meal
// away again until the next total, a level down and up when the meal crossed a level line).
struct Meals {
    uint32_t seq = 0;
    std::deque<std::pair<uint32_t, int>> unacked; // meal number, points

    uint32_t sent(int points)
    {
        ++seq;
        if (points)
            unacked.push_back({seq, points});
        return seq;
    }
    // the change the guest applies to its own total for a host total (has_ack: a host that sends it)
    int diff(int host_total, bool has_ack, uint32_t ack, int pending, int local_total, bool fix)
    {
        if (!fix || !has_ack)
            return host_total - local_total;
        while (!unacked.empty() && (int32_t)(ack - unacked.front().first) >= 0)
            unacked.pop_front();
        int on_way = pending;
        for (auto& m : unacked)
            on_way += m.second;
        return host_total + on_way - local_total;
    }
    void reset() { *this = Meals{}; }
};

// BUG-0041: the avatar's target size after a small respawn, scaled by the body factor (0.5 .. 1).
// `before` / `after`: the target size around the game's AddFoodPoints. A meal that crossed a level
// line made the game resize the avatar to the full size of the new level inside AddFoodPoints: the
// old code took that full size for the small one and divided it by the factor again (up to 2x).
struct Body {
    float factor;  // the new body factor (1: full size, nothing more to do)
    float target;  // the target size to write
};
inline Body regrow(float before, float after, float body, int amount, bool fix)
{
    bool resized = std::abs(after - before) > before * 0.01f;
    if (fix && resized)
        return {1.f, after}; // the game sized it for the new level: full size again
    if (amount <= 0 || body >= 1.f || after <= 0)
        return {body, after};
    float f = std::min(1.f, body + amount * 0.01f);
    float full = after / body;
    return {f, full * f};
}

// BUG-0061: a remote player's "knocked out" is refreshed every 0.25 s by its owner. Without a
// refresh for this long it is stale (the player left, its "up again" was lost).
constexpr double kKoStale = 2.0;
inline bool ko_stale(double now, double refreshed, bool fix) { return fix && now - refreshed > kKoStale; }

} // namespace cell_logic
