#include "spacecoop.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>

#include "game.h"
#include "modes.h"
#include "sporekit_game.hpp"

using skg::cdecl_call;
using skg::thiscall;
using skg::vcall;

namespace spacecoop {

static const SkHost* H;
static SkMod* M;
static Ctx C;
static bool g_ok;

template <typename T>
static T& F(void* p, uintptr_t off) { return *(T*)((uint8_t*)p + off); }

// cGameDataUFO (docs/re/space.md): combatant +0x508, max hp +0x534, state +0x53C, hp +0x540,
// invincible +0x588, death cinematic handle +0x7F0
constexpr uintptr_t kCombat = 0x508, kMaxHp = 0x534, kState = 0x53C, kHp = 0x540, kInvincible = 0x588,
                    kCinematic = 0x7F0, kPos = 0x38;
constexpr double kDownTime = 20.0, kRepairHold = 2.0;
static float g_repair_dist = 60.f; // coop.space repairdist <m> (tests: 0 = nobody can repair)

enum Kind : uint8_t { K_DOWN = 1, K_RAID = 2, K_MISSION = 3, K_TERRA = 4, K_GIFT = 5, K_GIFT_BACK = 6 };

static uintptr_t a_PlayerUFOGet, a_Context, a_ActivePlanet, a_StarMgr, a_GetStarRecord, a_ModeSpace, a_EnterSystem,
    a_LeavePlanet, a_TravelSystem, a_PlanetCleanup, a_PlanetCleanupMgr, a_AddHealth, a_SetHealth, a_Destroyed,
    a_SetState, a_TerraA, a_TerraW, a_TerraT, a_TerraMgr, a_SpaceGame, a_AddItemByKey, a_Trading, a_CreateTrading,
    a_AddMoney, a_PlayerEmpire, a_CinematicMgr, a_MessageMgr;

struct Key {
    uint32_t v[3];
};

// ---- state ----
static double g_down_until, g_repair_t, g_last_down;
static bool g_down;
static std::unordered_map<uint8_t, double> g_partner_down; // partner -> until
static uint32_t g_rescues, g_lost, g_raids_seen, g_raids_told, g_raid_bonus, g_missions_sent, g_tools_got,
    g_terra_sent, g_terra_got, g_gifts_sent, g_gifts_got, g_jumps, g_respawns_forced;
static std::unordered_map<uint8_t, uint32_t> g_last_star; // partner -> last known StarID
static bool g_applying;
static float g_terra_acc[3];
static uint32_t g_terra_planet;
static double g_terra_last;
static std::unordered_map<void*, double> g_raid_since; // raid mission -> start
static std::string g_last_raid_text;

static bool connected() { return C.sess && C.sess->connected(); }

// any cinematic of the game running (cCinematicManager +0x2C: 0 none, 2 playing; docs/re/space.md 7.2):
// the death caption, but also the respawn caption "We've built you a new ship..." (CVG2SPG_short)
static bool cinematic_playing()
{
    void* cm = a_CinematicMgr ? cdecl_call<void*>(a_CinematicMgr) : nullptr;
    return cm && F<uint32_t>(cm, 0x2C) != 0;
}
static bool in_space() { return g_ok && game::mode_id() == modes::kSpace; }

static void* my_ufo()
{
    void* mgr = a_PlayerUFOGet ? cdecl_call<void*>(a_PlayerUFOGet) : nullptr;
    return mgr ? F<void*>(mgr, 0x40) : nullptr;
}

static net::Vec3 ufo_pos(void* u)
{
    auto& p = F<float[3]>(u, kPos);
    return {p[0], p[1], p[2]};
}

static uint32_t star_of(uint32_t place)
{
    if (place & 0x80000000u)
        return place & 0x7FFFFFFFu;
    if (place & 0x40000000u)
        return place & 0xFFFFFFu; // PlanetID: star in the low 24 bits
    return 0;
}

static std::vector<Partner> partners_here()
{
    std::vector<Partner> r;
    uint32_t mine = C.my_place ? C.my_place() : 0;
    if (!mine || !C.partners)
        return r;
    for (auto& p : C.partners())
        if (p.place == mine)
            r.push_back(p);
    return r;
}

static void send(uint8_t kind, const net::Writer& body)
{
    if (!connected())
        return;
    net::Writer w;
    w.u8(kind);
    w.u8(C.sess->my_id());
    w.raw(body.data.data(), body.data.size());
    if (C.sess->role() == net::Role::Host)
        C.sess->broadcast_reliable(net::MSG_SPACE, w);
    else
        C.sess->send_reliable_to(0, net::MSG_SPACE, w);
}

static std::string name(uint8_t id) { return C.sess ? C.sess->name_of(id) : "?"; }

// ---------------- rescue ----------------
static void* my_combatant() { void* u = my_ufo(); return u ? (uint8_t*)u + kCombat : nullptr; }

bool disabled() { return g_down; }

float filter_own_damage(void* ufo, float dmg, int type)
{
    if (!in_space() || !connected() || ufo != my_ufo() || type == 10)
        return dmg;
    if (g_down)
        return 0.f;
    float hp = F<float>(ufo, kHp);
    double now = H->time_seconds();
    if (hp - dmg >= 1.f || hp <= 1.f || F<int>(ufo, kState) == 2 || now - g_last_down < 60)
        return dmg;
    auto here = partners_here();
    if (here.empty())
        return dmg; // nobody can help: the native death
    // disabled instead of destroyed: HP stays at 1, no more damage, no shooting
    g_down = true;
    g_down_until = now + kDownTime;
    g_last_down = now;
    g_repair_t = 0; // invincible from the next frame on (down_tick): this hit still shows
    net::Writer w;
    w.u8(1);
    w.u8((uint8_t)kDownTime);
    send(K_DOWN, w);
    if (C.notice)
        C.notice("your ship is disabled: a partner can repair it by flying close (20 s)");
    return std::max(0.f, hp - 1.f);
}

static void end_down(bool rescued)
{
    void* u = my_ufo();
    g_down = false;
    net::Writer w;
    w.u8(rescued ? 2 : 3);
    w.u8(0);
    send(K_DOWN, w);
    if (!u)
        return;
    F<uint8_t>(u, kInvincible) = 0;
    if (rescued) {
        g_rescues++;
        if (a_AddHealth)
            thiscall<void>(a_AddHealth, (uint8_t*)u + kCombat, F<float>(u, kMaxHp) * 0.5f);
        if (C.notice)
            C.notice("your ship is repaired");
        return;
    }
    // nobody came: the native death (cinematic, respawn at the nearest colony)
    g_lost++;
    if (a_SetHealth)
        thiscall<void>(a_SetHealth, (uint8_t*)u + kCombat, 0.f);
    F<int>(u, kState) = 2;
    if (a_Destroyed)
        thiscall<void>(a_Destroyed, u);
}

static void down_tick(double now)
{
    if (!g_down)
        return;
    void* u = my_ufo();
    if (!u || !in_space() || !connected()) {
        g_down = false;
        if (u)
            F<uint8_t>(u, kInvincible) = 0;
        return;
    }
    F<uint8_t>(u, kInvincible) = 1;
    bool close_by = false;
    for (auto& p : partners_here())
        if (net::dist(p.pos, ufo_pos(u)) < g_repair_dist)
            close_by = true;
    static double last = now;
    double dt = std::min(0.5, now - last);
    last = now;
    g_repair_t = close_by ? g_repair_t + dt : std::max(0.0, g_repair_t - dt);
    if (g_repair_t >= kRepairHold)
        end_down(true);
    else if (now > g_down_until)
        end_down(false);
}

// A destroyed ship waits for the end of its death cinematic (UFO message handler 0xC3B6A0 ->
// Respawn). If that cinematic is gone but the ship is still dead (the end message was lost, e.g. a
// fault in another listener of it), post the handler's own retry message 0x67B2A95: the game
// respawns the ship the native way (docs/re/space.md 7.2).
static void respawn_tick(double now)
{
    static double stuck_since;
    void* u = my_ufo();
    if (!u || F<int>(u, kState) != 2 || !F<uint32_t>(u, kCinematic) || cinematic_playing() || !a_MessageMgr) {
        stuck_since = 0;
        return;
    }
    if (!stuck_since)
        stuck_since = now;
    if (now - stuck_since < 5)
        return;
    stuck_since = now; // once per 5 s until the ship is back
    H->log(M, SK_WARN, "space: the ship is destroyed but no death cinematic runs: respawn (msg 0x67B2A95)");
    if (void* mm = cdecl_call<void*>(a_MessageMgr))
        vcall<void>(mm, 0x18 / 4, 0x67B2A95u, (void*)nullptr, (void*)nullptr, 0); // MessagePost
    g_respawns_forced++;
}

// ---------------- jump ----------------
static std::string travel_to(uint32_t star, bool force = false);

bool can_jump(std::string& to)
{
    if (!in_space() || !connected() || !C.partners)
        return false;
    uint32_t here = star_of(C.my_place ? C.my_place() : 0);
    for (auto& p : C.partners()) {
        uint32_t s = star_of(p.place);
        if (!s && g_last_star.count(p.id))
            s = g_last_star[p.id];
        if (s && s != here) {
            to = p.name;
            return true;
        }
    }
    return false;
}

std::string jump(uint8_t to)
{
    if (!in_space())
        return "not in space";
    if (g_down)
        return "the ship is disabled";
    uint32_t star = 0;
    if (C.partners)
        for (auto& p : C.partners())
            if (p.id == to || to == 0xFF) {
                star = star_of(p.place);
                if (!star && g_last_star.count(p.id))
                    star = g_last_star[p.id];
                if (star)
                    break;
            }
    if (!star)
        return "the partner's star is not known (in the galaxy view?)";
    if (star == star_of(C.my_place ? C.my_place() : 0))
        return "already in the partner's system";
    std::string e = travel_to(star);
    if (e.empty() && C.notice)
        C.notice("jumped to " + name(to == 0xFF ? 0 : to) + "'s star system");
    return e;
}

static std::string travel_to(uint32_t star, bool force)
{
    if (!a_ModeSpace || !a_TravelSystem || !a_Context || !a_StarMgr || !a_GetStarRecord)
        return "symbols missing";
    void* u = my_ufo();
    // leaving the planet while the respawn caption still plays destroys the planet object its end
    // handler uses: the next cinematic end faults and the ship is never respawned (docs/re/space.md 7.2)
    if (((u && F<uint32_t>(u, kCinematic)) || cinematic_playing()) && !force)
        return "a cinematic is playing";
    void* mode = *(void**)a_ModeSpace;
    int ctx = cdecl_call<int>(a_Context);
    void* rec = thiscall<void*>(a_GetStarRecord, cdecl_call<void*>(a_StarMgr), star);
    if (!mode || !rec || ctx < 0)
        return "can't travel now";
    // the native sequence of cGameDataUFO::Respawn (docs/re/space.md 7.3)
    if (ctx == 0) {
        void* planet = a_ActivePlanet ? cdecl_call<void*>(a_ActivePlanet) : nullptr;
        if (planet && a_PlanetCleanup && a_PlanetCleanupMgr && *(void**)a_PlanetCleanupMgr)
            thiscall<void>(a_PlanetCleanup, *(void**)a_PlanetCleanupMgr, planet);
        thiscall<void>(a_LeavePlanet, mode, 0);
    } else if (ctx == 2)
        thiscall<void>(a_EnterSystem, mode);
    thiscall<void>(a_TravelSystem, mode, rec, 1);
    g_jumps++;
    return "";
}

// ---------------- missions and raids ----------------
static bool is_raid(void* m)
{
    uint32_t id = F<uint32_t>(m, 0x78);
    return id == 0xA4B0A2DFu || id == 0xC4956AA7u || id == 0xD646C543u || id == 0x77184BAEu;
}

static uint32_t mission_place(void* m)
{
    void* planet = F<void*>(m, 0x1E8);
    void* rec = planet && !IsBadReadPtr(planet, 0x140) ? F<void*>(planet, 0x13C) : nullptr;
    return rec && !IsBadReadPtr(rec, 0x188) ? (F<uint32_t>(rec, 0x184) | 0x40000000u) : 0;
}

static void add_money(int n)
{
    void* emp = a_PlayerEmpire ? cdecl_call<void*>(a_PlayerEmpire) : nullptr;
    if (emp && a_AddMoney)
        thiscall<void>(a_AddMoney, emp, n);
}

static void* inventory()
{
    void* sg = a_SpaceGame ? *(void**)a_SpaceGame : nullptr;
    return sg ? F<void*>(sg, 0x40) : nullptr;
}

using SetState_t = void(SK_THISCALL*)(void*, int);
static SetState_t o_SetState;
static void SK_THISCALL h_SetState(void* m, int s)
{
    int old = m ? F<int>(m, 0x84) : -1;
    o_SetState(m, s);
    if (!m || old == s || !in_space() || !connected())
        return;
    if (is_raid(m)) {
        uint32_t place = mission_place(m);
        if (s == 3) { // a raid starts on our colony
            g_raids_seen++;
            g_raid_since[m] = H->time_seconds();
            net::Writer w;
            w.u32(place);
            w.u8(3);
            send(K_RAID, w);
        } else if (s == 5 || s == 6) {
            g_raid_since.erase(m);
            bool helped = false;
            for (auto& p : partners_here())
                (void)p, helped = true;
            if (s == 5 && helped) {
                add_money(2000); // money is shared: added once, here
                g_raid_bonus++;
                if (C.notice)
                    C.notice("colony defended together: +2000");
            }
            net::Writer w;
            w.u32(place);
            w.u8((uint8_t)s);
            send(K_RAID, w);
        }
        return;
    }
    if (s == 5 && !(F<uint32_t>(m, 0x130) & 1)) { // a mission (not an event) is completed
        net::Writer w;
        std::vector<Key> keys;
        Key k;
        memcpy(k.v, (uint8_t*)m + 0xA4, 12);
        if (k.v[0])
            keys.push_back(k);
        uint8_t* b = F<uint8_t*>(m, 0x11C);
        uint8_t* e = F<uint8_t*>(m, 0x120);
        for (uint8_t* it = b; it && it + 12 <= e && keys.size() < 8; it += 12) {
            memcpy(k.v, it, 12);
            if (k.v[0])
                keys.push_back(k);
        }
        w.f32(F<float>(m, 0xA0));
        w.u8((uint8_t)keys.size());
        for (auto& kk : keys)
            for (uint32_t v : kk.v)
                w.u32(v);
        send(K_MISSION, w);
        g_missions_sent++;
        if (!partners_here().empty()) {
            int bonus = (int)(F<float>(m, 0xA0) * 0.25f);
            if (bonus > 0) {
                add_money(bonus);
                if (C.notice)
                    C.notice("mission done with a partner nearby: +" + std::to_string(bonus));
            }
        }
    }
}

// ---------------- terraforming ----------------
using Terra_t = void(SK_THISCALL*)(void*, void*, float);
static Terra_t o_Terra[3];
static void note_terra(int which, void* planet, float d)
{
    if (g_applying || !in_space() || !connected() || !a_ActivePlanet || planet != cdecl_call<void*>(a_ActivePlanet))
        return;
    uint32_t place = C.my_place ? C.my_place() : 0;
    if (!(place & 0x40000000u))
        return;
    if (g_terra_planet != place) {
        g_terra_acc[0] = g_terra_acc[1] = g_terra_acc[2] = 0;
        g_terra_planet = place;
    }
    g_terra_acc[which] += d;
}
static void SK_THISCALL h_TerraA(void* tm, void* p, float d) { note_terra(0, p, d); o_Terra[0](tm, p, d); }
static void SK_THISCALL h_TerraW(void* tm, void* p, float d) { note_terra(1, p, d); o_Terra[1](tm, p, d); }
static void SK_THISCALL h_TerraT(void* tm, void* p, float d) { note_terra(2, p, d); o_Terra[2](tm, p, d); }

static void terra_tick(double now)
{
    if (now - g_terra_last < 0.5)
        return;
    g_terra_last = now;
    if (!g_terra_planet || (!g_terra_acc[0] && !g_terra_acc[1] && !g_terra_acc[2]))
        return;
    bool here = false;
    for (auto& p : partners_here())
        (void)p, here = true;
    if (here) {
        net::Writer w;
        w.u32(g_terra_planet);
        for (float v : g_terra_acc)
            w.f32(v);
        send(K_TERRA, w);
        g_terra_sent++;
    }
    g_terra_acc[0] = g_terra_acc[1] = g_terra_acc[2] = 0;
}

static void apply_terra(uint32_t planet_place, const float d[3])
{
    uint32_t mine = C.my_place ? C.my_place() : 0;
    if (mine != planet_place || !a_TerraMgr || !a_ActivePlanet)
        return;
    void* tm = cdecl_call<void*>(a_TerraMgr);
    void* p = cdecl_call<void*>(a_ActivePlanet);
    if (!tm || !p)
        return;
    g_applying = true;
    for (int i = 0; i < 3; i++)
        if (d[i] != 0 && o_Terra[i])
            o_Terra[i](tm, p, d[i]);
    g_applying = false;
    g_terra_got++;
}

// ---------------- cargo ----------------
// the first spice (trading object, item type 4) in the cargo
static int find_spice(void* inv, Key& key, int& count)
{
    if (!inv)
        return -1;
    int n = vcall<int>(inv, 0x5C / 4);
    for (int i = 0; i < n && i < 256; i++) {
        uint8_t* it = vcall<uint8_t*>(inv, 0x60 / 4, i);
        if (!it || F<int>(it, 0x14) != 4)
            continue;
        const uint32_t* k = vcall<const uint32_t*>(it, 0x18 / 4);
        if (!k)
            continue;
        memcpy(key.v, k, 12);
        count = F<int>(it, 0x10);
        return i;
    }
    return -1;
}

static bool add_spice(const Key& key, int n)
{
    void* inv = inventory();
    void* tr = a_Trading ? cdecl_call<void*>(a_Trading) : nullptr;
    if (!inv || !tr || !a_CreateTrading || n <= 0)
        return false;
    void* item = nullptr;
    thiscall<void>(a_CreateTrading, tr, &item, key.v, n, 0.f);
    if (!item)
        return false;
    bool ok = vcall<bool>(inv, 0x80 / 4, item, (void*)nullptr, true);
    vcall<void>(item, 0x08 / 4); // Release
    return ok;
}

static uint8_t gift_target()
{
    if (C.sess)
        for (auto& p : C.sess->players())
            if (p.id != C.sess->my_id())
                return p.id;
    return 0xFF;
}

int spice(std::string& to)
{
    if (!in_space() || !connected())
        return -1;
    uint8_t t = gift_target();
    if (t == 0xFF)
        return -1;
    to = name(t);
    Key k;
    int c = 0;
    return find_spice(inventory(), k, c) >= 0 ? c : 0;
}

int give_spice(int n)
{
    uint8_t to = gift_target();
    void* inv = inventory();
    Key k;
    int c = 0;
    int i = find_spice(inv, k, c);
    if (to == 0xFF || i < 0 || n <= 0)
        return 0;
    n = std::min(n, c);
    void* out = nullptr;
    if (!vcall<bool>(inv, 0x7C / 4, i, &out, n == c ? -1 : n))
        return 0;
    if (out)
        vcall<void>(out, 0x08 / 4);
    net::Writer w;
    w.u8(to);
    for (uint32_t v : k.v)
        w.u32(v);
    w.u32((uint32_t)n);
    send(K_GIFT, w);
    g_gifts_sent++;
    if (C.notice)
        C.notice(std::to_string(n) + " spice sent to " + name(to));
    return n;
}

// ---------------- messages ----------------
void on_message(uint8_t type, uint8_t from, net::Reader& r)
{
    if (type != net::MSG_SPACE || !g_ok)
        return;
    if (C.sess->role() == net::Role::Host) { // relay to the other guests
        net::Writer w;
        w.raw(r.cur(), r.left());
        C.sess->broadcast_reliable(net::MSG_SPACE, w, from);
    }
    uint8_t kind = r.u8(), origin = r.u8();
    if (!r.ok() || origin == C.sess->my_id())
        return;
    double now = H->time_seconds();
    switch (kind) {
    case K_DOWN: {
        uint8_t st = r.u8(), secs = r.u8();
        if (st == 1) {
            g_partner_down[origin] = now + secs;
            if (C.notice)
                C.notice(name(origin) + "'s ship is disabled: fly close to repair it");
        } else {
            g_partner_down.erase(origin);
            if (C.notice)
                C.notice(st == 2 ? name(origin) + "'s ship is repaired" : name(origin) + "'s ship was destroyed");
        }
        break;
    }
    case K_RAID: {
        uint32_t place = r.u32();
        uint8_t st = r.u8();
        if (st == 3) {
            g_raids_told++;
            if (star_of(place))
                g_last_star[origin] = star_of(place);
            g_last_raid_text = name(origin) + "'s colony is raided";
            if (C.notice)
                C.notice(g_last_raid_text + ": jump there from the co-op window (coop.space jump)");
        } else if (C.notice)
            C.notice(st == 5 ? name(origin) + "'s colony is safe" : name(origin) + "'s colony was plundered");
        break;
    }
    case K_MISSION: {
        r.f32();
        uint8_t n = r.u8();
        void* inv = inventory();
        int got = 0;
        for (int i = 0; i < n && r.ok(); i++) {
            Key k;
            for (auto& v : k.v)
                v = r.u32();
            if (!inv || !a_AddItemByKey || !r.ok())
                continue;
            if (vcall<bool>(inv, 0x74 / 4, k.v)) // HasTool: no second copy
                continue;
            thiscall<void>(a_AddItemByKey, inv, k.v);
            got++;
        }
        g_tools_got += got;
        if (C.notice)
            C.notice(name(origin) + " completed a mission" +
                     (got ? ": you get its reward tools too (" + std::to_string(got) + ")" : ""));
        break;
    }
    case K_TERRA: {
        uint32_t place = r.u32();
        float d[3] = {r.f32(), r.f32(), r.f32()};
        if (r.ok())
            apply_terra(place, d);
        break;
    }
    case K_GIFT:
    case K_GIFT_BACK: {
        uint8_t to = r.u8();
        Key k;
        for (auto& v : k.v)
            v = r.u32();
        int n = (int)r.u32();
        if (!r.ok() || to != C.sess->my_id())
            break;
        if (add_spice(k, n)) {
            if (kind == K_GIFT)
                g_gifts_got++;
            if (C.notice)
                C.notice(kind == K_GIFT ? name(origin) + " gave you " + std::to_string(n) + " spice"
                                        : name(origin) + " had no room: " + std::to_string(n) + " spice came back");
        } else if (kind == K_GIFT) { // no free cargo slot: send it back
            net::Writer w;
            w.u8(origin);
            for (uint32_t v : k.v)
                w.u32(v);
            w.u32((uint32_t)n);
            send(K_GIFT_BACK, w);
            if (C.notice)
                C.notice("no free cargo slot for " + name(origin) + "'s spice");
        }
        break;
    }
    }
}

// ---------------- frame ----------------
void tick(double now)
{
    if (!in_space() || !connected())
        return;
    if (C.partners)
        for (auto& p : C.partners())
            if (uint32_t s = star_of(p.place))
                g_last_star[p.id] = s;
    down_tick(now);
    respawn_tick(now);
    terra_tick(now);
    for (auto it = g_partner_down.begin(); it != g_partner_down.end();)
        it = now > it->second + 2 ? g_partner_down.erase(it) : std::next(it);
    // a disabled partner here: a beacon on its ship
    if (C.beacon)
        for (auto& p : partners_here())
            if (g_partner_down.count(p.id)) {
                int left = (int)std::max(0.0, g_partner_down[p.id] - now);
                C.beacon(p.pos, "REPAIR " + p.name + " (" + std::to_string(left) + " s)", 0.25, 0xFFFF6040);
            }
}

void draw_overlay()
{
    if (!g_down || !in_space())
        return;
    int sw = 0, sh = 0;
    H->screen_size(&sw, &sh);
    double now = H->time_seconds();
    int left = (int)std::max(0.0, g_down_until - now);
    char b[160];
    snprintf(b, sizeof b, "SHIP DISABLED  %d s  -  a partner repairs it by flying close", left);
    uint32_t col = ((int)(now * 3)) & 1 ? 0xFFFF5030 : 0xFFFFD040;
    H->overlay_text(sw / 2 - (int)strlen(b) * 9 / 2, sh / 4, col, b);
    if (g_repair_t > 0) {
        std::string bar = "repair ";
        int n = (int)(g_repair_t / kRepairHold * 10);
        for (int i = 0; i < 10; i++)
            bar += i < n ? "■" : "□";
        H->overlay_text(sw / 2 - 80, sh / 4 + 26, 0xFF60FF60, bar.c_str());
    }
}

void on_session_end()
{
    if (g_down) {
        g_down = false;
        if (void* u = my_ufo())
            F<uint8_t>(u, kInvincible) = 0;
    }
    g_partner_down.clear();
    g_last_star.clear();
    g_raid_since.clear();
    g_terra_planet = 0;
}

bool init(const SkHost* host, SkMod* mod, Ctx ctx)
{
    H = host;
    M = mod;
    C = std::move(ctx);
    auto S = [](const char* n) { return H->sym(n); };
    a_PlayerUFOGet = S("Simulator::cSimulatorPlayerUFO::Get");
    a_Context = S("SporeCoop::Space_GetCurrentContext");
    a_ActivePlanet = S("SporeCoop::Space_GetActivePlanet");
    a_StarMgr = S("Simulator::cStarManager::Get");
    a_GetStarRecord = S("Simulator::cStarManager::GetStarRecord");
    a_ModeSpace = S("Simulator::cAppModeSpace::ptr");
    a_EnterSystem = S("Simulator::cAppModeSpace::EnterSolarSystem");
    a_LeavePlanet = S("Simulator::cAppModeSpace::LeavePlanet");
    a_TravelSystem = S("Simulator::cAppModeSpace::TravelToStarSystem");
    a_PlanetCleanup = S("SporeCoop::PlanetModelCleanup");
    a_PlanetCleanupMgr = S("SporeCoop::PlanetModelMgr_ptr");
    a_AddHealth = S("Simulator::cCombatant::AddHealth");
    a_SetHealth = S("Simulator::cCombatant::SetHealth");
    a_Destroyed = S("Simulator::cGameDataUFO::PlayerDestroyed");
    a_SetState = S("Simulator::cMission::SetState");
    a_TerraA = S("Simulator::cTerraformingManager::AddAtmosphereDelta");
    a_TerraW = S("Simulator::cTerraformingManager::AddWaterDelta");
    a_TerraT = S("Simulator::cTerraformingManager::AddTemperatureDelta");
    a_TerraMgr = S("Simulator::cTerraformingManager::Get");
    a_SpaceGame = S("Simulator::cSimulatorSpaceGame::ptr");
    a_AddItemByKey = S("Simulator::cPlayerInventory::AddItemByKey");
    a_Trading = S("Simulator::cSpaceTrading::Get");
    a_CreateTrading = S("Simulator::cSpaceTrading::CreateTradingObject");
    a_AddMoney = S("Simulator::cEmpire::AddMoney");
    a_PlayerEmpire = S("Simulator::GetPlayerEmpire");
    a_CinematicMgr = S("Simulator::cCinematicManager::Get");
    a_MessageMgr = S("App::IMessageManager::Get");
    g_ok = a_PlayerUFOGet && a_Context && a_ActivePlanet;
    if (a_SetState)
        H->hook_create(M, (void*)a_SetState, (void*)h_SetState, (void**)&o_SetState);
    if (a_TerraA && a_TerraW && a_TerraT) {
        H->hook_create(M, (void*)a_TerraA, (void*)h_TerraA, (void**)&o_Terra[0]);
        H->hook_create(M, (void*)a_TerraW, (void*)h_TerraW, (void**)&o_Terra[1]);
        H->hook_create(M, (void*)a_TerraT, (void*)h_TerraT, (void**)&o_Terra[2]);
    }
    H->log(M, SK_INFO, "space co-op: rescue %s, jump %s, missions %s, terraform %s, cargo %s",
           a_AddHealth && a_Destroyed ? "on" : "off", a_TravelSystem && a_GetStarRecord ? "on" : "off",
           a_SetState ? "on" : "off", a_TerraA ? "on" : "off", a_CreateTrading && a_Trading ? "on" : "off");
    return g_ok;
}

std::vector<syncdbg::Entry> nav_entries()
{
    std::vector<syncdbg::Entry> v;
    if (!in_space() || !a_StarMgr || !a_Context)
        return v;
    int ctx = cdecl_call<int>(a_Context);
    void* sm = cdecl_call<void*>(a_StarMgr);
    if (!sm || IsBadReadPtr(sm, 0xD0))
        return v;
    char key[24];
    if (ctx == 1) {
        // the loaded cPlanet objects of the current system: cStarManager +0x50..+0x54 (the list
        // RecordToPlanet 0xBB5B20 searches); cPlanet +0x04 position, +0x13C record (docs/re/space.md 7.9)
        void** b = F<void**>(sm, 0x50);
        void** e = F<void**>(sm, 0x54);
        for (void** it = b; it && it < e && v.size() < 64; ++it) {
            void* p = *it;
            if (!p || IsBadReadPtr(p, 0x140))
                continue;
            void* rec = F<void*>(p, 0x13C);
            if (!rec || IsBadReadPtr(rec, 0x198))
                continue;
            snprintf(key, sizeof key, "%c%08X", F<int>(rec, 0x194) == 5 ? 'C' : 'P', F<uint32_t>(rec, 0x184));
            auto& q = F<float[3]>(p, 0x04);
            v.push_back({key, {q[0], q[1], q[2]}, {0, 0, 0, 1}});
        }
    } else if (ctx == 2) {
        // galaxy view: star records near the own ship. cStarManager +0xC8 = sectors (20 bytes each,
        // first dword = array of cStarRecord*, as GetStarRecord 0xBA6EB0 indexes them: StarID =
        // sector << 12 | index); cStarRecord +0x3C position (galaxy coordinates), +0x54 empire id,
        // +0x70 StarID (docs/re/space.md 7.9)
        void* me = my_ufo();
        if (!me)
            return v;
        net::Vec3 at = ufo_pos(me);
        uint8_t* sb = F<uint8_t*>(sm, 0xC8);
        uint8_t* se = F<uint8_t*>(sm, 0xCC);
        if (!sb || se <= sb || (se - sb) / 20 > 4096 || IsBadReadPtr(sb, se - sb))
            return v;
        for (uint8_t* sec = sb; sec + 20 <= se && v.size() < 400; sec += 20) {
            void** b = F<void**>(sec, 0);
            void** e = F<void**>(sec, 4);
            if (!b || e <= b || e - b > 4096 || IsBadReadPtr(b, (e - b) * 4))
                continue;
            for (void** it = b; it < e && v.size() < 400; ++it) {
                void* r = *it;
                if (!r || IsBadReadPtr(r, 0x74))
                    continue;
                auto& q = F<float[3]>(r, 0x3C);
                net::Vec3 p{q[0], q[1], q[2]};
                if (net::dist(p, at) > 60.f)
                    continue;
                snprintf(key, sizeof key, "%c%08X", F<uint32_t>(r, 0x54) == F<uint32_t>(me, 0x30) ? 'H' : 'S',
                         F<uint32_t>(r, 0x70));
                v.push_back({key, p, {0, 0, 0, 1}});
            }
        }
    }
    return v;
}

std::string status()
{
    char b[400];
    snprintf(b, sizeof b,
             "space co-op: down=%d rescues=%u lost=%u raids=%u raids_told=%u raid_bonus=%u missions_sent=%u "
             "tools_got=%u terra_sent=%u terra_got=%u gifts_sent=%u gifts_got=%u jumps=%u respawns_forced=%u "
             "partners_here=%u partners_down=%u\n",
             (int)g_down, g_rescues, g_lost, g_raids_seen, g_raids_told, g_raid_bonus, g_missions_sent, g_tools_got,
             g_terra_sent, g_terra_got, g_gifts_sent, g_gifts_got, g_jumps, g_respawns_forced,
             (unsigned)partners_here().size(), (unsigned)g_partner_down.size());
    return b;
}

// tests: coop.space down | hp | jump [id] | give <n> | spice add <n> | terra <a> <w> <t> | money
std::string debug(const std::vector<std::string>& a)
{
    if (a.empty())
        return status();
    void* u = my_ufo();
    char b[200];
    if (a[0] == "hit" && a.size() > 1 && u) { // the game's damage path on the own ship
        float hp0 = F<float>(u, kHp);
        static const float zero[3] = {0, 0, 0}; // the hit direction must not be null
        vcall<int>((uint8_t*)u + kCombat, 0x18 / 4, (float)atof(a[1].c_str()), 0xFFFFFFFFu, 0, zero,
                   (void*)nullptr);
        snprintf(b, sizeof b, "hp %.1f -> %.1f max %.1f down=%d invincible=%d state=%d", hp0, F<float>(u, kHp),
                 F<float>(u, kMaxHp), (int)g_down, (int)F<uint8_t>(u, kInvincible), F<int>(u, kState));
        return b;
    }
    if (a[0] == "hp" && u) {
        // cine=1: the death caption ("...you return to your homeworld") waits for the player's OK;
        // movie=1: any cinematic runs (also the respawn caption "We've built you a new ship...")
        snprintf(b, sizeof b, "hp %.1f max %.1f down=%d left=%.1f repair=%.1f state=%d cine=%d movie=%d",
                 F<float>(u, kHp), F<float>(u, kMaxHp), (int)g_down, g_down ? g_down_until - H->time_seconds() : 0.0,
                 g_repair_t, F<int>(u, kState), F<uint32_t>(u, kCinematic) ? 1 : 0, (int)cinematic_playing());
        return b;
    }
    if (a[0] == "jump") {
        std::string e = jump(a.size() > 1 ? (uint8_t)atoi(a[1].c_str()) : 0xFF);
        return e.empty() ? "jumping" : e;
    }
    if (a[0] == "star") { // the own star id
        uint32_t pl = C.my_place ? C.my_place() : 0;
        snprintf(b, sizeof b, "place %08X star %u", pl, star_of(pl));
        return b;
    }
    if (a[0] == "land") { // tests: land on the active planet (the landing of Respawn)
        uintptr_t f = H->sym("SporeCoop::cAppModeSpace_LandOnActivePlanet");
        void* mode = a_ModeSpace ? *(void**)a_ModeSpace : nullptr;
        if (!f || !mode || cdecl_call<int>(a_Context) != 1)
            return "not in a system";
        if (!cdecl_call<void*>(a_ActivePlanet) || !u || F<uint32_t>(u, kCinematic) || cinematic_playing() ||
            F<int>(u, kState) == 2)
            return "no active planet / the ship is not flying";
        thiscall<void>(f, mode);
        return "landing";
    }
    if (a[0] == "jumpto" && a.size() > 1) { // "force": even during a cinematic (tests: the old jump)
        std::string e = travel_to((uint32_t)strtoul(a[1].c_str(), nullptr, 0), a.size() > 2 && a[2] == "force");
        return e.empty() ? "jumping" : e;
    }
    if (a[0] == "goto" && u) { // put the own ship next to a partner (tests of the repair)
        for (auto& p : C.partners ? C.partners() : std::vector<Partner>{})
            if (C.my_place && p.place == C.my_place()) {
                float q[3] = {p.pos.x + 20.f, p.pos.y, p.pos.z};
                memcpy((uint8_t*)u + kPos, q, 12);
                memcpy((uint8_t*)u + 0x718, q, 12); // next position
                memcpy((uint8_t*)u + 0x750, q, 12); // destination
                snprintf(b, sizeof b, "moved next to %s (%.0f %.0f %.0f)", p.name.c_str(), q[0], q[1], q[2]);
                return b;
            }
        return "no partner here";
    }
    if (a[0] == "repairdist" && a.size() > 1) { // tests: the repair range (0: a disabled ship is never repaired)
        g_repair_dist = (float)atof(a[1].c_str());
        snprintf(b, sizeof b, "repair range %.0f m", g_repair_dist);
        return b;
    }
    if (a[0] == "dist" && u) {
        std::string s;
        for (auto& p : partners_here()) {
            snprintf(b, sizeof b, "%s %.1f m; ", p.name.c_str(), net::dist(p.pos, ufo_pos(u)));
            s += b;
        }
        return s.empty() ? "no partner here" : s;
    }
    if (a[0] == "give" && a.size() > 1)
        return "given " + std::to_string(give_spice(atoi(a[1].c_str())));
    if (a[0] == "spice") {
        Key k;
        int c = 0;
        int i = find_spice(inventory(), k, c);
        if (a.size() > 2 && a[1] == "add") {
            if (i < 0)
                return "no spice to copy the key from";
            return add_spice(k, atoi(a[2].c_str())) ? "added" : "not added";
        }
        if (a.size() > 2 && a[1] == "make") { // tests: spice of the local planet (record+0x198)
            uintptr_t home = H->sym("SporeCoop::GetPlayerHomePlanetRecord");
            void* p = a_ActivePlanet ? cdecl_call<void*>(a_ActivePlanet) : nullptr;
            void* recs[2] = {p ? F<void*>(p, 0x13C) : nullptr, home ? cdecl_call<void*>(home) : nullptr};
            for (void* rec : recs)
                if (rec && F<uint32_t>(rec, 0x198)) {
                    memcpy(k.v, (uint8_t*)rec + 0x198, 12);
                    return add_spice(k, atoi(a[2].c_str())) ? "made" : "not added (no cargo slot?)";
                }
            return "no planet spice key";
        }
        snprintf(b, sizeof b, "spice slot %d count %d key %08X %08X %08X", i, c, k.v[0], k.v[1], k.v[2]);
        return b;
    }
    if (a[0] == "terra" && a.size() > 3) { // as if the own tool changed the planet
        void* tm = a_TerraMgr ? cdecl_call<void*>(a_TerraMgr) : nullptr;
        void* p = a_ActivePlanet ? cdecl_call<void*>(a_ActivePlanet) : nullptr;
        if (!tm || !p)
            return "not on a planet";
        h_TerraA(tm, p, (float)atof(a[1].c_str()));
        h_TerraW(tm, p, (float)atof(a[2].c_str()));
        h_TerraT(tm, p, (float)atof(a[3].c_str()));
        void* rec = F<void*>(p, 0x13C);
        snprintf(b, sizeof b, "planet b0=%.3f b4=%.3f b8=%.3f", F<float>(rec, 0xB0), F<float>(rec, 0xB4),
                 F<float>(rec, 0xB8));
        return b;
    }
    if (a[0] == "tools" || (a[0] == "tool" && a.size() > 2 && a[1] == "add")) {
        // tests: the tools in the inventory (instance hash = FNV-1 of the tool file name, e.g. scan,
        // abduct, interplanetarydrive, interstellardrive) and adding one by name (setup: the fixture's
        // ship has no interstellar drive yet, docs/re/space.md 7.7)
        void* inv = inventory();
        if (!inv)
            return "no inventory";
        Key tk{};
        std::string s;
        int n = vcall<int>(inv, 0x5C / 4);
        for (int i = 0; i < n && i < 256; i++) {
            uint8_t* it = vcall<uint8_t*>(inv, 0x60 / 4, i);
            if (!it || F<int>(it, 0x14) != 2)
                continue;
            const uint32_t* k = vcall<const uint32_t*>(it, 0x18 / 4);
            if (!k)
                continue;
            memcpy(tk.v, k, 12);
            snprintf(b, sizeof b, "tool %08X %08X %08X\n", k[0], k[1], k[2]);
            s += b;
        }
        if (a[0] == "tools")
            return s.empty() ? "no tools" : s;
        if (!tk.v[0] || !a_AddItemByKey) // tool keys are {instance, 0, 0} in this build
            return "no tool to copy the key type/group from";
        const std::string& nm = a[2];
        uint32_t h = 0x811C9DC5u;
        if (nm.size() > 2 && nm[0] == '0' && nm[1] == 'x')
            h = (uint32_t)strtoul(nm.c_str(), nullptr, 16);
        else
            for (char c : nm)
                h = (h * 0x01000193u) ^ (uint8_t)tolower((unsigned char)c);
        tk.v[0] = h;
        if (vcall<bool>(inv, 0x74 / 4, tk.v)) // HasTool
            return "already there";
        thiscall<void>(a_AddItemByKey, inv, tk.v);
        snprintf(b, sizeof b, "added %08X: %s", h, vcall<bool>(inv, 0x74 / 4, tk.v) ? "ok" : "not in the inventory");
        return b;
    }
    if (a[0] == "planet") {
        void* p = a_ActivePlanet ? cdecl_call<void*>(a_ActivePlanet) : nullptr;
        void* rec = p ? F<void*>(p, 0x13C) : nullptr;
        if (!rec)
            return "not on a planet";
        snprintf(b, sizeof b, "planet b0=%.3f b4=%.3f b8=%.3f", F<float>(rec, 0xB0), F<float>(rec, 0xB4),
                 F<float>(rec, 0xB8));
        return b;
    }
    return "usage: coop.space [hit <dmg> | hp | jump [id] | give <n> | spice [add <n>] | terra <a> <w> <t> | planet]";
}

} // namespace spacecoop
