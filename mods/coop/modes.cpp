#include "modes.h"

#include <windows.h>

#include <cmath>
#include <cstring>
#include <unordered_map>

#include "game.h"
#include "sporekit.h"
#include "sporekit_game.hpp"

extern const SkHost* g_host_for_modes;
extern SkMod* g_mod_for_modes;

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

// ---------------------------------------------------------------- cell stage
// cCellGame (*0x16B4BE4): mCells pool at +0x1C (mpData +0, object size +0x14), mpCellQuery +0x40FC,
// mAvatarCellIndex +0x411C. cCellObjectData (0x398 bytes): index +0, mIsIdle +4, mTargetPosition +8,
// mTransform +0x48 (offset +0x4C, scale +0x58, rotation 3x3 +0x5C), mCellResource +0x108,
// mIsInvulnerable +0x111, mChaseCellIndex +0x22C, mHealthPoints +0x244, mScale +0x358.
static uint8_t* cell_game()
{
    uintptr_t p = S("Simulator::Cell::cCellGame::_ptr");
    return p ? *(uint8_t**)p : nullptr;
}

// cObjectPoolIndex = slot (low 16 bits) | generation (high 16); the object's first field holds
// its own full index while it lives (cObjectPool::GetIfNotDeleted 0xB72290)
static uint8_t* cell_at(uint32_t idx)
{
    uint8_t* g = cell_game();
    uint32_t slot = idx & 0xFFFF;
    if (!g || !idx || slot >= 4096)
        return nullptr;
    uint8_t* data = *(uint8_t**)(g + 0x1C);
    uint32_t size = *(uint32_t*)(g + 0x1C + 0x14);
    if (!data || size != 0x398 || slot > *(uint32_t*)(g + 0x1C + 0x10))
        return nullptr;
    uint8_t* c = data + (size_t)slot * size;
    return *(uint32_t*)c == idx ? c : nullptr;
}

static std::unordered_map<void*, uint32_t> g_cell_ids; // puppet -> its pool index

static void* cell_local()
{
    uint8_t* g = cell_game();
    return g ? cell_at(*(uint32_t*)(g + 0x411C)) : nullptr;
}

static void mat_to_quat(const float* m, net::Quat& q) // row-major 3x3
{
    float tr = m[0] + m[4] + m[8];
    if (tr > 0) {
        float s = std::sqrt(tr + 1.f) * 2;
        q.w = 0.25f * s;
        q.x = (m[7] - m[5]) / s;
        q.y = (m[2] - m[6]) / s;
        q.z = (m[3] - m[1]) / s;
    } else if (m[0] > m[4] && m[0] > m[8]) {
        float s = std::sqrt(1.f + m[0] - m[4] - m[8]) * 2;
        q.w = (m[7] - m[5]) / s;
        q.x = 0.25f * s;
        q.y = (m[1] + m[3]) / s;
        q.z = (m[2] + m[6]) / s;
    } else if (m[4] > m[8]) {
        float s = std::sqrt(1.f + m[4] - m[0] - m[8]) * 2;
        q.w = (m[2] - m[6]) / s;
        q.x = (m[1] + m[3]) / s;
        q.y = 0.25f * s;
        q.z = (m[5] + m[7]) / s;
    } else {
        float s = std::sqrt(1.f + m[8] - m[0] - m[4]) * 2;
        q.w = (m[3] - m[1]) / s;
        q.x = (m[2] + m[6]) / s;
        q.y = (m[5] + m[7]) / s;
        q.z = 0.25f * s;
    }
}

static void quat_to_mat(const net::Quat& q, float* m)
{
    float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z, xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z,
          wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    m[0] = 1 - 2 * (yy + zz); m[1] = 2 * (xy - wz);     m[2] = 2 * (xz + wy);
    m[3] = 2 * (xy + wz);     m[4] = 1 - 2 * (xx + zz); m[5] = 2 * (yz - wx);
    m[6] = 2 * (xz - wy);     m[7] = 2 * (yz + wx);     m[8] = 1 - 2 * (xx + yy);
}

static void cell_fill(void* cp, net::PlayerState& st)
{
    uint8_t* c = (uint8_t*)cp;
    auto* pos = (float*)(c + 0x4C);
    st.flags |= net::PF_IN_WORLD;
    st.pos = {pos[0], pos[1], pos[2]};
    mat_to_quat((float*)(c + 0x5C), st.rot);
    auto* tgt = (float*)(c + 0x08);
    net::Vec3 d{tgt[0] - pos[0], tgt[1] - pos[1], tgt[2] - pos[2]};
    if (!*(bool*)(c + 4) && drive::speed(d) > 0.05f)
        st.flags |= net::PF_MOVING;
    st.health = (float)*(int*)(c + 0x244);
    st.max_health = 6;
    if (st.health <= 0)
        st.flags |= net::PF_DEAD;
    st.species = *(uint32_t*)(c + 0x58); // transform scale (cell size) travels in the species slot
}

// CreateCellObject asks GetModelKeyForCellResource for the model; the avatar's resource
// ("PlayerCreature") maps to the species picked in the creator, not to the evolved cell the
// player has now. While spawning a puppet the hook answers with the avatar's current model.
typedef skg::ResourceKey*(__cdecl* GetModelKey_t)(skg::ResourceKey*, void*);
static GetModelKey_t o_GetModelKey;
static bool g_force_key;
static skg::ResourceKey g_key;

static skg::ResourceKey* __cdecl h_GetModelKey(skg::ResourceKey* ret, void* res)
{
    if (g_force_key) {
        *ret = g_key;
        return ret;
    }
    return o_GetModelKey(ret, res);
}

static void* cell_spawn(const net::PlayerState& s, void* local)
{
    if (!o_GetModelKey) {
        uintptr_t a = S("Simulator::Cell::GetModelKeyForCellResource");
        if (a)
            g_host_for_modes->hook_create(g_mod_for_modes, (void*)a, (void*)h_GetModelKey, (void**)&o_GetModelKey);
    }
    uint8_t* g = cell_game();
    uint8_t* me = (uint8_t*)local;
    uintptr_t create = S("Simulator::Cell::CreateCellObject");
    if (!g || !me || !create)
        return nullptr;
    skg::Vector3 p{s.pos.x, s.pos.y, s.pos.z};
    void* query = *(void**)(g + 0x40FC);
    void* res = *(void**)(me + 0x108);
    // the avatar's own mScale is -1 ("player"); other cells with -1 are not drawn: use the level the
    // player is at (GetCurrentAdvectInfo reports the current CellStageScale)
    int scale = 1;
    if (uintptr_t adv = S("Simulator::Cell::GetCurrentAdvectInfo")) {
        int lvl = -1;
        float a, b, c;
        skg::cdecl_call<uint32_t>(adv, &lvl, &a, &b, &c);
        if (lvl >= 0 && lvl < 8)
            scale = lvl;
    }
    g_key = skg::field<skg::ResourceKey>(me, 0xFC);
    g_force_key = o_GetModelKey && g_key.instance != 0;
    uint32_t idx = skg::cdecl_call<uint32_t>(create, query, &p, 0.f, res, scale, 1.f, 1.f, 1, (void*)nullptr, 0);
    g_force_key = false;
    uint8_t* c = cell_at(idx);
    if (!c)
        return nullptr;
    g_cell_ids[c] = idx;
    *(bool*)(c + 0x111) = true; // invulnerable: nobody eats a friend
    return c;
}

static void cell_drive(void* cp, const net::PlayerState& s, drive::State&, double)
{
    uint8_t* c = (uint8_t*)cp;
    auto* pos = (float*)(c + 0x4C);
    net::Vec3 cur{pos[0], pos[1], pos[2]};
    // the cell game moves cells itself (our write lands after its update): swim towards the remote
    // position like the player's own cell does (mTargetPosition + not idle), snap when far behind
    if (net::dist(cur, s.pos) > 4.f) {
        pos[0] = s.pos.x; pos[1] = s.pos.y; pos[2] = s.pos.z;
    }
    auto* tgt = (float*)(c + 0x08);
    tgt[0] = s.pos.x; tgt[1] = s.pos.y; tgt[2] = s.pos.z;
    *(bool*)(c + 4) = net::dist(cur, s.pos) < 0.2f;
    skg::field<skg::Quaternion>(c, 0x14) = {s.rot.x, s.rot.y, s.rot.z, s.rot.w}; // mTargetOrientation
    float size;
    memcpy(&size, &s.species, 4);
    if (size > 0.01f && size < 1000.f) {
        *(float*)(c + 0x58) = size;
        *(float*)(c + 0xB4) = size; // mTargetSize
    }
    *(int*)(c + 0x22C) = -1; // never chase anything
    *(int*)(c + 0x220) = -1; // nor flee
    *(int*)(c + 0x244) = 6;
    *(bool*)(c + 0x111) = true;
}

static bool cell_alive(void* cp)
{
    auto it = g_cell_ids.find(cp);
    bool ok = it != g_cell_ids.end() && cell_at(it->second) == cp;
    if (!ok && it != g_cell_ids.end())
        g_cell_ids.erase(it);
    return ok;
}

static void cell_despawn(void* cp)
{
    if (!cell_alive(cp))
        return;
    g_cell_ids.erase(cp);
    uint8_t* c = (uint8_t*)cp;
    *(bool*)(c + 0x111) = false;
    *(int*)(c + 0x244) = 0; // the cell game removes dead cells
    *(float*)(c + 0xA4) = 0; // mTargetOpacity: fade out meanwhile
}

static float cell_tag(void* cp) { return *(float*)((uint8_t*)cp + 0x58) * 1.2f + 0.5f; }

// ---------------------------------------------------------------- tribe / civilization
// RTS stages have no single avatar: a player is shown as a pointer - the point of the planet
// under their mouse - with the name tag. Both play the same (host) world, so the tribe or the
// cities themselves already exist on every machine.
struct Marker {
    net::Vec3 pos;
};
static Marker g_local_marker;

static void* noun_manager()
{
    uintptr_t get = S("Simulator::cGameNounManager::Get");
    return get ? skg::cdecl_call<void*>(get) : nullptr;
}

// a point on the planet surface of the player's community (sets the ray-cast sphere radius)
static bool rts_anchor(net::Vec3& p)
{
    void* nm = noun_manager();
    if (!nm)
        return false;
    void* tribe = skg::field<void*>(nm, 0x70);
    if (tribe && skg::readable(tribe, 0x130)) {
        auto v = skg::field<skg::Vector3>(tribe, 0x124);
        p = {v.x, v.y, v.z};
        return drive::speed(p) > 1;
    }
    uintptr_t f = S("Simulator::cGameNounManager::GetPlayerCivilization");
    void* civ = f ? skg::thiscall<void*>(f, nm) : nullptr;
    if (civ && skg::readable(civ, 0xA8)) {
        void** b = skg::field<void**>(civ, 0x9C); // mCities (vector begin)
        void** e = skg::field<void**>(civ, 0xA0);
        if (b && e > b && skg::readable(b, 4) && *b && skg::readable(*b, 0x130)) {
            auto v = skg::field<skg::Vector3>(*b, 0x124);
            p = {v.x, v.y, v.z};
            return drive::speed(p) > 1;
        }
    }
    return false;
}

static void* rts_local()
{
    net::Vec3 a;
    return rts_anchor(a) ? &g_local_marker : nullptr;
}

// planet point under the mouse (ray from the game camera against the planet sphere)
static bool mouse_on_planet(net::Vec3& out)
{
    net::Vec3 anchor;
    if (!rts_anchor(anchor))
        return false;
    HWND w = (HWND)g_host_for_modes->game_window();
    POINT pt;
    if (!w || !GetCursorPos(&pt) || !ScreenToClient(w, &pt))
        return false;
    uintptr_t mmget = S("App::IGameModeManager::Get"), ray = S("App::cViewer::GetCameraToPoint");
    void* mm = mmget ? skg::cdecl_call<void*>(mmget) : nullptr;
    void* viewer = mm ? skg::vcall<void*>(mm, 0x58 / 4) : nullptr;
    if (!viewer || !ray)
        return false;
    skg::Vector3 o{}, d{};
    if (!skg::thiscall<bool>(ray, viewer, (float)pt.x, (float)pt.y, &o, &d))
        return false;
    float dl = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if (dl < 1e-6f)
        return false;
    d.x /= dl; d.y /= dl; d.z /= dl;
    float R = drive::speed(anchor);
    float b = o.x * d.x + o.y * d.y + o.z * d.z;
    float c = o.x * o.x + o.y * o.y + o.z * o.z - R * R;
    float disc = b * b - c;
    if (disc < 0)
        return false;
    float t = -b - std::sqrt(disc);
    if (t < 0)
        t = -b + std::sqrt(disc);
    if (t < 0)
        return false;
    out = {o.x + d.x * t, o.y + d.y * t, o.z + d.z * t};
    return true;
}

static void rts_fill(void* m, net::PlayerState& st)
{
    Marker* mk = (Marker*)m;
    net::Vec3 p;
    if (mk == &g_local_marker && mouse_on_planet(p))
        mk->pos = p;
    st.flags |= net::PF_IN_WORLD;
    st.pos = mk->pos;
}

static void* rts_spawn(const net::PlayerState& s, void*)
{
    auto* m = new Marker;
    m->pos = s.pos;
    return m;
}

static void rts_drive(void* m, const net::PlayerState& s, drive::State&, double) { ((Marker*)m)->pos = s.pos; }
static void rts_despawn(void* m) { delete (Marker*)m; }
static float rts_tag(void*) { return 0.5f; }

static const Adapter kAdapters[] = {
    {"Tribe", kTribe, nullptr, false, rts_local, rts_fill, rts_spawn, rts_drive, rts_despawn, rts_tag},
    {"Civilization", kCiv, nullptr, false, rts_local, rts_fill, rts_spawn, rts_drive, rts_despawn, rts_tag},
    {"Cell", kCell, nullptr, false, cell_local, cell_fill, cell_spawn, cell_drive, cell_despawn, cell_tag, cell_alive, true},
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
SkMod* g_mod_for_modes;
