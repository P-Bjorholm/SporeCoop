#include "tribe.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <unordered_map>
#include <unordered_set>

#include "driver.h"
#include "game.h"
#include "modes.h"
#include "npc.h"
#include "net/interp.h"
#include "sporekit_game.hpp"

using skg::thiscall;
using skg::vcall;

namespace tribe {

static const SkHost* H;
static SkMod* M;
static Ctx C;
static bool g_ok;

// ---- layout (docs/re/tribe.md) ----
constexpr uint32_t kTribeNoun = 0x18C6D19;
constexpr uintptr_t kMembersOff = 0x340;   // cTribe: vector<cCreatureCitizen*>
constexpr uintptr_t kTribePosOff = 0x124;  // cTribe spatial position
constexpr uintptr_t kOwnerTribeOff = 0x1014; // cCreatureCitizen::mpOwnerTribe
constexpr uintptr_t kAgeOff = 0xB34;       // 0 baby, 1 adult
constexpr uintptr_t kSelectedOff = 0x12C;  // spatial+0x6C IsSelected
constexpr uintptr_t kCitizenVt = 0x0146B818;

static uintptr_t a_GetPlayerTribe, a_DoAction, a_SetValueVector3, a_BTTick, a_DoActionForObjects, a_WalkSelected,
    a_SetSelected, a_CreateCitizen, a_AddMember;

template <typename T>
static T& F(void* p, uintptr_t off) { return *(T*)((uint8_t*)p + off); }

static bool is_host() { return C.sess && C.sess->role() == net::Role::Host && C.sess->connected(); }
static bool is_guest() { return C.sess && C.sess->role() == net::Role::Client && C.sess->connected(); }
bool active() { return g_ok && C.sess && C.sess->connected() && game::mode_id() == modes::kTribe; }

static void* player_tribe()
{
    void* nm = game::noun_manager();
    return nm ? thiscall<void*>(a_GetPlayerTribe, nm) : nullptr;
}

static bool is_citizen(void* c) { return c && !IsBadReadPtr(c, 8) && *(uintptr_t*)c == H->rebase(kCitizenVt); }

static std::vector<void*> members(void* t)
{
    std::vector<void*> v;
    void** b = F<void**>(t, kMembersOff);
    void** e = F<void**>(t, kMembersOff + 4);
    for (void** it = b; it && it < e && v.size() < 256; ++it)
        if (*it && game::object_alive(*it))
            v.push_back(*it);
    return v;
}

static std::vector<void*> all_tribes()
{
    std::vector<void*> v;
    game::for_each_object(
        [](void* o, uint32_t noun, void* u) {
            if (noun == kTribeNoun && game::object_alive(o))
                ((std::vector<void*>*)u)->push_back(o);
            return true;
        },
        &v);
    return v;
}

static net::Vec3 tribe_pos(void* t)
{
    auto& p = F<float[3]>(t, kTribePosOff);
    return {p[0], p[1], p[2]};
}

// ---------------- host ----------------
struct HMember {
    uint16_t id;
    uint8_t owner;
};
static std::unordered_map<void*, HMember> g_hm;
static std::unordered_map<uint16_t, void*> g_hm_by_id;
static uint16_t g_next_id = 1;
static uint32_t g_player_members_seen = 0;
static std::vector<uint8_t> g_split_for; // player ids the player tribe was split for
static double g_last_send;
static uint16_t g_cycle;
static uint32_t g_cmds_done, g_cmds_rejected;

// ---------------- guest ----------------
struct GMember {
    void* c = nullptr;
    uint8_t tribe = 0, owner = 0xFE, age = 1;
    net::Vec3 pos, vel;
    net::Quat rot;
    float hp = 0;
    double seen = 0;
    drive::State ds;
};
static std::unordered_map<uint16_t, GMember> g_gm;
static std::unordered_map<void*, uint16_t> g_gid_of;
static std::unordered_set<uint16_t> g_cycle_ids;
static uint16_t g_rx_cycle = 0xFFFF;
static uint32_t g_spawned, g_removed, g_cmds_sent;
static std::vector<void*> g_local_tribe_of; // host tribe index -> local tribe

uint8_t owner_of(void* c)
{
    if (C.sess && C.sess->role() == net::Role::Host) {
        auto it = g_hm.find(c);
        return it == g_hm.end() ? 0xFF : it->second.owner;
    }
    auto it = g_gid_of.find(c);
    if (it == g_gid_of.end())
        return 0xFF;
    return g_gm[it->second].owner;
}

// ---------------- hooks ----------------
using BTTick_t = int(SK_CDECL*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
static BTTick_t o_BTTick;
// behaviour tree tick (shared by every creature kind): skipped for citizen mirrors on guests
static int SK_CDECL h_BTTick(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t self, uint32_t a5, uint32_t a6,
                             uint32_t a7, uint32_t a8)
{
    if (is_guest() && is_citizen((void*)self) && game::mode_id() == modes::kTribe)
        return 0;
    return o_BTTick(a0, a1, a2, a3, self, a5, a6, a7, a8);
}

// a player may select only their own members of the player tribe
using SetSelected_t = int(SK_THISCALL*)(void*, int);
static SetSelected_t o_SetSelected;
static int SK_THISCALL h_SetSelected(void* spatial, int sel)
{
    if ((sel & 0xFF) && active()) {
        void* c = (uint8_t*)spatial - 0xC0;
        uint8_t owner = owner_of(c);
        if (owner != 0xFF && owner != C.sess->my_id())
            sel = 0;
    }
    return o_SetSelected(spatial, sel);
}

struct Target {
    uint8_t kind = 0; // 0 none, 1 citizen id, 2 object by noun + position
    uint16_t id = 0;
    uint32_t noun = 0;
    net::Vec3 pos;
};

static Target encode_target(void* t)
{
    Target r;
    if (!t)
        return r;
    auto it = g_gid_of.find(t);
    if (it != g_gid_of.end()) {
        r.kind = 1;
        r.id = it->second;
        return r;
    }
    r.kind = 2;
    r.noun = game::noun_id(t);
    game::object_position(t, r.pos);
    return r;
}

static void write_target(net::Writer& w, const Target& t)
{
    w.u8(t.kind);
    w.u16(t.id);
    w.u32(t.noun);
    w.f32(t.pos.x); w.f32(t.pos.y); w.f32(t.pos.z);
}

static Target read_target(net::Reader& r)
{
    Target t;
    t.kind = r.u8();
    t.id = r.u16();
    t.noun = r.u32();
    t.pos.x = r.f32(); t.pos.y = r.f32(); t.pos.z = r.f32();
    return t;
}

enum : uint8_t { CMD_ACTION = 0, CMD_WALK = 1 };

static void send_cmd(uint8_t kind, const std::vector<uint16_t>& ids, int action, const Target& tgt, const net::Vec3& pos)
{
    if (ids.empty())
        return;
    net::Writer w;
    w.u8(kind);
    w.u8((uint8_t)std::min<size_t>(ids.size(), 64));
    for (size_t i = 0; i < ids.size() && i < 64; i++)
        w.u16(ids[i]);
    w.u32((uint32_t)action);
    write_target(w, tgt);
    w.f32(pos.x); w.f32(pos.y); w.f32(pos.z);
    C.sess->send_reliable_to(0, net::MSG_TRIBE_CMD, w);
    g_cmds_sent++;
}

// cTribeInputStrategy::DoActionForObjects(vector<cSpatialObject*>*, action, target, prop) — every
// order on an object goes through it; on guests the order goes to the host instead
using DoActionForObjects_t = int(SK_THISCALL*)(void*, void*, int, void*, void*);
static DoActionForObjects_t o_DoActionForObjects;
static int SK_THISCALL h_DoActionForObjects(void* self, void* vec, int action, void* target, void* prop)
{
    if (!is_guest() || game::mode_id() != modes::kTribe)
        return o_DoActionForObjects(self, vec, action, target, prop);
    std::vector<uint16_t> ids;
    void** b = vec ? F<void**>(vec, 0) : nullptr;
    void** e = vec ? F<void**>(vec, 4) : nullptr;
    for (void** it = b; it && it < e; ++it) {
        auto f = g_gid_of.find((uint8_t*)*it - 0xC0);
        if (f != g_gid_of.end())
            ids.push_back(f->second);
    }
    send_cmd(CMD_ACTION, ids, action, encode_target(target), {});
    return ids.empty() ? 1 : 0;
}

// cTribeInputStrategy::WalkSelectedTo(const Vector3*)
using WalkSelected_t = int(SK_THISCALL*)(void*, const float*);
static WalkSelected_t o_WalkSelected;
static int SK_THISCALL h_WalkSelected(void* self, const float* pos)
{
    if (!is_guest() || game::mode_id() != modes::kTribe || !pos)
        return o_WalkSelected(self, pos);
    std::vector<uint16_t> ids;
    for (auto& [id, m] : g_gm)
        if (m.c && F<uint8_t>(m.c, kSelectedOff) && m.owner == C.sess->my_id())
            ids.push_back(id);
    send_cmd(CMD_WALK, ids, 4, {}, {pos[0], pos[1], pos[2]});
    return 0;
}

bool init(const SkHost* host, SkMod* mod, Ctx ctx)
{
    H = host;
    M = mod;
    C = std::move(ctx);
    a_GetPlayerTribe = H->sym("Simulator::cGameNounManager::GetPlayerTribe");
    a_DoAction = H->sym("Simulator::cCreatureCitizen::DoAction");
    a_SetValueVector3 = H->sym("App::Property::SetValueVector3");
    a_BTTick = H->sym("Simulator::BehaviorTreeTick");
    a_DoActionForObjects = H->sym("Simulator::cTribeInputStrategy::DoActionForObjects");
    a_WalkSelected = H->sym("Simulator::cTribeInputStrategy::WalkSelectedTo");
    a_SetSelected = H->sym("Simulator::cCreatureCitizen::SetSelected");
    a_CreateCitizen = H->sym("Simulator::cTribe::CreateCitizen");
    a_AddMember = H->sym("Simulator::cTribe::AddMember");
    g_ok = a_GetPlayerTribe && a_DoAction && a_SetValueVector3 && a_BTTick && a_DoActionForObjects && a_WalkSelected &&
           a_SetSelected && a_CreateCitizen && a_AddMember;
    if (!g_ok) {
        H->log(M, SK_WARN, "tribe: symbols missing, tribe co-op disabled");
        return false;
    }
    H->hook_create(M, (void*)a_BTTick, (void*)h_BTTick, (void**)&o_BTTick);
    H->hook_create(M, (void*)a_SetSelected, (void*)h_SetSelected, (void**)&o_SetSelected);
    H->hook_create(M, (void*)a_DoActionForObjects, (void*)h_DoActionForObjects, (void**)&o_DoActionForObjects);
    H->hook_create(M, (void*)a_WalkSelected, (void*)h_WalkSelected, (void**)&o_WalkSelected);
    return true;
}

// ---------------- host: snapshots + orders ----------------
static void host_tick(double now)
{
    if (now - g_last_send < 0.12)
        return;
    g_last_send = now;
    void* pt = player_tribe();
    auto tribes = all_tribes();
    std::vector<uint8_t> players;
    for (auto& p : C.sess->players())
        players.push_back(p.id);
    std::sort(players.begin(), players.end());
    if (players != g_split_for && pt) {
        // the players changed: split the player tribe again, in member order
        g_split_for = players;
        g_player_members_seen = 0;
        for (void* c : members(pt)) {
            auto it = g_hm.find(c);
            if (it != g_hm.end())
                it->second.owner = players[g_player_members_seen++ % players.size()];
        }
    }
    std::unordered_set<void*> alive;
    struct Entry {
        uint16_t id;
        uint8_t tribe, owner, age;
        void* c;
    };
    std::vector<Entry> entries;
    for (size_t ti = 0; ti < tribes.size() && ti < 32; ti++) {
        bool player = tribes[ti] == pt;
        for (void* c : members(tribes[ti])) {
            alive.insert(c);
            auto it = g_hm.find(c);
            if (it == g_hm.end()) {
                // the player tribe is split between the players in turns; others belong to nobody
                uint8_t owner = player && !players.empty() ? players[g_player_members_seen++ % players.size()] : 0xFE;
                it = g_hm.emplace(c, HMember{g_next_id++, owner}).first;
                if (g_next_id == 0)
                    g_next_id = 1;
                g_hm_by_id[it->second.id] = c;
            }
            entries.push_back({it->second.id, (uint8_t)ti, it->second.owner, F<uint8_t>(c, kAgeOff), c});
        }
    }
    for (auto it = g_hm.begin(); it != g_hm.end();) {
        if (!alive.count(it->first)) {
            g_hm_by_id.erase(it->second.id);
            it = g_hm.erase(it);
        } else
            ++it;
    }
    // chunks of <= 24 members; every chunk carries the tribe table
    g_cycle++;
    size_t per = 24, chunks = std::max<size_t>(1, (entries.size() + per - 1) / per);
    for (size_t k = 0; k < chunks; k++) {
        net::Writer w;
        w.u16(g_cycle);
        w.u8((uint8_t)k);
        w.u8((uint8_t)chunks);
        w.u8((uint8_t)std::min<size_t>(tribes.size(), 32));
        for (size_t ti = 0; ti < tribes.size() && ti < 32; ti++) {
            net::Vec3 p = tribe_pos(tribes[ti]);
            w.f32(p.x); w.f32(p.y); w.f32(p.z);
            w.u8(tribes[ti] == pt ? 1 : 0);
        }
        size_t from = k * per, to = std::min(entries.size(), from + per);
        w.u8((uint8_t)(to - from));
        for (size_t i = from; i < to; i++) {
            const Entry& e = entries[i];
            net::Vec3 p = game::position(e.c), v = game::velocity(e.c);
            net::Quat q = game::orientation(e.c);
            w.u16(e.id); w.u8(e.tribe); w.u8(e.owner); w.u8(e.age);
            w.f32(p.x); w.f32(p.y); w.f32(p.z);
            w.f32(v.x); w.f32(v.y); w.f32(v.z);
            w.f32(q.x); w.f32(q.y); w.f32(q.z); w.f32(q.w);
            w.f32(game::health(e.c));
        }
        for (auto& p : C.sess->players())
            if (p.id != 0)
                C.sess->send_unreliable_to(p.id, net::MSG_TRIBE_STATE, w);
    }
}

static void* resolve_target(const Target& t)
{
    if (t.kind == 1) {
        auto it = g_hm_by_id.find(t.id);
        return it == g_hm_by_id.end() ? nullptr : it->second;
    }
    if (t.kind == 2 && t.noun)
        return game::find_object(t.noun, t.pos, 8.f);
    return nullptr;
}

static void host_command(uint8_t from, net::Reader& r)
{
    uint8_t kind = r.u8(), n = r.u8();
    std::vector<uint16_t> ids;
    for (int i = 0; i < n && r.ok(); i++)
        ids.push_back(r.u16());
    int action = (int)r.u32();
    Target tgt = read_target(r);
    net::Vec3 pos;
    pos.x = r.f32(); pos.y = r.f32(); pos.z = r.f32();
    if (!r.ok())
        return;
    void* pt = player_tribe();
    void* target = kind == CMD_ACTION ? resolve_target(tgt) : nullptr;
    if (kind == CMD_ACTION && tgt.kind != 0 && !target) {
        g_cmds_rejected++;
        return; // the object is gone on the host
    }
    for (uint16_t id : ids) {
        auto it = g_hm_by_id.find(id);
        if (it == g_hm_by_id.end()) {
            g_cmds_rejected++;
            continue;
        }
        void* c = it->second;
        auto hm = g_hm.find(c);
        if (hm == g_hm.end() || hm->second.owner != from || F<void*>(c, kOwnerTribeOff) != pt ||
            !game::object_alive(c)) {
            g_cmds_rejected++;
            continue;
        }
        if (kind == CMD_WALK) {
            void* rec = thiscall<void*>(a_DoAction, c, 4, (void*)nullptr, (void*)nullptr);
            float p[3] = {pos.x, pos.y, pos.z};
            if (rec)
                thiscall<void>(a_SetValueVector3, (uint8_t*)rec + 0x39, (const float*)p);
        } else
            thiscall<void*>(a_DoAction, c, action, target, (void*)nullptr);
        g_cmds_done++;
    }
}

// ---------------- guest: mirrors ----------------
static void* spawn_member(void* t, uint8_t age)
{
    // SpawnMember (vt 0x98) returns null at the population cap: then create + add directly
    void* c = vcall<void*>(t, 0x98 / 4, (int)age);
    if (!c) {
        c = thiscall<void*>(a_CreateCitizen, t, (int)age);
        if (c)
            thiscall<void>(a_AddMember, t, c, 1);
    }
    return c;
}

static void remove_member(void* c)
{
    void* t = F<void*>(c, kOwnerTribeOff);
    if (t)
        vcall<void>(t, 0x9C / 4, c, 1); // RemoveMember(c, destroy)
}

static void guest_state(net::Reader& r)
{
    uint16_t cycle = r.u16();
    uint8_t chunk = r.u8(), chunks = r.u8(), nt = r.u8();
    std::vector<std::pair<net::Vec3, bool>> htribes;
    for (int i = 0; i < nt && r.ok(); i++) {
        net::Vec3 p;
        p.x = r.f32(); p.y = r.f32(); p.z = r.f32();
        htribes.push_back({p, r.u8() != 0});
    }
    if (!r.ok())
        return;
    // host tribe index -> local tribe (the player tribe directly, others by position)
    auto locals = all_tribes();
    void* pt = player_tribe();
    g_local_tribe_of.assign(htribes.size(), nullptr);
    for (size_t i = 0; i < htribes.size(); i++) {
        if (htribes[i].second) {
            g_local_tribe_of[i] = pt;
            continue;
        }
        float best = 40.f;
        for (void* t : locals) {
            float d = net::dist(tribe_pos(t), htribes[i].first);
            if (t != pt && d < best) {
                best = d;
                g_local_tribe_of[i] = t;
            }
        }
    }
    if (cycle != g_rx_cycle) {
        g_rx_cycle = cycle;
        g_cycle_ids.clear();
    }
    double now = H->time_seconds();
    uint8_t n = r.u8();
    for (int i = 0; i < n && r.ok(); i++) {
        uint16_t id = r.u16();
        GMember m;
        m.tribe = r.u8(); m.owner = r.u8(); m.age = r.u8();
        m.pos.x = r.f32(); m.pos.y = r.f32(); m.pos.z = r.f32();
        m.vel.x = r.f32(); m.vel.y = r.f32(); m.vel.z = r.f32();
        m.rot.x = r.f32(); m.rot.y = r.f32(); m.rot.z = r.f32(); m.rot.w = r.f32();
        m.hp = r.f32();
        if (!r.ok())
            return;
        g_cycle_ids.insert(id);
        auto& g = g_gm[id];
        void* c = g.c;
        g.tribe = m.tribe; g.owner = m.owner; g.age = m.age;
        g.pos = m.pos; g.vel = m.vel; g.rot = m.rot; g.hp = m.hp; g.seen = now;
        if (c && !game::object_alive(c)) {
            g_gid_of.erase(c);
            g.c = c = nullptr;
        }
        if (c)
            continue;
        void* t = m.tribe < g_local_tribe_of.size() ? g_local_tribe_of[m.tribe] : nullptr;
        if (!t)
            continue;
        // bind to the nearest unbound local member of that tribe, else spawn one
        float best = 6.f;
        for (void* lc : members(t)) {
            if (g_gid_of.count(lc))
                continue;
            float d = net::dist(game::position(lc), m.pos);
            if (d < best) {
                best = d;
                c = lc;
            }
        }
        if (!c) {
            c = spawn_member(t, m.age);
            if (!c)
                continue;
            game::teleport(c, m.pos, m.rot);
            g_spawned++;
        }
        g.c = c;
        g_gid_of[c] = id;
    }
    if (chunk + 1 < chunks)
        return;
    // end of a full snapshot: members the host no longer has go away, and so do local members
    // the host does not know (the guest's tribes are only mirrors)
    for (auto it = g_gm.begin(); it != g_gm.end();) {
        if (!g_cycle_ids.count(it->first)) {
            if (it->second.c && game::object_alive(it->second.c)) {
                remove_member(it->second.c);
                g_removed++;
            }
            g_gid_of.erase(it->second.c);
            it = g_gm.erase(it);
        } else
            ++it;
    }
    for (void* t : g_local_tribe_of)
        if (t)
            for (void* lc : members(t))
                if (!g_gid_of.count(lc)) {
                    remove_member(lc);
                    g_removed++;
                }
}

static void guest_tick(double now)
{
    for (auto& [id, g] : g_gm) {
        if (!g.c || !game::object_alive(g.c) || now - g.seen > 3.0)
            continue;
        drive::step(g.c, g.pos, g.rot, g.vel, g.ds, now, now - g.seen, true);
        float hp = game::health(g.c);
        if (std::fabs(hp - g.hp) > 0.01f && g.hp > 0)
            game::set_health(g.c, g.hp);
    }
}

void tick(double now)
{
    if (!active())
        return;
    if (is_host())
        host_tick(now);
    else if (is_guest())
        guest_tick(now);
}

static uint32_t g_rx, g_rx_skipped, g_resets;

void on_message(uint8_t type, uint8_t from, net::Reader& r)
{
    if (!g_ok)
        return;
    if (type == net::MSG_TRIBE_STATE) {
        g_rx++;
        if (!is_guest() || game::mode_id() != modes::kTribe)
            g_rx_skipped++;
    }
    if (type == net::MSG_TRIBE_STATE && is_guest() && game::mode_id() == modes::kTribe)
        guest_state(r);
    else if (type == net::MSG_TRIBE_CMD && is_host() && game::mode_id() == modes::kTribe)
        host_command(from, r);
}

void on_session_end()
{
    g_split_for.clear();
    g_hm.clear();
    g_hm_by_id.clear();
    g_gm.clear();
    g_gid_of.clear();
    g_player_members_seen = 0;
}

void on_world_change()
{
    g_resets++;
    on_session_end();
}

bool center(net::Vec3& pos)
{
    void* t = g_ok ? player_tribe() : nullptr;
    if (!t)
        return false;
    pos = tribe_pos(t);
    return true;
}

void for_each_owned(void (*fn)(void* citizen, uint8_t owner, void* user), void* user)
{
    void* t = active() ? player_tribe() : nullptr;
    if (!t)
        return;
    for (void* c : members(t)) {
        uint8_t o = owner_of(c);
        if (o != 0xFF && o != 0xFE)
            fn(c, o, user);
    }
}

std::string status()
{
    char b[256];
    if (C.sess && C.sess->role() == net::Role::Host)
        snprintf(b, sizeof b, "tribe(host): members=%u cmds_done=%u rejected=%u\n", (unsigned)g_hm.size(), g_cmds_done,
                 g_cmds_rejected);
    else
        snprintf(b, sizeof b,
                 "tribe(guest): mirrors=%u spawned=%u removed=%u cmds_sent=%u rx=%u skipped=%u resets=%u "
                 "local_tribes=%u player_tribe=%p\n",
                 (unsigned)g_gm.size(), g_spawned, g_removed, g_cmds_sent, g_rx, g_rx_skipped, g_resets,
                 (unsigned)all_tribes().size(), player_tribe());
    return b;
}

// ---- debug (tests): orders through the same hooked functions the UI uses ----
static void* local_of(uint16_t id)
{
    if (C.sess && C.sess->role() == net::Role::Host) {
        auto it = g_hm_by_id.find(id);
        return it == g_hm_by_id.end() ? nullptr : it->second;
    }
    auto it = g_gm.find(id);
    return it == g_gm.end() ? nullptr : it->second.c;
}

std::string debug(const std::vector<std::string>& a)
{
    if (a.size() >= 3 && a[0] == "select") { // select <id> <0|1>
        void* c = local_of((uint16_t)atoi(a[1].c_str()));
        if (!c)
            return "no such member";
        vcall<int>((uint8_t*)c + 0xC0, 0x54 / 4, atoi(a[2].c_str()));
        return std::string("selected=") + std::to_string(F<uint8_t>(c, kSelectedOff));
    }
    if (a.size() >= 4 && a[0] == "walk") { // walk <x> <y> <z> : the selected members walk there
        float p[3] = {(float)atof(a[1].c_str()), (float)atof(a[2].c_str()), (float)atof(a[3].c_str())};
        thiscall<int>(a_WalkSelected, (void*)nullptr, (const float*)p);
        return "walk ordered";
    }
    if (a.size() >= 3 && (a[0] == "act" || a[0] == "act_npc")) { // act <action> <member id | npc id>
        void* t = a[0] == "act" ? local_of((uint16_t)atoi(a[2].c_str())) : npc::proxy((uint16_t)atoi(a[2].c_str()));
        std::vector<void*> sel;
        for (auto& [id, g] : g_gm)
            if (g.c && F<uint8_t>(g.c, kSelectedOff))
                sel.push_back((uint8_t*)g.c + 0xC0);
        for (auto& [c, hm] : g_hm)
            if (F<uint8_t>(c, kSelectedOff))
                sel.push_back((uint8_t*)c + 0xC0);
        void* vec[3] = {sel.data(), sel.data() + sel.size(), sel.data() + sel.size()};
        int r = thiscall<int>(a_DoActionForObjects, (void*)nullptr, (void*)vec, atoi(a[1].c_str()), t, (void*)nullptr);
        return "action ordered (" + std::to_string(sel.size()) + " selected, ret " + std::to_string(r) + ")";
    }
    return "usage: coop.tribe select <id> <0|1> | walk <x> <y> <z> | act <action> <target id>";
}

std::string list()
{
    std::string out;
    char b[200];
    void* pt = player_tribe();
    auto tribes = all_tribes();
    for (size_t ti = 0; ti < tribes.size(); ti++) {
        net::Vec3 p = tribe_pos(tribes[ti]);
        auto ms = members(tribes[ti]);
        snprintf(b, sizeof b, "tribe %u%s at (%.0f %.0f %.0f): %u members\n", (unsigned)ti,
                 tribes[ti] == pt ? " (player)" : "", p.x, p.y, p.z, (unsigned)ms.size());
        out += b;
        for (void* c : ms) {
            net::Vec3 cp = game::position(c);
            uint8_t ow = owner_of(c);
            uint16_t id = 0;
            if (C.sess && C.sess->role() == net::Role::Host) {
                auto it = g_hm.find(c);
                id = it == g_hm.end() ? 0 : it->second.id;
            } else {
                auto it = g_gid_of.find(c);
                id = it == g_gid_of.end() ? 0 : it->second;
            }
            net::Vec3 v = game::velocity(c), sv{};
            if (C.sess && C.sess->role() != net::Role::Host) { // the host's velocity in the snapshot
                auto it = g_gm.find(id);
                if (it != g_gm.end())
                    sv = it->second.vel;
            }
            snprintf(b, sizeof b,
                     "  #%u %p owner=%d age=%u sel=%u hp=%.1f pos=(%.1f %.1f %.1f) speed=%.1f host_speed=%.1f\n", id,
                     c, ow == 0xFF ? -1 : (int)ow, F<uint8_t>(c, kAgeOff), F<uint8_t>(c, kSelectedOff),
                     game::health(c), cp.x, cp.y, cp.z, drive::speed(v), drive::speed(sv));
            out += b;
        }
    }
    return out;
}

std::vector<syncdbg::Entry> sync_entries()
{
    std::vector<syncdbg::Entry> v;
    if (C.sess && C.sess->role() == net::Role::Host) {
        for (auto& [c, h] : g_hm)
            if (game::object_alive(c))
                v.push_back({"t" + std::to_string(h.id), game::position(c), game::orientation(c)});
    } else {
        for (auto& [id, g] : g_gm)
            if (g.c && game::object_alive(g.c))
                v.push_back({"t" + std::to_string(id), game::position(g.c), game::orientation(g.c)});
    }
    return v;
}

} // namespace tribe
