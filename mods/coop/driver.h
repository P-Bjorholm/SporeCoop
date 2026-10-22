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

// age: how old `pos` is (a snapshot received `age` seconds ago): the target moves on with the
// remote velocity, so the puppet does not trail a walking creature by its latency
// nudge: also pull the puppet a part of the way every frame (it cannot walk faster than the
// creature's top speed, so a walking puppet would otherwise trail by its latency)
inline void step(void* p, const net::Vec3& pos_in, const net::Quat& rot, const net::Vec3& vel, State& ds, double now,
                 double age = 0, bool nudge = false)
{
    float a = (float)std::min(std::max(age, 0.0), 0.5);
    net::Vec3 pos{pos_in.x + vel.x * a, pos_in.y + vel.y * a, pos_in.z + vel.z * a};
    net::Vec3 cur = game::position(p);
    float err = net::dist(cur, pos);
    float sp = speed(vel);
    if (err > 8.f) {
        game::teleport(p, pos, rot);
        ds.last_cmd = now;
    } else if (sp > 0.3f || err > 0.8f) {
        // walk towards a point slightly ahead, faster than the remote while behind (catch up)
        net::Vec3 tgt{pos.x + vel.x * 0.25f, pos.y + vel.y * 0.25f, pos.z + vel.z * 0.25f};
        float want_sp = std::min((sp > 0.5f ? sp : 2.f) + err * 1.2f, std::max(sp, 2.f) * 2.5f);
        if (now - ds.last_cmd > 0.1 || net::dist(tgt, ds.last_target) > 1.f) {
            game::set_speed(p, want_sp);
            game::move_to(p, tgt, 0.3f);
            ds.last_cmd = now;
            ds.last_target = tgt;
        }
        if (nudge && err > 0.5f) {
            const float k = 0.15f;
            game::set_position(p, {cur.x + (pos.x - cur.x) * k, cur.y + (pos.y - cur.y) * k, cur.z + (pos.z - cur.z) * k});
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
