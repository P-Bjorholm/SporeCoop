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

static float creature_tag(void* p) { return game::bounding_radius(p) * 1.3f + 0.3f; }

// ---------------------------------------------------------------- space: UFOs
// cGameDataUFO: cGameData 0x00, cLocomotiveObject 0x34 (pos +0x38, rot +0x44, vel +0x1FC),
// cCombatant 0x508 (hp +0x540, max +0x534, invincible +0x588)
constexpr uintptr_t kUfoLoco = 0x34, kUfoCombat = 0x508;

static uintptr_t S(const char* n) { return g_host_for_modes ? g_host_for_modes->sym(n) : 0; }

static uint8_t* space_player_data()
{
    uintptr_t p = S("Simulator::SpacePlayerData::ptr");
    return p ? *(uint8_t**)p : nullptr;
}

static void* space_local()
{
    uintptr_t get = S("Simulator::cSimulatorPlayerUFO::Get");
    void* mgr = get ? skg::cdecl_call<void*>(get) : nullptr;
    return mgr ? skg::field<void*>(mgr, 0x40) : nullptr;
}

static uint32_t space_place()
{
    uint8_t* spd = space_player_data();
    if (!spd)
        return 0;
    int context = *(int*)(spd + 0x10); // 0 planet, 1 solar system, 2 galaxy
    void* planet = *(void**)(spd + 0x04);
    void* star = *(void**)(spd + 0x08);
    if (context == 0 && planet) {
        void* rec = skg::field<void*>(planet, 0x13C);                    // cPlanet::mpPlanetRecord
        return rec ? (skg::field<uint32_t>(rec, 0x184) | 0x40000000u) : 0; // PlanetID
    }
    if (context == 1 && star) {
        void* rec = skg::field<void*>(star, 0x48);                // cStar::mpStarRecord
        return rec ? (skg::field<uint32_t>(rec, 0x70) | 0x80000000u) : 0; // StarID
    }
    return 0; // galaxy view: not shown
}

static void space_fill(void* u, net::PlayerState& st)
{
    st.flags |= net::PF_IN_WORLD;
    auto& p = skg::field<skg::Vector3>(u, kUfoLoco + 0x04);
    auto& q = skg::field<skg::Quaternion>(u, kUfoLoco + 0x10);
    auto& v = skg::field<skg::Vector3>(u, kUfoLoco + 0x1C8);
    st.pos = {p.x, p.y, p.z};
    st.rot = {q.x, q.y, q.z, q.w};
    st.vel = {v.x, v.y, v.z};
    if (drive::speed(st.vel) > 0.3f)
        st.flags |= net::PF_MOVING;
    st.health = skg::field<float>(u, kUfoCombat + 0x38);
    st.max_health = skg::field<float>(u, kUfoCombat + 0x2C);
}

static void* space_spawn(const net::PlayerState& s, void* local)
{
    uintptr_t spawn = S("Simulator::SpawnUFO"), empid = S("Simulator::GetPlayerEmpireID"),
              getemp = S("Simulator::GetPlayerEmpire");
    if (!spawn || !empid || !getemp)
        return nullptr;
    uint32_t pol = skg::cdecl_call<uint32_t>(empid);
    void* emp = skg::cdecl_call<void*>(getemp);
    if (!emp)
        return nullptr;
    skg::ResourceKey model = skg::field<skg::ResourceKey>(emp, 0xB8); // cEmpire::mUFOKey
    // UfoType 6 = Defender: friendly to its political id (ours)
    void* u = skg::cdecl_call<void*>(spawn, 6, &pol, &model);
    if (!u)
        return nullptr;
    skg::field<uint8_t>(u, kUfoCombat + 0x80) = 1; // mbInvincible
    skg::Vector3 p{s.pos.x, s.pos.y, s.pos.z};
    skg::Quaternion q{s.rot.x, s.rot.y, s.rot.z, s.rot.w};
    skg::vcall<void>((uint8_t*)u + kUfoLoco, 0x44 / 4, &p, &q); // Teleport
    (void)local;
    return u;
}

static void space_drive(void* u, const net::PlayerState& s, drive::State&, double)
{
    // UFOs fly freely: place the puppet exactly where the remote ship is, every frame
    skg::Vector3 p{s.pos.x, s.pos.y, s.pos.z};
    skg::Quaternion q{s.rot.x, s.rot.y, s.rot.z, s.rot.w};
    skg::vcall<void>((uint8_t*)u + kUfoLoco, 0x38 / 4, &p); // SetPosition
    skg::vcall<void>((uint8_t*)u + kUfoLoco, 0x3C / 4, &q); // SetOrientation
    skg::field<skg::Vector3>(u, kUfoLoco + 0x1C8) = {s.vel.x, s.vel.y, s.vel.z};
    // the UFO update steers towards mNextPosition/mDestination: pin them to the remote state
    skg::field<skg::Vector3>(u, 0x718) = p;    // mNextPosition
    skg::field<skg::Vector3>(u, 0x724) = {s.vel.x, s.vel.y, s.vel.z}; // mNextVelocity
    skg::field<skg::Quaternion>(u, 0x730) = q; // mNextOrientation
    skg::field<skg::Vector3>(u, 0x750) = p;    // mDestination
    skg::field<uint8_t>(u, 0x74C) = 1;         // mbAtDestination
    float mh = skg::field<float>(u, kUfoCombat + 0x2C);
    if (mh > 0)
        skg::field<float>(u, kUfoCombat + 0x38) = mh;
}

static void space_despawn(void* u) { game::destroy_creature(u); } // DestroyInstance works for any cGameData

static float space_tag(void*) { return 4.f; }

static const Adapter kAdapters[] = {
    {"Creature", kCreature, nullptr, false, creature_local, creature_fill, creature_spawn, creature_drive, creature_despawn, creature_tag},
    {"Adventure", kScenario, nullptr, false, creature_local, creature_fill, creature_spawn, creature_drive, creature_despawn, creature_tag},
    {"Space", kSpace, space_place, false, space_local, space_fill, space_spawn, space_drive, space_despawn, space_tag},
};

net::Vec3 entity_pos(const Adapter* a, void* e)
{
    net::PlayerState st;
    if (a && e)
        a->fill(e, st);
    return st.pos;
}

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

uint32_t current_place(const Adapter* a)
{
    if (a && a->place)
        return a->place();
    return current_world();
}

uint32_t current_world()
{
    std::string n = current_world_name();
    return n.empty() ? 0 : net::crc32_update(0, (const uint8_t*)n.data(), n.size());
}

} // namespace modes

const SkHost* g_host_for_modes;
