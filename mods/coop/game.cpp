#include "game.h"

#include <algorithm>
#include <vector>

#include "sporekit_game.hpp"

using namespace skg;

namespace game {

static const SkHost* H;
static SkMod* M;
static bool g_ready;

static uintptr_t a_NounGet, a_DestroyInstance, a_CreatureCreate, a_NPCTickAI, a_ModeMgrGet;
static uintptr_t a_SpeciesMgrGet, a_GetSpecies, a_CreateHerd;

typedef void(SK_CDECL* AddEvo_t)(float);
static AddEvo_t o_AddEvo;
static DnaListener g_dna_listener;

static void SK_CDECL h_AddEvo(float delta)
{
    if (g_dna_listener && !g_dna_listener(delta))
        return;
    o_AddEvo(delta);
}

static std::vector<void*> g_puppets;

// ---- registry via cCreatureAnimal::Update (thiscall, int deltaTime) ----
typedef void(SK_THISCALL* Update_t)(void*, int);
static Update_t o_Update;
static std::vector<void*> g_seen, g_seen_out;
static UpdateFilter g_update_filter;

static void SK_THISCALL h_Update(void* self, int dt)
{
    g_seen.push_back(self);
    if (g_update_filter && !g_update_filter(self))
        return;
    o_Update(self, dt);
}

const std::vector<void*>& creatures_updated()
{
    g_seen_out.swap(g_seen);
    g_seen.clear();
    std::sort(g_seen_out.begin(), g_seen_out.end());
    g_seen_out.erase(std::unique(g_seen_out.begin(), g_seen_out.end()), g_seen_out.end());
    return g_seen_out;
}

void set_update_filter(UpdateFilter fn) { g_update_filter = fn; }
bool is_destroyed(void* c) { return field<uint8_t>(c, 0x21) != 0; } // cGameData::mbIsDestroyed

// ---- combat via cCreatureAnimal TakeDamage (cCombatant vtable[6], this = creature+0x5A8) ----
typedef int(SK_THISCALL* TakeDamage_t)(void*, float, uint32_t, int, const void*, void*);
static TakeDamage_t o_TakeDamage;
static DamageFilter g_damage_filter;

static int SK_THISCALL h_TakeDamage(void* combatant, float dmg, uint32_t pol, int type, const void* pos, void* attacker)
{
    void* self = (uint8_t*)combatant - kCombatOff;
    void* att = attacker ? (uint8_t*)attacker - kCombatOff : nullptr;
    if (g_damage_filter && !g_damage_filter(self, dmg, type, att))
        return 0;
    return o_TakeDamage(combatant, dmg, pol, type, pos, attacker);
}

void set_damage_filter(DamageFilter fn) { g_damage_filter = fn; }

void apply_damage(void* c, float dmg, int type, void* attacker)
{
    if (!o_TakeDamage)
        return;
    Vector3 pos = field<Vector3>(c, kPosOff);
    o_TakeDamage((uint8_t*)c + kCombatOff, dmg, attacker ? political_id(attacker) : 0xFFFFFFFFu, type, &pos,
                 attacker ? (uint8_t*)attacker + kCombatOff : nullptr);
}

void set_health(void* c, float hp) { field<float>(c, kHpOff) = hp; }

void hit(void* c, float dmg, int type, void* attacker)
{
    Vector3 pos = field<Vector3>(c, kPosOff);
    h_TakeDamage((uint8_t*)c + kCombatOff, dmg, attacker ? political_id(attacker) : 0xFFFFFFFFu, type, &pos,
                 attacker ? (uint8_t*)attacker + kCombatOff : nullptr);
}
static uintptr_t a_PlayAnim, a_PlayAnimTo;
static AnimListener g_anim_listener;
static bool g_in_puppet_anim;

typedef int(SK_THISCALL* PlayAnim_t)(void*, uint32_t, int, int);
typedef int(SK_THISCALL* PlayAnimTo_t)(void*, uint32_t, void*, int, int);
static PlayAnim_t o_PlayAnim;
static PlayAnimTo_t o_PlayAnimTo;

static int SK_THISCALL h_PlayAnim(void* self, uint32_t id, int block, int disable)
{
    if (g_anim_listener && !g_in_puppet_anim)
        g_anim_listener(self, id, block, (disable & 0xFF) != 0, false);
    return o_PlayAnim(self, id, block, disable);
}

static int SK_THISCALL h_PlayAnimTo(void* self, uint32_t id, void* other, int a, int block)
{
    if (g_anim_listener && !g_in_puppet_anim)
        g_anim_listener(self, id, block, false, true);
    return o_PlayAnimTo(self, id, other, a, block);
}

void set_anim_listener(AnimListener fn) { g_anim_listener = fn; }

void play_animation(void* c, uint32_t anim_id, int block, bool disable_blend)
{
    g_in_puppet_anim = true;
    o_PlayAnim(c, anim_id, block, disable_blend ? 1 : 0);
    g_in_puppet_anim = false;
}

typedef void(SK_THISCALL* NPCTickAI_t)(void*, float);
static NPCTickAI_t o_NPCTickAI;

static void SK_THISCALL h_NPCTickAI(void* self, float dt)
{
    if (is_puppet(self))
        return; // remote players drive their puppets, no local AI
    o_NPCTickAI(self, dt);
}

bool init(const SkHost* host, SkMod* mod)
{
    H = host;
    M = mod;
    static const char* req[] = {
        "Simulator::cGameNounManager::Get",
        "Simulator::cGameNounManager::DestroyInstance",
        "Simulator::cCreatureAnimal::Create",
        "Simulator::cCreatureAnimal::NPCTickAI",
        "App::IGameModeManager::Get",
    };
    if (H->sym_require(M, req, 5) != 0) {
        g_ready = false;
        return false;
    }
    a_NounGet = H->sym(req[0]);
    a_DestroyInstance = H->sym(req[1]);
    a_CreatureCreate = H->sym(req[2]);
    a_NPCTickAI = H->sym(req[3]);
    a_ModeMgrGet = H->sym(req[4]);
    if (H->hook_create(M, (void*)a_NPCTickAI, (void*)h_NPCTickAI, (void**)&o_NPCTickAI) != 0)
        return false;
    if (uintptr_t a = H->sym("Simulator::cCreatureAnimal::Update"))
        H->hook_create(M, (void*)a, (void*)h_Update, (void**)&o_Update);
    if (uintptr_t a = H->sym("Simulator::cCreatureAnimal::TakeDamage"))
        H->hook_create(M, (void*)a, (void*)h_TakeDamage, (void**)&o_TakeDamage);
    if (uintptr_t a = H->sym("Simulator::cCreatureGameData::AddEvolutionPoints"))
        H->hook_create(M, (void*)a, (void*)h_AddEvo, (void**)&o_AddEvo);
    a_SpeciesMgrGet = H->sym("Editors::cSpeciesManager::Get");
    a_GetSpecies = H->sym("Editors::cSpeciesManager::GetSpeciesProfile");
    a_CreateHerd = H->sym("Simulator::cGameNounManager::CreateHerd");
    a_PlayAnim = H->sym("Simulator::cCreatureBase::PlayAnimation");
    a_PlayAnimTo = H->sym("Simulator::cCreatureBase::PlayAnimationTo");
    if (a_PlayAnim)
        H->hook_create(M, (void*)a_PlayAnim, (void*)h_PlayAnim, (void**)&o_PlayAnim);
    if (a_PlayAnimTo)
        H->hook_create(M, (void*)a_PlayAnimTo, (void*)h_PlayAnimTo, (void**)&o_PlayAnimTo);
    g_ready = true;
    return true;
}

bool ready() { return g_ready; }

uint32_t mode_id()
{
    if (!g_ready)
        return 0;
    void* mm = cdecl_call<void*>(a_ModeMgrGet);
    return mm ? vcall<uint32_t>(mm, 0x38 / 4) : 0;
}

bool in_creature_stage() { return mode_id() == 0x1654C01; }

void* avatar()
{
    if (!g_ready || !in_creature_stage())
        return nullptr;
    void* nm = cdecl_call<void*>(a_NounGet);
    if (!nm)
        return nullptr;
    return field<void*>(nm, kNounAvatarOff);
}

net::Vec3 position(void* c)
{
    auto& v = field<Vector3>(c, kPosOff);
    return {v.x, v.y, v.z};
}

net::Quat orientation(void* c)
{
    auto& q = field<Quaternion>(c, kRotOff);
    return {q.x, q.y, q.z, q.w};
}

net::Vec3 velocity(void* c)
{
    auto& v = field<Vector3>(c, kVelOff);
    return {v.x, v.y, v.z};
}

float health(void* c) { return field<float>(c, kHpOff); }
float max_health(void* c) { return field<float>(c, kMaxHpOff); }
bool is_dead(void* c) { return field<uint8_t>(c, kDeadOff) != 0; }
uint32_t species_instance(void* c) { return field<uint32_t>(c, kSpeciesKeyOff); }
void* species_profile(void* c) { return field<void*>(c, kSpeciesOff); }

void* avatar_herd()
{
    if (!g_ready || !in_creature_stage())
        return nullptr;
    void* nm = cdecl_call<void*>(a_NounGet);
    return nm ? field<void*>(nm, kNounAvatarHerdOff) : nullptr;
}

void* spawn_creature(const net::Vec3& pos, void* species, void* herd)
{
    if (!g_ready || !species || !herd)
        return nullptr;
    Vector3 p{pos.x, pos.y, pos.z};
    // static cCreatureAnimal* Create(const Vector3&, cSpeciesProfile*, int age,
    //                                cHerd*, bool isAvatar, bool reuse)   (cdecl)
    return cdecl_call<void*>(a_CreatureCreate, &p, species, 1, herd, 0, 0);
}

void destroy_creature(void* c)
{
    if (!g_ready || !c)
        return;
    puppet_unregister(c);
    void* nm = cdecl_call<void*>(a_NounGet);
    if (nm)
        thiscall<void>(a_DestroyInstance, nm, c);
}

static void* spatial(void* c) { return ptr_add<void>(c, kSpatialOff); }

void teleport(void* c, const net::Vec3& pos, const net::Quat& rot)
{
    Vector3 p{pos.x, pos.y, pos.z};
    Quaternion q{rot.x, rot.y, rot.z, rot.w};
    vcall<void>(spatial(c), 0x44 / 4, &p, &q);
}

void set_orientation(void* c, const net::Quat& rot)
{
    Quaternion q{rot.x, rot.y, rot.z, rot.w};
    vcall<void>(spatial(c), 0x3C / 4, &q);
}

void move_to(void* c, const net::Vec3& dst, float stop_dist)
{
    Vector3 p{dst.x, dst.y, dst.z};
    // cLocomotiveObject::MoveTo(const Vector3&, float goalStop, float acceptableStop, bool)
    vcall<void>(spatial(c), 0xE0 / 4, &p, stop_dist, stop_dist * 2.f, 0);
}

void stop(void* c) { vcall<void>(spatial(c), 0xEC / 4); }

void set_speed(void* c, float speed) { vcall<void>(spatial(c), 0xC4 / 4, speed, 0); }

float scale(void* c) { return vcall<float>(spatial(c), 0x34 / 4); }       // GetScale
float bounding_radius(void* c) { return vcall<float>(spatial(c), 0x70 / 4); } // GetBoundingRadius
void set_scale(void* c, float s) { vcall<void>(spatial(c), 0x40 / 4, s); } // SetScale

Key species_key(void* c)
{
    auto& k = field<ResourceKey>(c, kSpeciesKeyOff);
    return {k.instance, k.type, k.group};
}

void* species_by_key(const Key& k)
{
    if (!a_SpeciesMgrGet || !a_GetSpecies)
        return nullptr;
    void* mgr = cdecl_call<void*>(a_SpeciesMgrGet);
    ResourceKey rk{k.instance, k.type, k.group};
    return mgr ? thiscall<void*>(a_GetSpecies, mgr, &rk) : nullptr;
}

void* create_herd(const net::Vec3& pos, void* species)
{
    if (!a_CreateHerd || !species)
        return nullptr;
    void* nm = cdecl_call<void*>(a_NounGet);
    Vector3 p{pos.x, pos.y, pos.z};
    // CreateHerd(const Vector3&, cSpeciesProfile*, herdSize, isOwnedByAvatar, personality, createNest)
    return nm ? thiscall<void*>(a_CreateHerd, nm, &p, species, 0, 0, 0, 0) : nullptr;
}

void* herd_of(void* c) { return field<void*>(c, kHerdOff); }

uint32_t political_id(void* c) { return vcall<uint32_t>(c, 0x4C / 4); } // cGameData::GetPoliticalID

float dna()
{
    uintptr_t get = H->sym("Simulator::cCreatureGameData::GetEvolutionPoints");
    return get ? cdecl_call<float>(get) : 0.f;
}

void dna_add_raw(float delta)
{
    if (o_AddEvo)
        o_AddEvo(delta);
}

void set_dna_listener(DnaListener fn) { g_dna_listener = fn; }

static void* tod_object()
{
    uintptr_t un = H->sym("SporeCoop::TimeOfDay_Unavailable"), get = H->sym("SporeCoop::TimeOfDay_GetObject");
    if (!un || !get || cdecl_call<int>(un) != 0)
        return nullptr;
    return cdecl_call<void*>(get);
}

bool time_of_day(net::Quat& q)
{
    void* o = tod_object();
    if (!o)
        return false;
    auto& r = field<Quaternion>(o, 0x10); // cSpatialObject::mOrientation
    q = {r.x, r.y, r.z, r.w};
    return true;
}

void set_time_of_day(const net::Quat& q)
{
    void* o = tod_object();
    uintptr_t apply = H->sym("SporeCoop::TimeOfDay_Apply");
    if (!o || !apply)
        return;
    Quaternion r{q.x, q.y, q.z, q.w};
    vcall<void>(o, 0x3C / 4, &r); // SetOrientation
    cdecl_call<void>(apply, o);
}

void puppet_register(void* c)
{
    if (!is_puppet(c))
        g_puppets.push_back(c);
}

void puppet_unregister(void* c)
{
    g_puppets.erase(std::remove(g_puppets.begin(), g_puppets.end(), c), g_puppets.end());
}

bool is_puppet(void* c) { return std::find(g_puppets.begin(), g_puppets.end(), c) != g_puppets.end(); }

} // namespace game
