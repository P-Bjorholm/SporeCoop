// Snapshot interpolation buffer for remote player states (portable).
#pragma once

#include <algorithm>
#include <cmath>
#include <deque>

#include "protocol.h"

namespace net {

inline Vec3 lerp(const Vec3& a, const Vec3& b, float t)
{
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

inline Quat nlerp(const Quat& a, Quat b, float t)
{
    float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (d < 0) {
        b.x = -b.x; b.y = -b.y; b.z = -b.z; b.w = -b.w;
    }
    Quat q{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
    float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (n > 1e-6f) {
        q.x /= n; q.y /= n; q.z /= n; q.w /= n;
    }
    return q;
}

inline float dist(const Vec3& a, const Vec3& b)
{
    float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

class SnapshotBuffer {
public:
    double delay = 0.10;       // interpolation delay (s)
    double max_extrap = 0.25;  // extrapolation limit (s)

    void clear()
    {
        snaps_.clear();
        have_offset_ = false;
    }

    void push(const PlayerState& s, double local_now)
    {
        double st = s.time_ms / 1000.0;
        if (!snaps_.empty() && st <= snaps_.back().t)
            return; // old or duplicate
        double off = local_now - st;
        if (!have_offset_ || off <= offset_) {
            offset_ = off;
            have_offset_ = true;
        } else // slowly forget an old minimum (clock drift, route changes)
            offset_ = off < offset_ + 0.0002 ? off : offset_ + 0.0002;
        snaps_.push_back({st, s});
        while (snaps_.size() > 64)
            snaps_.pop_front();
    }

    bool empty() const { return snaps_.empty(); }
    const PlayerState& latest() const { return snaps_.back().s; }
    double age(double local_now) const
    {
        return snaps_.empty() ? 1e9 : local_now - (snaps_.back().t + offset_);
    }

    // Interpolated state at (now - delay) in sender time.
    bool sample(double local_now, PlayerState& out) const
    {
        if (snaps_.empty())
            return false;
        double t = local_now - offset_ - delay;
        if (t <= snaps_.front().t) {
            out = snaps_.front().s;
            return true;
        }
        for (size_t i = 1; i < snaps_.size(); i++) {
            const auto& a = snaps_[i - 1];
            const auto& b = snaps_[i];
            if (t <= b.t) {
                out = b.s;
                // no blend across a gap: a snapshot of another mode / world, or one without the
                // player in the world (pos 0,0,0 then) is no point on the path. Blending from it
                // put a puppet that appeared after the editor, a load or a respawn somewhere
                // between the planet's centre and the surface, and the herd made for it kept that
                // home (crash 0xB88BEA in the herd's land search, docs/re/creature.md 10)
                if (a.s.mode != b.s.mode || a.s.world != b.s.world || ((a.s.flags ^ b.s.flags) & PF_IN_WORLD))
                    return true;
                float f = (float)((t - a.t) / (b.t - a.t));
                out.pos = lerp(a.s.pos, b.s.pos, f);
                out.rot = nlerp(a.s.rot, b.s.rot, f);
                return true;
            }
        }
        // extrapolate from the newest snapshot
        const auto& n = snaps_.back();
        double dt = t - n.t;
        if (dt > max_extrap)
            dt = max_extrap;
        out = n.s;
        out.pos.x += n.s.vel.x * (float)dt;
        out.pos.y += n.s.vel.y * (float)dt;
        out.pos.z += n.s.vel.z * (float)dt;
        return true;
    }

private:
    struct Snap {
        double t;
        PlayerState s;
    };
    std::deque<Snap> snaps_;
    double offset_ = 0;
    bool have_offset_ = false;
};

// Velocity of a remote object from its snapshots (for objects whose snapshots carry no velocity):
// update() on every snapshot, at(now) = where it should be now (moved on for the snapshot's age)
struct VelocityEstimate {
    Vec3 pos{}, vel{};
    double t = 0;
    void update(const Vec3& p, double now)
    {
        double dt = now - t;
        if (t > 0 && dt > 0.02 && dt < 1.0) {
            Vec3 v{(p.x - pos.x) / (float)dt, (p.y - pos.y) / (float)dt, (p.z - pos.z) / (float)dt};
            bool jump = dist(p, pos) > 200.f;
            vel = jump ? Vec3{} : Vec3{vel.x + (v.x - vel.x) * 0.6f, vel.y + (v.y - vel.y) * 0.6f, vel.z + (v.z - vel.z) * 0.6f};
        } else if (dt >= 1.0)
            vel = {};
        pos = p;
        t = now;
    }
    // latency: the one-way delay before the snapshot arrived
    Vec3 at(double now, double latency = 0.03) const
    {
        float a = (float)std::min(std::max(now - t + latency, 0.0), 0.4);
        return {pos.x + vel.x * a, pos.y + vel.y * a, pos.z + vel.z * a};
    }
};

} // namespace net
