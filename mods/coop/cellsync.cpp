#include "cellsync.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

#include "game.h"
#include "modes.h"
#include "net/interp.h"
#include "sporekit_game.hpp"

namespace cellsync {

static const SkHost* H;
static SkMod* M;
static Ctx C;
static bool g_ok;
static uintptr_t a_DamageCell, a_KillCell, a_Break, a_Destroy, a_AddFood, a_Populate, a_Rollover, a_GetDamage,
    a_RefCreate, a_Serializer, a_S;

// ---- cCellObjectData fields (docs/re/cell.md) ----
constexpr uintptr_t kTarget = 0x08, kFlags = 0x48, kCounter = 0x4A, kPos = 0x4C, kSize = 0x58, kRot = 0x5C,
                    kPrevPos = 0x84, kVel = 0x90, kTargetSize = 0xB4, kModelKey = 0xFC, kRes = 0x108,
                    kInvuln = 0x111, kDead = 0x112, kDying = 0x113, kAiLock = 0x16C, kHp = 0x244,
                    kKeepAlive = 0x354, kLevel = 0x358, kQuery = 0x35C;

template <typename T>
static T& F(void* p, uintptr_t off) { return *(T*)((uint8_t*)p + off); }

static bool is_host() { return C.sess && C.sess->role() == net::Role::Host && C.sess->connected(); }
static bool is_guest() { return C.sess && C.sess->role() == net::Role::Client && C.sess->connected(); }
static bool in_cell() { return g_ok && game::mode_id() == modes::kCell && modes::cell_game(); }

static uint8_t* G() { return modes::cell_game(); }
static uint32_t avatar_idx() { return G() ? F<uint32_t>(G(), 0x411C) : 0; }
static uint8_t* avatar() { return G() ? modes::cell_at(avatar_idx()) : nullptr; }
static void* fg_query() { return G() ? F<void*>(G(), 0x40FC) : nullptr; }
static uint8_t* S() { return a_S ? *(uint8_t**)a_S : nullptr; }

// every live foreground cell (gameplay layer), avatar excluded
template <typename Fn>
static void for_each_cell(Fn fn)
{
    uint8_t* g = G();
    if (!g)
        return;
    uint8_t* data = F<uint8_t*>(g, 0x1C);
    uint32_t used = F<uint32_t>(g, 0x1C + 0x10), size = F<uint32_t>(g, 0x1C + 0x14);
    if (!data || size != 0x398)
        return;
    void* fq = fg_query();
    uint32_t av = avatar_idx();
    for (uint32_t slot = 0; slot < used && slot < 4096; slot++) {
        uint8_t* c = data + (size_t)slot * size;
        uint32_t idx = F<uint32_t>(c, 0);
        if ((idx & 0xFFFF) != slot || idx == av || F<void*>(c, kQuery) != fq)
            continue;
        fn(c, idx);
    }
}

static net::Vec3 pos_of(uint8_t* c)
{
    auto* p = (float*)(c + kPos);
    return {p[0], p[1], p[2]};
}

static void set_pos(uint8_t* c, const net::Vec3& p)
{
    auto* d = (float*)(c + kPos);
    d[0] = p.x; d[1] = p.y; d[2] = p.z;
    auto* pp = (float*)(c + kPrevPos);
    pp[0] = p.x; pp[1] = p.y; pp[2] = p.z;
    F<uint16_t>(c, kFlags) |= 4;
    F<uint16_t>(c, kCounter)++;
}

// ---------------- host ----------------
static double g_last_send, g_last_food;
static uint16_t g_cycle;
static uint32_t g_hits_in, g_eats_in;
static int32_t g_food_sent = -1, g_plant_sent = -1;

// ---------------- guest ----------------
struct Mirror {
    uint32_t local = 0;
    net::Vec3 pos, vel;
    net::Quat rot;
    float size = 1;
    int hp = 6;
    double seen = 0;
};
static std::unordered_map<uint32_t, Mirror> g_mirrors;   // host index -> mirror
static std::unordered_map<uint32_t, uint32_t> g_host_of; // local index -> host index
static std::unordered_set<uint32_t> g_cycle_seen;
static uint16_t g_rx_cycle = 0xFFFF;
static bool g_our_destroy, g_food_guard;
static uint32_t g_created, g_destroyed, g_hits_sent, g_eats_sent, g_purged;

// ---------------- both: symbiosis ----------------
static double g_last_regen, g_invuln_until;
static uint32_t g_rescues;

static std::string nearest_partner(float max_dist, net::Vec3* where = nullptr)
{
    uint8_t* a = avatar();
    if (!a || !C.partners)
        return {};
    net::Vec3 me = pos_of(a);
    std::string best;
    float bd = max_dist;
    for (auto& [n, p] : C.partners()) {
        float d = net::dist(me, p);
        if (d <= bd) {
            bd = d;
            best = n;
            if (where)
                *where = p;
        }
    }
    return best;
}

// The avatar would die: with a partner in the pool it respawns next to them instead
// (the vanilla death rebuilds the whole level and would wipe every mirror).
static bool save_avatar(uint8_t* c)
{
    if (!c || c != avatar() || !C.sess || !C.sess->connected())
        return false;
    net::Vec3 at;
    std::string partner = nearest_partner(1e9f, &at);
    if (partner.empty())
        return false;
    F<int>(c, kHp) = 6;
    F<uint8_t>(c, kDead) = 0;
    F<uint8_t>(c, kDying) = 0;
    F<uint8_t>(c, kInvuln) = 1;
    g_invuln_until = H->time_seconds() + 3.0;
    net::Vec3 p{at.x + 2.f, at.y + 1.f, 0.f};
    set_pos(c, p);
    auto* t = (float*)(c + kTarget);
    t[0] = p.x; t[1] = p.y; t[2] = p.z;
    auto* v = (float*)(c + kVel);
    v[0] = v[1] = v[2] = 0;
    F<uint8_t>(c, 0x04) = 1; // idle
    g_rescues++;
    if (C.notice)
        C.notice(partner + " saved " + C.my_name() + ": symbiosis");
    return true;
}

// ---------------- hooks ----------------
using DamageCell_t = bool(SK_CDECL*)(uint8_t*, uint8_t*, const float*, int, int, float);
static DamageCell_t o_DamageCell;
static void hurt_avatar(int dmg, int type);

static bool SK_CDECL h_DamageCell(uint8_t* victim, uint8_t* attacker, const float* dir, int type, int x, float dt)
{
    if (!in_cell())
        return o_DamageCell(victim, attacker, dir, type, x, dt);
    if (is_host() && victim && attacker && C.puppet_owner) {
        int owner = C.puppet_owner(victim);
        if (owner > 0) { // a host NPC bites a guest's cell: the guest takes it on its own cell
            int dmg = a_GetDamage ? skg::cdecl_call<int>(a_GetDamage, attacker, victim) : 1;
            net::Writer w;
            w.u32(0xFFFFFFFFu);
            w.u8((uint8_t)std::max(0, std::min(dmg, 255)));
            w.u8((uint8_t)type);
            C.sess->send_reliable_to((uint8_t)owner, net::MSG_CELL_HIT, w);
            return false;
        }
    }
    if (is_guest() && victim) {
        auto it = g_host_of.find(F<uint32_t>(victim, 0));
        if (it != g_host_of.end()) { // a mirror: the host applies the bite
            if (attacker == avatar()) {
                int dmg = a_GetDamage ? skg::cdecl_call<int>(a_GetDamage, attacker, victim) : 1;
                net::Writer w;
                w.u32(it->second);
                w.u8((uint8_t)std::max(0, std::min(dmg, 255)));
                w.u8((uint8_t)type);
                C.sess->send_reliable_to(0, net::MSG_CELL_HIT, w);
                g_hits_sent++;
            }
            return false;
        }
    }
    return o_DamageCell(victim, attacker, dir, type, x, dt);
}

using KillCell_t = bool(SK_CDECL*)(uint8_t*, uint32_t, void*, bool, int, float);
static KillCell_t o_KillCell;
static bool SK_CDECL h_KillCell(uint8_t* c, uint32_t killer, void* res, bool by_player, int type, float dt)
{
    if (in_cell() && save_avatar(c))
        return false;
    return o_KillCell(c, killer, res, by_player, type, dt);
}

using Break_t = void(SK_CDECL*)(uint8_t*, uint8_t*, float);
static Break_t o_Break;
static void SK_CDECL h_Break(uint8_t* victim, uint8_t* attacker, float f)
{
    if (in_cell() && save_avatar(victim))
        return;
    o_Break(victim, attacker, f);
}

using Destroy_t = void(SK_CDECL*)(uint32_t, bool, float, int);
static Destroy_t o_Destroy;
static void SK_CDECL h_Destroy(uint32_t idx, bool loot, float f, int x)
{
    if (is_guest() && !g_our_destroy && in_cell()) {
        auto it = g_host_of.find(idx);
        if (it != g_host_of.end()) { // the guest ate a mirror (food) or it broke: the host removes it
            net::Writer w;
            w.u32(it->second);
            C.sess->send_reliable_to(0, net::MSG_CELL_EAT, w);
            g_eats_sent++;
            g_mirrors.erase(it->second);
            g_host_of.erase(it);
            loot = false;
        }
    }
    o_Destroy(idx, loot, f, x);
}

using AddFood_t = void(SK_CDECL*)(int, bool);
static AddFood_t o_AddFood;
static void SK_CDECL h_AddFood(int amount, bool plant)
{
    o_AddFood(amount, plant);
    if (is_guest() && !g_food_guard && amount != 0 && in_cell()) {
        net::Writer w;
        w.u32((uint32_t)amount);
        w.u8(plant ? 1 : 0);
        C.sess->send_reliable_to(0, net::MSG_CELL_FOOD, w);
    }
}

using Populate_t = int(SK_CDECL*)(float, int, int, void*, void*, void*, int);
static Populate_t o_Populate;
static int SK_CDECL h_Populate(float prob, int layer, int level, void* query, void* bbox, void* marker, int x)
{
    // guests do not spawn their own creatures/food on the gameplay layer: the host's are mirrored
    if (is_guest() && in_cell() && query == fg_query())
        return 0;
    return o_Populate(prob, layer, level, query, bbox, marker, x);
}

bool init(const SkHost* host, SkMod* mod, Ctx ctx)
{
    H = host;
    M = mod;
    C = std::move(ctx);
    a_DamageCell = H->sym("Simulator::Cell::DamageCell");
    a_KillCell = H->sym("Simulator::Cell::KillCell");
    a_Break = H->sym("Simulator::Cell::BreakOrSwallow");
    a_Destroy = H->sym("Simulator::Cell::DestroyCell");
    a_AddFood = H->sym("Simulator::Cell::AddFoodPoints");
    a_Populate = H->sym("Simulator::Cell::PopulateMarker");
    a_Rollover = H->sym("Simulator::Cell::ShowHealthRollover");
    a_GetDamage = H->sym("Simulator::Cell::GetDamageAmount");
    a_RefCreate = H->sym("Simulator::Cell::cCellDataReference_::Create");
    a_Serializer = H->sym("Simulator::Cell::cCellSerializer::_ptr");
    a_S = H->sym("Simulator::Cell::cCellSerializableData::_ptr");
    g_ok = a_DamageCell && a_KillCell && a_Break && a_Destroy && a_AddFood && a_Populate && a_RefCreate &&
           a_Serializer && a_S;
    if (!g_ok) {
        H->log(M, SK_WARN, "cellsync: symbols missing, cell co-op disabled");
        return false;
    }
    H->hook_create(M, (void*)a_DamageCell, (void*)h_DamageCell, (void**)&o_DamageCell);
    H->hook_create(M, (void*)a_KillCell, (void*)h_KillCell, (void**)&o_KillCell);
    H->hook_create(M, (void*)a_Break, (void*)h_Break, (void**)&o_Break);
    H->hook_create(M, (void*)a_Destroy, (void*)h_Destroy, (void**)&o_Destroy);
    H->hook_create(M, (void*)a_AddFood, (void*)h_AddFood, (void**)&o_AddFood);
    H->hook_create(M, (void*)a_Populate, (void*)h_Populate, (void**)&o_Populate);
    return true;
}

// ---------------- host ----------------
static void host_tick(double now)
{
    uint8_t* s = S();
    if (s && now - g_last_food > 0.5) {
        g_last_food = now;
        int32_t food = F<int32_t>(s, 0x1C), plant = F<int32_t>(s, 0x20);
        if (food != g_food_sent || plant != g_plant_sent) {
            g_food_sent = food;
            g_plant_sent = plant;
            net::Writer w;
            w.u32((uint32_t)food);
            w.u32((uint32_t)plant);
            C.sess->broadcast_reliable(net::MSG_CELL_FOOD, w);
        }
    }
    if (now - g_last_send < 0.1)
        return;
    g_last_send = now;
    uint8_t* av = avatar();
    if (!av)
        return;
    net::Vec3 me = pos_of(av);
    struct E {
        uint8_t* c;
        uint32_t idx;
    };
    std::vector<E> cells;
    for_each_cell([&](uint8_t* c, uint32_t idx) {
        if (modes::cell_is_puppet(c) || !F<void*>(c, kRes) || net::dist(pos_of(c), me) > 120.f)
            return;
        cells.push_back({c, idx});
    });
    g_cycle++;
    size_t per = 14, chunks = std::max<size_t>(1, (cells.size() + per - 1) / per);
    for (size_t k = 0; k < chunks; k++) {
        net::Writer w;
        w.u16(g_cycle);
        w.u8((uint8_t)k);
        w.u8((uint8_t)chunks);
        size_t from = k * per, to = std::min(cells.size(), from + per);
        w.u8((uint8_t)(to - from));
        for (size_t i = from; i < to; i++) {
            uint8_t* c = cells[i].c;
            void* ref = F<void*>(c, kRes);
            uint32_t inst = ref ? *(uint32_t*)ref : 0;
            auto* key = (uint32_t*)(c + kModelKey);
            net::Vec3 p = pos_of(c);
            auto* v = (float*)(c + kVel);
            net::Quat q;
            modes::mat_to_quat((float*)(c + kRot), q);
            w.u32(cells[i].idx);
            w.u32(inst);
            w.u32(key[0]); w.u32(key[1]); w.u32(key[2]);
            w.u8((uint8_t)F<int>(c, kLevel));
            w.f32(F<float>(c, kSize));
            w.f32(p.x); w.f32(p.y); w.f32(p.z);
            w.f32(v[0]); w.f32(v[1]);
            w.f32(q.x); w.f32(q.y); w.f32(q.z); w.f32(q.w);
            w.u8((uint8_t)F<int>(c, kHp));
            w.u8((uint8_t)((F<uint8_t>(c, kDying) ? 1 : 0) | (F<uint8_t>(c, kDead) ? 2 : 0)));
        }
        for (auto& pl : C.sess->players())
            if (pl.id != 0)
                C.sess->send_unreliable_to(pl.id, net::MSG_CELL_STATE, w);
    }
}

// ---------------- guest ----------------
static void* ref_for(uint32_t inst)
{
    return inst ? skg::cdecl_call<void*>(a_RefCreate, inst, (void*)a_Serializer) : nullptr;
}

static void destroy_local(uint32_t idx)
{
    g_our_destroy = true;
    modes::cell_destroy(idx);
    g_our_destroy = false;
}

static void guest_state(net::Reader& r)
{
    uint16_t cycle = r.u16();
    uint8_t chunk = r.u8(), chunks = r.u8(), n = r.u8();
    if (!r.ok())
        return;
    if (cycle != g_rx_cycle) {
        g_rx_cycle = cycle;
        g_cycle_seen.clear();
    }
    double now = H->time_seconds();
    for (int i = 0; i < n && r.ok(); i++) {
        uint32_t hidx = r.u32(), inst = r.u32(), key[3];
        key[0] = r.u32(); key[1] = r.u32(); key[2] = r.u32();
        int level = r.u8();
        float size = r.f32();
        net::Vec3 p, v;
        p.x = r.f32(); p.y = r.f32(); p.z = r.f32();
        v.x = r.f32(); v.y = r.f32(); v.z = 0;
        net::Quat q;
        q.x = r.f32(); q.y = r.f32(); q.z = r.f32(); q.w = r.f32();
        int hp = r.u8();
        uint8_t flags = r.u8();
        if (!r.ok())
            return;
        g_cycle_seen.insert(hidx);
        auto& m = g_mirrors[hidx];
        m.pos = p; m.vel = v; m.rot = q; m.size = size; m.hp = hp; m.seen = now;
        uint8_t* c = m.local ? modes::cell_at(m.local) : nullptr;
        if (!c && !(flags & 2)) {
            uint32_t idx = modes::cell_create(p, ref_for(inst), level, key, &q);
            c = modes::cell_at(idx);
            if (c) {
                m.local = idx;
                g_host_of[idx] = hidx;
                g_created++;
            }
        }
        if (c) {
            F<uint8_t>(c, kAiLock) = 1;    // passive: no behaviour, the host decides
            F<uint8_t>(c, kKeepAlive) = 1; // we own the lifetime, not the tile streaming
            F<float>(c, kSize) = size;
            F<float>(c, kTargetSize) = size;
            F<int>(c, kHp) = hp;
        }
    }
    if (chunk + 1 < chunks)
        return;
    // the host no longer has these: remove the mirrors
    for (auto it = g_mirrors.begin(); it != g_mirrors.end();) {
        if (!g_cycle_seen.count(it->first)) {
            if (it->second.local) {
                g_host_of.erase(it->second.local);
                destroy_local(it->second.local);
                g_destroyed++;
            }
            it = g_mirrors.erase(it);
        } else
            ++it;
    }
    // stray local gameplay cells (spawned before the sync started, loot, ...) do not exist on the host
    std::vector<uint32_t> stray;
    for_each_cell([&](uint8_t* c, uint32_t idx) {
        if (!g_host_of.count(idx) && !modes::cell_is_puppet(c))
            stray.push_back(idx);
    });
    for (uint32_t idx : stray) {
        destroy_local(idx);
        g_purged++;
    }
}

static void guest_tick(double now)
{
    for (auto& [hidx, m] : g_mirrors) {
        uint8_t* c = m.local ? modes::cell_at(m.local) : nullptr;
        if (!c)
            continue;
        double age = std::min(now - m.seen, 0.5);
        net::Vec3 want{m.pos.x + m.vel.x * (float)age, m.pos.y + m.vel.y * (float)age, m.pos.z};
        net::Vec3 cur = pos_of(c);
        if (net::dist(cur, want) > 3.f)
            set_pos(c, want);
        auto* v = (float*)(c + kVel);
        v[0] = m.vel.x + (want.x - cur.x) * 2.f;
        v[1] = m.vel.y + (want.y - cur.y) * 2.f;
        modes::quat_to_mat(m.rot, (float*)(c + kRot));
        F<uint16_t>(c, kFlags) |= 2;
        F<uint8_t>(c, kAiLock) = 1;
    }
}

// ---------------- both ----------------
static void symbiosis_tick(double now)
{
    uint8_t* a = avatar();
    if (!a)
        return;
    if (g_invuln_until > 0 && now > g_invuln_until) {
        F<uint8_t>(a, kInvuln) = 0;
        g_invuln_until = 0;
    }
    if (now - g_last_regen < 3.0)
        return;
    g_last_regen = now;
    int hp = F<int>(a, kHp);
    if (hp > 0 && hp < 6 && !nearest_partner(12.f).empty()) {
        F<int>(a, kHp) = hp + 1;
        if (a_Rollover && F<uint32_t>(a, 0x248))
            skg::cdecl_call<void>(a_Rollover, a, hp);
    }
}

// the host forwards a bite of its NPC on our puppet: apply it to our own cell
static void hurt_avatar(int dmg, int type)
{
    uint8_t* a = avatar();
    if (!a || dmg <= 0 || F<uint8_t>(a, kInvuln) || F<uint8_t>(a, kDying))
        return;
    int old = F<int>(a, kHp), hp = old - dmg;
    if (hp > 0) {
        F<int>(a, kHp) = hp;
        if (a_Rollover && F<uint32_t>(a, 0x248))
            skg::cdecl_call<void>(a_Rollover, a, old);
        return;
    }
    F<int>(a, kHp) = 0;
    h_KillCell(a, avatar_idx(), F<void*>(a, kRes), false, type, 0.f); // the pack may save us
}

void tick(double now)
{
    if (!in_cell() || !C.sess || !C.sess->connected() || !avatar())
        return;
    if (is_host())
        host_tick(now);
    else if (is_guest())
        guest_tick(now);
    symbiosis_tick(now);
}

void on_message(uint8_t type, uint8_t from, net::Reader& r)
{
    if (!in_cell() || !avatar())
        return;
    if (type == net::MSG_CELL_HIT && is_guest()) {
        uint32_t idx = r.u32();
        int dmg = r.u8(), t = r.u8();
        if (r.ok() && idx == 0xFFFFFFFFu)
            hurt_avatar(dmg, t);
        return;
    }
    if (type == net::MSG_CELL_STATE && is_guest()) {
        guest_state(r);
    } else if (type == net::MSG_CELL_HIT && is_host()) {
        uint32_t idx = r.u32();
        int dmg = r.u8(), t = r.u8();
        uint8_t* c = modes::cell_at(idx);
        if (!r.ok() || !c || F<uint8_t>(c, kDying))
            return;
        g_hits_in++;
        int hp = F<int>(c, kHp) - dmg;
        if (hp > 0) {
            F<int>(c, kHp) = hp;
        } else {
            F<int>(c, kHp) = 0;
            o_KillCell(c, avatar_idx(), F<void*>(avatar(), kRes), true, t, 0.f); // counts as a player kill
        }
    } else if (type == net::MSG_CELL_EAT && is_host()) {
        uint32_t idx = r.u32();
        if (r.ok() && modes::cell_at(idx)) {
            g_eats_in++;
            o_Destroy(idx, false, 0.f, 0);
        }
    } else if (type == net::MSG_CELL_FOOD) {
        if (is_host()) { // a guest ate something: add it to the shared progress
            int amount = (int)r.u32();
            bool plant = r.u8() != 0;
            if (r.ok() && amount > -1000 && amount < 1000)
                o_AddFood(amount, plant);
        } else if (is_guest()) {
            int food = (int)r.u32(), plant = (int)r.u32();
            uint8_t* s = S();
            if (!r.ok() || !s)
                return;
            int diff = food - F<int32_t>(s, 0x1C);
            if (diff != 0) {
                g_food_guard = true;
                o_AddFood(diff, false); // through the game: level ups happen as usual
                g_food_guard = false;
            }
            F<int32_t>(s, 0x20) = plant;
        }
    }
}

void on_session_end()
{
    for (auto& [h, m] : g_mirrors)
        if (m.local)
            destroy_local(m.local);
    g_mirrors.clear();
    g_host_of.clear();
    g_food_sent = g_plant_sent = -1;
}

// ---- debug (tests) ----
std::string debug(const std::vector<std::string>& a)
{
    uint8_t* av = avatar();
    if (!av)
        return "not in the cell stage";
    if (!a.empty() && a[0] == "bite") { // our avatar bites the nearest mirror (through DamageCell)
        uint8_t* best = nullptr;
        float bd = 1e9f;
        for (auto& [h, m] : g_mirrors) {
            uint8_t* c = m.local ? modes::cell_at(m.local) : nullptr;
            if (!c)
                continue;
            float d = net::dist(pos_of(c), pos_of(av));
            if (d < bd) {
                bd = d;
                best = c;
            }
        }
        if (!best)
            return "no mirror";
        float dir[3] = {1, 0, 0};
        h_DamageCell(best, av, dir, 4, 0, 0.f);
        return "bit mirror " + std::to_string(F<uint32_t>(best, 0)) + " at " + std::to_string(bd);
    }
    if (a.size() >= 2 && a[0] == "food") { // the game's AddFoodPoints (hooked: shared)
        h_AddFood(atoi(a[1].c_str()), false);
        return "food added";
    }
    if (a.size() >= 2 && a[0] == "hp") {
        F<int>(av, kHp) = atoi(a[1].c_str());
        return "hp set";
    }
    if (a.size() >= 3 && a[0] == "hurt") { // host: send a bite to player <id>
        net::Writer w;
        w.u32(0xFFFFFFFFu);
        w.u8((uint8_t)atoi(a[2].c_str()));
        w.u8(4);
        C.sess->send_reliable_to((uint8_t)atoi(a[1].c_str()), net::MSG_CELL_HIT, w);
        return "bite sent";
    }
    char b[160];
    int local = 0;
    for_each_cell([&](uint8_t*, uint32_t) { local++; });
    snprintf(b, sizeof b, "avatar hp=%d pos=(%.1f %.1f) local_gameplay_cells=%d\n", F<int>(av, kHp), pos_of(av).x,
             pos_of(av).y, local);
    return b + status();
}

std::string status()
{
    char b[256];
    uint8_t* s = S();
    int food = s ? F<int32_t>(s, 0x1C) : -1;
    if (C.sess && C.sess->role() == net::Role::Host)
        snprintf(b, sizeof b, "cell(host): food=%d hits_in=%u eats_in=%u rescues=%u\n", food, g_hits_in, g_eats_in,
                 g_rescues);
    else
        snprintf(b, sizeof b,
                 "cell(guest): food=%d mirrors=%u created=%u destroyed=%u purged=%u hits_sent=%u eats_sent=%u "
                 "rescues=%u\n",
                 food, (unsigned)g_mirrors.size(), g_created, g_destroyed, g_purged, g_hits_sent, g_eats_sent,
                 g_rescues);
    return b;
}

} // namespace cellsync
