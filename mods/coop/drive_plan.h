// What drive::step does with a puppet in one frame, as a pure decision (unit test:
// tests/npc_tests.cpp). Portable: no game calls.
#pragma once

namespace drive {

struct Track {
    double stuck_since = 0; // the remote stands still and the puppet is more than 1 m off
    double far_since = 0;   // BUG-0169: the puppet is more than kFar off and gets no closer
    float far_best = 0;
    double far_quiet_until = 0; // after such a teleport: not again for a while (a spot it cannot stand on)
};

struct Plan {
    bool snap = false;  // teleport: too far
    bool walk = false;  // walk with the game's locomotion
    bool stuck = false; // teleport: the remote stands still, the puppet could not reach it
    bool stall = false; // teleport: far off for seconds whatever the remote's speed (BUG-0169)
};

constexpr float kFar = 2.5f;     // m
constexpr double kFarHold = 3.0; // s without getting 0.5 m closer
constexpr double kFarQuiet = 10.0;

// err: puppet to target (m), sp: the remote's speed (m/s), snap: teleport distance.
// stall_fix off: the code before BUG-0169 (a remote that reports a speed, e.g. an NPC pushing
// against a rock, keeps the puppet walking to a spot it cannot reach, for as long as it pushes)
inline Plan plan(Track& t, float err, float sp, double now, float snap, bool stall_fix = true)
{
    Plan p;
    if (err > snap)
        p.snap = true;
    else if (sp > 0.3f || err > 0.8f)
        p.walk = true;
    if (sp < 0.3f && err > 1.0f) {
        if (t.stuck_since == 0)
            t.stuck_since = now;
        else if (now - t.stuck_since > 1.5) {
            p.stuck = true;
            t.stuck_since = 0;
        }
    } else
        t.stuck_since = 0;
    if (!stall_fix || p.snap || p.stuck || err <= kFar || now < t.far_quiet_until) {
        t.far_since = 0;
        return p;
    }
    if (t.far_since == 0 || err < t.far_best - 0.5f) {
        t.far_since = now;
        t.far_best = err;
    } else if (now - t.far_since > kFarHold) {
        p.stall = true;
        t.far_since = 0;
        t.far_quiet_until = now + kFarQuiet;
    }
    return p;
}

} // namespace drive
