// Snapshot interpolation buffer for remote player states (portable).
#pragma once

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
                float f = (float)((t - a.t) / (b.t - a.t));
                out = b.s;
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

} // namespace net
