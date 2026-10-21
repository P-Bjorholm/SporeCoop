// Drives a locally simulated creature ("puppet") towards a remotely owned state:
// walk with the game's locomotion for small errors (so it animates), teleport for
// big ones, keep the orientation when idle.
#pragma once

#include <cmath>

#include "game.h"
#include "net/interp.h"

namespace drive {

struct State {
    double last_cmd = 0;
    double stuck_since = 0;
    net::Vec3 last_target;
};

inline float speed(const net::Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }

inline void step(void* p, const net::Vec3& pos, const net::Quat& rot, const net::Vec3& vel, State& ds, double now)
{
    net::Vec3 cur = game::position(p);
    float err = net::dist(cur, pos);
    float sp = speed(vel);
    if (err > 8.f) {
        game::teleport(p, pos, rot);
        ds.last_cmd = now;
    } else if (sp > 0.3f || err > 0.8f) {
        // walk towards a point slightly ahead of the interpolated position
        net::Vec3 tgt{pos.x + vel.x * 0.2f, pos.y + vel.y * 0.2f, pos.z + vel.z * 0.2f};
        if (now - ds.last_cmd > 0.1 || net::dist(tgt, ds.last_target) > 1.f) {
            game::set_speed(p, sp > 0.5f ? sp : 2.f);
            game::move_to(p, tgt, 0.3f);
            ds.last_cmd = now;
            ds.last_target = tgt;
        }
    } else {
        game::set_orientation(p, rot);
    }
    // the remote stands still but the puppet could not reach it (rocks, other creatures)
    if (sp < 0.3f && err > 1.0f) {
        if (ds.stuck_since == 0)
            ds.stuck_since = now;
        else if (now - ds.stuck_since > 1.5) {
            game::teleport(p, pos, rot);
            ds.stuck_since = 0;
        }
    } else
        ds.stuck_since = 0;
}

} // namespace drive
