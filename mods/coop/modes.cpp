#include "modes.h"

#include <cmath>

#include "game.h"
#include "sporekit.h"
#include "sporekit_game.hpp"

extern const SkHost* g_host_for_modes;

namespace modes {

// ---------------------------------------------------------------- creature / captain
static void* creature_local() { return game::avatar(); }

static void creature_fill(void* av, net::PlayerState& st)
{
    st.flags |= net::PF_IN_WORLD;
    st.pos = game::position(av);
    st.rot = game::orientation(av);
    st.vel = game::velocity(av);
    float sp = drive::speed(st.vel);
    if (sp > 0.3f)
        st.flags |= net::PF_MOVING;
    if (sp > 6.f)
        st.flags |= net::PF_RUNNING;
    if (game::is_dead(av))
        st.flags |= net::PF_DEAD;
    st.health = game::health(av);
    st.max_health = game::max_health(av);
    st.species = game::species_instance(av);
}

static void* creature_spawn(const net::PlayerState& s, void* av)
{
    void* p = game::spawn_creature(s.pos, game::species_profile(av), game::avatar_herd());
    if (!p)
        return nullptr;
    game::puppet_register(p);
    game::teleport(p, s.pos, s.rot);
    game::set_scale(p, game::scale(p) * 1.15f);
    return p;
}

static void creature_drive(void* p, const net::PlayerState& s, drive::State& ds, double now)
{
    drive::step(p, s.pos, s.rot, s.vel, ds, now);
    float mh = game::max_health(p); // puppets mirror the remote health, never die locally
    if (mh > 0)
        game::set_health(p, mh);
}

static void creature_despawn(void* p) { game::destroy_creature(p); }

static const Adapter kAdapters[] = {
    {"Creature", kCreature, creature_local, creature_fill, creature_spawn, creature_drive, creature_despawn},
    {"Adventure", kScenario, creature_local, creature_fill, creature_spawn, creature_drive, creature_despawn},
};

const Adapter* for_mode(uint32_t mode)
{
    for (auto& a : kAdapters)
        if (a.mode == mode)
            return &a;
    return nullptr;
}

const char* mode_name(uint32_t m)
{
    switch (m) {
    case kCell: return "Cell";
    case kCreature: return "Creature";
    case kTribe: return "Tribe";
    case kCiv: return "Civilization";
    case kSpace: return "Space";
    case kEdit: case kEditor: return "Editor";
    case kLoad: return "Loading";
    case kScenario: return "Adventure";
    case kMenu: return "Galaxy menu";
    case 0: return "-";
    default: return "other";
    }
}

std::string current_world_name()
{
    uintptr_t a = g_host_for_modes ? g_host_for_modes->sym("SporeCoop::CurrentGameName") : 0;
    if (!a)
        return {};
    const char16_t* b = skg::field<const char16_t*>((void*)a, 0);
    const char16_t* e = skg::field<const char16_t*>((void*)a, 4);
    std::string out;
    if (!b || e < b || e - b > 260 || !skg::readable(b, (e - b) * 2))
        return {};
    for (; b < e; b++)
        out += *b < 128 ? (char)*b : '_';
    return out;
}

uint32_t current_world()
{
    std::string n = current_world_name();
    return n.empty() ? 0 : net::crc32_update(0, (const uint8_t*)n.data(), n.size());
}

} // namespace modes

const SkHost* g_host_for_modes;
