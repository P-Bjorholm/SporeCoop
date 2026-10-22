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
    a_RefCreate, a_Serializer, a_S, a_Fnv, a_Effect;

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
// cCellSerializableData: cCellGame+0x5190 (the global at 0x16B4DC0 holds the same pointer)
static uint8_t* S()
{
    uint8_t* g = G();
    uint8_t* s = g ? F<uint8_t*>(g, 0x5190) : nullptr;
    return s ? s : (a_S ? *(uint8_t**)a_S : nullptr);
}

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
// A lethal hit does not kill a player who has a partner in the pool: the cell is knocked out for
// kDownTime seconds. A partner who stays right next to it for kReviveHold seconds revives it
// (half health). Otherwise it respawns next to the partner as a small cell (half size), which
// grows back by eating or at the next level up. Alone in the pool the vanilla death happens
// (it rebuilds the whole level).
constexpr double kDownTime = 10.0, kReviveHold = 1.5;
constexpr float kSmall = 0.5f;
static double g_last_regen, g_invuln_until, g_last_tick;
static uint32_t g_revives, g_respawns;
static bool g_downed;
static double g_down_since, g_hold;
static net::Vec3 g_down_pos;
static float g_body = 1.f;    // body size factor after a respawn (1 = full size)
static float g_body_size = 0; // the target size we wrote, to notice the game resizing the avatar

static std::string nearest_partner(float max_dist, net::Vec3* where = nullptr, float* dist = nullptr)
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
    if (dist)
        *dist = best.empty() ? -1.f : bd;
    return best;
}

// "right next to": the cells (almost) touch
static float revive_dist(uint8_t* a)
{
    float size = F<float>(a, kSize);
    return std::max(3.f, size * 3.f);
}

// A cell with a soft body (the avatar) takes its position from the centroid of its particles
// (0xE7C080: +0x250 count, +0x254 particle indices into *(G+0x4108)+0x4654, 12 bytes each), so
// moving it means moving the particles too; +0x1C4 is the body velocity added to them.
static void stop_at(uint8_t* c, const net::Vec3& p)
{
    modes::cell_move(c, p);
    auto* bv = (float*)(c + 0x1C4);
    bv[0] = bv[1] = bv[2] = 0;
    auto* t = (float*)(c + kTarget);
    t[0] = p.x; t[1] = p.y; t[2] = p.z;
    auto* v = (float*)(c + kVel);
    v[0] = v[1] = v[2] = 0;
}

// scale the avatar body (target size; the game smooths +0x58 towards it), keeping the mass in step
static void set_body(uint8_t* a, float factor)
{
    float cur = F<float>(a, kTargetSize);
    if (cur <= 0)
        return;
    float full = g_body < 1.f ? cur / g_body : cur;
    float want = full * factor, k = want / cur;
    F<float>(a, kTargetSize) = want;
    F<float>(a, 0xB8) *= k * k; // mass ~ size^2 (as SetCellSize 0xE57190 computes it)
    F<float>(a, 0xBC) /= k * k; // inverse mass
    g_body = factor;
    g_body_size = want;
}

// ---- what everybody sees ----
enum : uint8_t { KO_UP = 0, KO_OUT = 1, KO_REVIVED = 2, KO_RESPAWNED = 3 };
struct Ko {
    uint8_t state = KO_UP;
    float left = 0, hold = 0;
    double since = 0, fx_at = 0;
};
static std::unordered_map<int, Ko> g_ko; // remote player -> knockout state
static double g_ko_sent;
constexpr float kKoOpacity = 0.35f;
constexpr uintptr_t kOpacity = 0xA4;

// a one-shot game effect on a cell (names from the game: cell_death_oneshot, cell_hatch_ice, ...)
static void effect(uint8_t* c, const char* name)
{
    if (!c || !a_Fnv || !a_Effect || !F<uint32_t>(c, 0x248))
        return;
    uint32_t id = skg::cdecl_call<uint32_t>(a_Fnv, name, 0x811C9DC5u, 1);
    skg::cdecl_call<void*>(a_Effect, c, id);
}

static void send_ko(uint8_t state, float left, float hold)
{
    if (!C.sess || !C.sess->connected())
        return;
    net::Writer w;
    w.u8((uint8_t)C.sess->my_id());
    w.u8(state);
    w.u8((uint8_t)std::max(0.f, std::min(left * 10.f, 255.f)));
    w.u8((uint8_t)std::max(0.f, std::min(hold * 255.f, 255.f)));
    if (C.sess->role() == net::Role::Host)
        C.sess->broadcast_reliable(net::MSG_CELL_KO, w);
    else
        C.sess->send_reliable_to(0, net::MSG_CELL_KO, w);
    g_ko_sent = H->time_seconds();
}

static uint8_t* puppet_cell(int id)
{
    if (!C.puppets)
        return nullptr;
    for (auto& p : C.puppets())
        if (p.id == id)
            return (uint8_t*)p.cell;
    return nullptr;
}

static void on_ko(int id, uint8_t state, float left, float hold)
{
    Ko& k = g_ko[id];
    uint8_t prev = k.state;
    k.left = left;
    k.hold = hold;
    if (state == prev)
        return;
    k.state = state;
    k.since = H->time_seconds();
    uint8_t* c = puppet_cell(id);
    if (state == KO_OUT)
        effect(c, "cell_death_oneshot");
    else if (state == KO_REVIVED)
        effect(c, "cell_hatch_ice");
    else if (state == KO_RESPAWNED)
        k.fx_at = k.since + 0.4; // after the puppet has moved next to its partner
    if (state != KO_OUT && c)
        F<float>(c, kOpacity) = 1.f;
}

static bool go_down(uint8_t* c)
{
    if (!c || c != avatar() || !C.sess || !C.sess->connected())
        return false;
    if (!g_downed) {
        if (nearest_partner(1e9f).empty())
            return false; // alone in the pool: the vanilla death
        g_downed = true;
        g_down_since = H->time_seconds();
        g_hold = 0;
        g_down_pos = pos_of(c);
        effect(c, "cell_death_oneshot");
        send_ko(KO_OUT, (float)kDownTime, 0);
        if (C.notice)
            C.notice(C.my_name() + " is knocked out: swim right next to them within 10 s");
    }
    F<int>(c, kHp) = 1;
    F<uint8_t>(c, kDead) = 0;
    F<uint8_t>(c, kDying) = 0;
    F<uint8_t>(c, kInvuln) = 1;
    g_invuln_until = 0;
    stop_at(c, g_down_pos);
    return true;
}

static void downed_tick(uint8_t* a, double now, double dt)
{
    if (F<uint8_t>(a, kDying) || F<uint8_t>(a, kDead)) {
        F<uint8_t>(a, kDead) = 0;
        F<uint8_t>(a, kDying) = 0;
    }
    F<int>(a, kHp) = 1;
    F<uint8_t>(a, kInvuln) = 1;
    F<float>(a, kOpacity) = kKoOpacity;
    stop_at(a, g_down_pos);
    float d = 0;
    std::string helper = nearest_partner(revive_dist(a), nullptr, &d);
    g_hold = helper.empty() ? 0 : g_hold + dt;
    if (now - g_ko_sent > 0.25)
        send_ko(KO_OUT, (float)std::max(0.0, kDownTime - (now - g_down_since)), (float)(g_hold / kReviveHold));
    if (!helper.empty() && g_hold >= kReviveHold) { // revived by the partner
        g_downed = false;
        F<int>(a, kHp) = 3;
        F<float>(a, kOpacity) = 1.f;
        g_invuln_until = now + 2.0;
        g_revives++;
        effect(a, "cell_hatch_ice");
        send_ko(KO_REVIVED, 0, 1);
        if (C.notice)
            C.notice(helper + " revived " + C.my_name());
        return;
    }
    if (now - g_down_since < kDownTime)
        return;
    // nobody came: respawn next to the partner, small again
    g_downed = false;
    net::Vec3 at = g_down_pos;
    std::string partner = nearest_partner(1e9f, &at);
    stop_at(a, partner.empty() ? at : net::Vec3{at.x + 2.f, at.y + 1.f, 0.f});
    F<uint8_t>(a, 0x04) = 1; // idle
    F<int>(a, kHp) = 6;
    g_invuln_until = now + 3.0;
    set_body(a, kSmall);
    F<float>(a, kOpacity) = 1.f;
    g_respawns++;
    effect(a, "cell_egg_L0_autohatch");
    send_ko(KO_RESPAWNED, 0, 0);
    if (C.notice)
        C.notice(C.my_name() + " respawned small" + (partner.empty() ? "" : " next to " + partner));
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
    if (in_cell() && go_down(c))
        return false;
    return o_KillCell(c, killer, res, by_player, type, dt);
}

using Break_t = void(SK_CDECL*)(uint8_t*, uint8_t*, float);
static Break_t o_Break;
static void SK_CDECL h_Break(uint8_t* victim, uint8_t* attacker, float f)
{
    if (in_cell() && go_down(victim))
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
    uint8_t* a = in_cell() ? avatar() : nullptr;
    if (a && amount > 0 && g_body < 1.f) // we ate: the small body grows back (50 points = full size)
        set_body(a, std::min(1.f, g_body + amount * 0.01f));
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
    a_Fnv = H->sym("FNV_hash");
    a_Effect = H->sym("Simulator::Cell::cCellGFX::InstanceEffectOnCell");
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
    double dt = g_last_tick > 0 ? std::min(now - g_last_tick, 0.5) : 0;
    g_last_tick = now;
    if (!a)
        return;
    if (g_body < 1.f && std::fabs(F<float>(a, kTargetSize) - g_body_size) > g_body_size * 0.01f)
        g_body = 1.f; // the game resized the avatar (level change): full size again
    for (auto& [id, k] : g_ko) {
        uint8_t* c = k.state == KO_OUT || k.fx_at > 0 ? puppet_cell(id) : nullptr;
        if (k.state == KO_OUT && c)
            F<float>(c, kOpacity) = kKoOpacity;
        if (k.fx_at > 0 && now >= k.fx_at) {
            k.fx_at = 0;
            effect(c, "cell_egg_L0_autohatch");
        }
    }
    if (g_downed) {
        downed_tick(a, now, dt);
        return;
    }
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
    if (type == net::MSG_CELL_KO) {
        uint8_t id = r.u8(), state = r.u8(), left = r.u8(), hold = r.u8();
        if (!r.ok())
            return;
        if (is_host() && from != 0) { // relay to the other guests
            net::Writer w;
            w.u8(id); w.u8(state); w.u8(left); w.u8(hold);
            C.sess->broadcast_reliable(net::MSG_CELL_KO, w, from);
        }
        if (id != C.sess->my_id())
            on_ko(id, state, left / 10.f, hold / 255.f);
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
    g_ko.clear();
    if (g_downed) {
        g_downed = false;
        if (uint8_t* a = in_cell() ? avatar() : nullptr)
            F<uint8_t>(a, kInvuln) = 0;
    }
    for (auto& [h, m] : g_mirrors)
        if (m.local)
            destroy_local(m.local);
    g_mirrors.clear();
    g_host_of.clear();
    g_food_sent = g_plant_sent = -1;
}

// ---- overlay: knockout countdown, revive progress, arrows to knocked out partners ----
static std::string bar(float f)
{
    int n = (int)(std::max(0.f, std::min(f, 1.f)) * 8.f + 0.5f);
    std::string s;
    for (int i = 0; i < 8; i++)
        s += i < n ? "\u25A0" : "\u25A1"; // filled / empty square
    return s;
}

static int text_w(const std::string& s) { return (int)s.size() * 9; }

static void draw_ko(uint8_t* c, const std::string& name, float left, float hold, double now, int sw, int sh)
{
    if (!c)
        return;
    net::Vec3 p = pos_of(c);
    float size = std::max(F<float>(c, kSize), 0.3f);
    float w[3] = {p.x, p.y + size * 2.2f + 1.f, p.z}, sc[2];
    bool blink = std::fmod(now, 0.8) < 0.5;
    char t[64];
    snprintf(t, sizeof t, "KO %d", (int)std::ceil(std::max(0.f, left)));
    uint32_t red = blink ? 0xFFFF4040 : 0xFFB02020;
    bool on = H->world_to_screen(w, sc) && sc[0] >= 0 && sc[1] >= 0 && sc[0] < sw && sc[1] < sh;
    if (on) {
        H->overlay_text((int)sc[0] - text_w(t) / 2, (int)sc[1] - 34, red, t);
        if (hold > 0.01f) {
            std::string b = bar(hold);
            H->overlay_text((int)sc[0] - 40, (int)sc[1] + 18, 0xFF60FF60, b.c_str());
        }
        return;
    }
    if (name.empty() || sw <= 0)
        return;
    // off screen: an arrow on the screen edge towards it
    float cx = sw / 2.f, cy = sh / 2.f;
    if (!H->world_to_screen(w, sc)) // behind: never happens in the flat cell world, keep a fallback
        sc[0] = cx, sc[1] = sh + 100.f;
    float dx = sc[0] - cx, dy = sc[1] - cy;
    float k = std::min(std::fabs(dx) > 1 ? (cx - 70) / std::fabs(dx) : 1e9f, std::fabs(dy) > 1 ? (cy - 50) / std::fabs(dy) : 1e9f);
    int x = (int)(cx + dx * k), y = (int)(cy + dy * k);
    const char* arrow = std::fabs(dx) > std::fabs(dy) ? (dx > 0 ? "\u25BA" : "\u25C4") : (dy > 0 ? "\u25BC" : "\u25B2");
    std::string s = std::string(arrow) + " " + name + " " + t;
    H->overlay_text(x - text_w(s) / 2, y - 10, red, s.c_str());
}

void draw_overlay()
{
    if (!in_cell() || !C.sess || !C.sess->connected())
        return;
    int sw = 0, sh = 0;
    H->screen_size(&sw, &sh);
    double now = H->time_seconds();
    if (g_downed)
        draw_ko(avatar(), "", (float)std::max(0.0, kDownTime - (now - g_down_since)), (float)(g_hold / kReviveHold), now,
                sw, sh);
    if (!C.puppets)
        return;
    for (auto& p : C.puppets()) {
        auto it = g_ko.find(p.id);
        if (it == g_ko.end() || it->second.state != KO_OUT)
            continue;
        draw_ko((uint8_t*)p.cell, p.name, it->second.left, it->second.hold, now, sw, sh);
    }
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
    if (a.size() >= 3 && a[0] == "goto") { // swim to <dx dy> from where we are (like a mouse click)
        net::Vec3 p = pos_of(av);
        auto* t = (float*)(av + kTarget);
        t[0] = p.x + (float)atof(a[1].c_str());
        t[1] = p.y + (float)atof(a[2].c_str());
        t[2] = p.z;
        F<uint8_t>(av, 0x04) = 0;
        return "swimming";
    }
    if (a.size() >= 3 && a[0] == "tp") { // move our cell to <dx dy> from the nearest partner
        net::Vec3 at;
        if (nearest_partner(1e9f, &at).empty())
            return "no partner";
        stop_at(av, {at.x + (float)atof(a[1].c_str()), at.y + (float)atof(a[2].c_str()), 0.f});
        return "moved";
    }
    if (a.size() >= 3 && a[0] == "hurt") { // host: send a bite to player <id>
        net::Writer w;
        w.u32(0xFFFFFFFFu);
        w.u8((uint8_t)atoi(a[2].c_str()));
        w.u8(4);
        C.sess->send_reliable_to((uint8_t)atoi(a[1].c_str()), net::MSG_CELL_HIT, w);
        return "bite sent";
    }
    char b[320];
    int local = 0;
    for_each_cell([&](uint8_t*, uint32_t) { local++; });
    float pd = 0;
    nearest_partner(1e9f, nullptr, &pd);
    std::string ko;
    for (auto& [id, k] : g_ko) {
        uint8_t* pc = puppet_cell(id);
        char kb[96];
        snprintf(kb, sizeof kb, "ko[%d]=%d left=%.1f hold=%.2f op=%.2f\n", id, k.state, k.left, k.hold,
                 pc ? F<float>(pc, kOpacity) : -1.f);
        ko += kb;
    }
    snprintf(b, sizeof b,
             "avatar hp=%d pos=(%.1f %.1f) size=%.2f target=%.2f body=%.2f downed=%d partner_dist=%.1f "
             "revive_dist=%.1f local_gameplay_cells=%d\n",
             F<int>(av, kHp), pos_of(av).x, pos_of(av).y, F<float>(av, kSize), F<float>(av, kTargetSize), g_body,
             g_downed ? 1 : 0, pd, revive_dist(av), local);
    char ob[48];
    snprintf(ob, sizeof ob, "avatar_opacity=%.2f\n", F<float>(av, kOpacity));
    return b + std::string(ob) + ko + status();
}

std::string status()
{
    char b[256];
    uint8_t* s = S();
    int food = s ? F<int32_t>(s, 0x1C) : -1;
    if (C.sess && C.sess->role() == net::Role::Host)
        snprintf(b, sizeof b, "cell(host): food=%d hits_in=%u eats_in=%u revives=%u respawns=%u\n", food, g_hits_in,
                 g_eats_in, g_revives, g_respawns);
    else
        snprintf(b, sizeof b,
                 "cell(guest): food=%d mirrors=%u created=%u destroyed=%u purged=%u hits_sent=%u eats_sent=%u "
                 "revives=%u respawns=%u\n",
                 food, (unsigned)g_mirrors.size(), g_created, g_destroyed, g_purged, g_hits_sent, g_eats_sent,
                 g_revives, g_respawns);
    return b;
}

std::vector<syncdbg::Entry> sync_entries()
{
    std::vector<syncdbg::Entry> v;
    if (!in_cell())
        return v;
    auto entry = [&](uint8_t* c, uint32_t key) {
        net::Quat q;
        modes::mat_to_quat((float*)(c + kRot), q);
        v.push_back({"c" + std::to_string(key), pos_of(c), q});
    };
    if (is_host()) {
        for_each_cell([&](uint8_t* c, uint32_t idx) {
            if (!modes::cell_is_puppet(c))
                entry(c, idx);
        });
    } else {
        for (auto& [h, m] : g_mirrors)
            if (uint8_t* c = m.local ? modes::cell_at(m.local) : nullptr)
                entry(c, h);
    }
    return v;
}

void join_partner()
{
    uint8_t* a = in_cell() ? avatar() : nullptr;
    net::Vec3 at;
    if (a && !nearest_partner(1e9f, &at).empty())
        stop_at(a, {at.x + 2.5f, at.y + 1.f, 0.f});
}

} // namespace cellsync
