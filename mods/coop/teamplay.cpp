#include "teamplay.h"

#include <cmath>
#include <cstdio>
#include <map>

#include "game.h"
#include "net/interp.h"

namespace teamplay {

static const SkHost* H;
static SkMod* M;
static Ctx C;
static bool g_on = true;
static double g_last_rescue = -1000, g_last_regen = 0;
static uint32_t g_joint_hits = 0, g_rescues = 0;

struct Hit {
    uint8_t player;
    double t;
};
static std::map<void*, std::vector<Hit>> g_hits; // target -> recent hits by players

void init(const SkHost* host, SkMod* mod, Ctx ctx)
{
    H = host;
    M = mod;
    C = std::move(ctx);
    g_on = H->config_int(M, "coop", "teamplay", 1) != 0;
}

bool enabled() { return g_on; }
void set_enabled(bool on) { g_on = on; }

float on_player_hit(void* target, uint8_t player, float damage)
{
    if (!g_on || !target)
        return damage;
    double now = H->time_seconds();
    auto& v = g_hits[target];
    bool joint = false;
    for (size_t i = 0; i < v.size();) {
        if (now - v[i].t > kHuntWindow) {
            v.erase(v.begin() + i);
            continue;
        }
        joint |= v[i].player != player;
        i++;
    }
    v.push_back({player, now});
    if (g_hits.size() > 256) // forget old targets
        for (auto it = g_hits.begin(); it != g_hits.end();)
            it = it->second.empty() ? g_hits.erase(it) : std::next(it);
    if (!joint)
        return damage;
    g_joint_hits++;
    return damage * kHuntBonus;
}

static std::string nearest_partner(void* avatar, float max_dist, float* out_dist = nullptr)
{
    if (!C.partners || !avatar)
        return {};
    net::Vec3 me = game::position(avatar);
    std::string best;
    float bd = max_dist;
    for (auto& [name, p] : C.partners()) {
        float d = net::dist(me, p);
        if (d <= bd) {
            bd = d;
            best = name;
        }
    }
    if (out_dist)
        *out_dist = bd;
    return best;
}

float on_own_damage(void* avatar, float damage)
{
    if (!g_on || !avatar || damage <= 0)
        return damage;
    float hp = game::health(avatar);
    if (hp <= 0 || hp - damage > 0)
        return damage;
    double now = H->time_seconds();
    if (now - g_last_rescue < kRescueCooldown)
        return damage;
    std::string partner = nearest_partner(avatar, kRescueDist);
    if (partner.empty())
        return damage;
    g_last_rescue = now;
    g_rescues++;
    // the hit lands, but the pack pulls us up: 40 % health afterwards
    float mx = game::max_health(avatar), keep = 0.4f * mx;
    float before = std::fmin(mx, keep + damage);
    game::set_health(avatar, before);
    if (C.notice)
        C.notice(partner + " saved " + C.my_name() + " from death");
    return before - keep;
}

void tick(void* avatar, double now)
{
    if (!g_on || !avatar || now - g_last_regen < 1.0)
        return;
    g_last_regen = now;
    float hp = game::health(avatar), mx = game::max_health(avatar);
    if (hp <= 0 || hp >= mx || nearest_partner(avatar, kRegenDist).empty())
        return;
    game::set_health(avatar, std::fmin(mx, hp + kRegenPerSec * mx));
}

std::string status()
{
    if (!g_on)
        return "";
    void* av = game::avatar();
    float d = 0;
    std::string p = av ? nearest_partner(av, kRescueDist, &d) : std::string();
    double now = H->time_seconds();
    char b[200];
    int cd = (int)(kRescueCooldown - (now - g_last_rescue));
    snprintf(b, sizeof b, "Pack: %s  |  rescue %s  |  joint hits %u, rescues %u",
             p.empty() ? "alone" : (p + (d <= kRegenDist ? " close (regen)" : " near")).c_str(),
             cd > 0 ? (std::to_string(cd) + "s").c_str() : "ready", g_joint_hits, g_rescues);
    return b;
}

} // namespace teamplay
