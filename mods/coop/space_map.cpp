// Space feature "map" (MSG_SPACE kinds 10..19): the roster of every player's empire (space_map.h)
// and the partners and their colonies on the galaxy map. docs/re/space.md 10.
#include "space_map.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>

#include "modes.h"
#include "space_ext.h"
#include "sporekit_game.hpp"

using skg::cdecl_call;
using skg::thiscall;

namespace spacemap {

static const SkHost* H;
static SkMod* M;

template <typename T>
static T& F(void* p, uintptr_t off) { return *(T*)((uint8_t*)p + off); }

enum Kind : uint8_t {
    K_ROSTER = 10,     // u32 version, u8 part, u8 parts, [part 0: head], u8 n, n colonies
    K_ROSTER_REQ = 11, // (nothing): everybody sends its roster now (a player joined / rejoined)
};

// packets are at most 1200 bytes (kind, origin and the session header come on top)
constexpr size_t kChunkBytes = 1000;
constexpr double kPeriod = 5.0, kScan = 1.0;

static uintptr_t a_PlayerEmpire, a_StarMgr, a_GetStarRecord, a_PlayerUFOGet;

// ---------------- reading the game ----------------
static bool readable(const void* p, size_t n) { return p && !IsBadReadPtr(p, n); }

// eastl::string16 {begin, end, capacity} -> UTF-8
static std::string str16(void* s)
{
    if (!readable(s, 12))
        return "";
    const char16_t* b = F<const char16_t*>(s, 0);
    const char16_t* e = F<const char16_t*>(s, 4);
    if (!b || e <= b || e - b > 256 || !readable(b, (e - b) * 2))
        return "";
    std::string out;
    for (const char16_t* c = b; c < e; c++) {
        uint32_t u = *c;
        if (u >= 0xD800 && u < 0xDC00 && c + 1 < e) { // surrogate pair
            u = 0x10000 + ((u - 0xD800) << 10) + (c[1] - 0xDC00);
            c++;
        }
        if (u < 0x80)
            out += (char)u;
        else if (u < 0x800) {
            out += (char)(0xC0 | u >> 6);
            out += (char)(0x80 | (u & 0x3F));
        } else if (u < 0x10000) {
            out += (char)(0xE0 | u >> 12);
            out += (char)(0x80 | ((u >> 6) & 0x3F));
            out += (char)(0x80 | (u & 0x3F));
        } else {
            out += (char)(0xF0 | u >> 18);
            out += (char)(0x80 | ((u >> 12) & 0x3F));
            out += (char)(0x80 | ((u >> 6) & 0x3F));
            out += (char)(0x80 | (u & 0x3F));
        }
    }
    return out;
}

// eastl::vector<T*> {begin, end, capacity}: the pointers, empty if not readable
static std::vector<void*> ptr_vec(void* v, size_t max = 4096)
{
    std::vector<void*> out;
    if (!readable(v, 8))
        return out;
    void** b = F<void**>(v, 0);
    void** e = F<void**>(v, 4);
    if (!b || e <= b || (size_t)(e - b) > max || !readable(b, (e - b) * 4))
        return out;
    out.assign(b, e);
    return out;
}

static void* player_empire() { return a_PlayerEmpire ? cdecl_call<void*>(a_PlayerEmpire) : nullptr; }

static void* star_record(uint32_t id)
{
    if (!a_StarMgr || !a_GetStarRecord || !id)
        return nullptr;
    void* sm = cdecl_call<void*>(a_StarMgr);
    void* r = sm ? thiscall<void*>(a_GetStarRecord, sm, id) : nullptr;
    return readable(r, 0xB0) ? r : nullptr;
}

std::string star_name(uint32_t star)
{
    void* r = star_record(star);
    return r ? str16((uint8_t*)r + 0x60) : ""; // cStarRecord::mName
}

static uint32_t star_of(uint32_t place)
{
    if (place & 0x80000000u)
        return place & 0x7FFFFFFFu;
    if (place & 0x40000000u)
        return place & 0xFFFFFFu; // PlanetID: star in the low 24 bits
    return 0;
}

// cEmpire (ModAPI layout, checked at runtime, docs/re/space.md 10.1): cIdentityColorable at +0x0C
// (mIDColorID +0x10, mCachedColor ColorRGB floats +0x30), mEmpireName string16 +0x3C, mPoliticalID
// +0x84, mStars vector<cStarRecord*> +0x88, mHomeStar +0xB0, mHomePlanet +0xB4, money +0xD0.
// cStarRecord: position +0x3C, mEmpireID +0x54, mName +0x60, StarID +0x70, mPlanets +0x84.
// cPlanetRecord: mName +0x18, mCivData +0x15C (cCivData: pol +0x14, mCities vector +0x3C),
// PlanetID +0x184, tech level +0x194 (5 = empire colony), spice key +0x198.
static bool read_local(Roster& r)
{
    void* emp = player_empire();
    if (!readable(emp, 0xD4))
        return false;
    r.political_id = F<uint32_t>(emp, 0x84);
    r.empire_name = str16((uint8_t*)emp + 0x3C);
    const float* c = &F<float>(emp, 0x30);
    if (c[0] >= 0 && c[0] <= 1.001f && c[1] >= 0 && c[1] <= 1.001f && c[2] >= 0 && c[2] <= 1.001f &&
        c[0] + c[1] + c[2] > 0.01f)
        r.color = 0xFF000000u | (uint32_t)(c[0] * 255.f + .5f) << 16 | (uint32_t)(c[1] * 255.f + .5f) << 8 |
                  (uint32_t)(c[2] * 255.f + .5f);
    r.home_star = F<uint32_t>(emp, 0xB0);
    r.home_planet = F<uint32_t>(emp, 0xB4);
    if (r.home_star == 0xFFFFFFFFu)
        r.home_star = 0;
    if (r.home_planet == 0xFFFFFFFFu)
        r.home_planet = 0;
    r.money = F<int>(emp, 0xD0);
    r.place = spaceext::ctx().my_place ? spaceext::ctx().my_place() : 0;
    r.star_now = star_of(r.place);
    if (r.star_now) {
        if (void* s = star_record(r.star_now)) {
            auto& q = F<float[3]>(s, 0x3C);
            r.galaxy_pos = {q[0], q[1], q[2]};
        }
    } else if (void* mgr = a_PlayerUFOGet ? cdecl_call<void*>(a_PlayerUFOGet) : nullptr) {
        void* u = F<void*>(mgr, 0x40);
        if (readable(u, 0x48)) {
            auto& q = F<float[3]>(u, 0x38);
            r.galaxy_pos = {q[0], q[1], q[2]};
        }
    }
    r.colonies.clear();
    for (void* s : ptr_vec((uint8_t*)emp + 0x88, 1024)) {
        if (!readable(s, 0x98))
            continue;
        for (void* p : ptr_vec((uint8_t*)s + 0x84, 64)) {
            if (!readable(p, 0x1A4) || F<int>(p, 0x194) != 5)
                continue;
            Colony col;
            col.planet = F<uint32_t>(p, 0x184);
            col.star = col.planet & 0xFFFFFFu;
            bool mine = F<uint32_t>(s, 0x54) == r.political_id;
            for (void* cd : ptr_vec((uint8_t*)p + 0x15C, 32))
                if (readable(cd, 0x48) && F<uint32_t>(cd, 0x14) == r.political_id) {
                    mine = true;
                    void** b = F<void**>(cd, 0x3C);
                    void** e = F<void**>(cd, 0x40);
                    if (b && e > b && e - b < 1000)
                        col.cities = (uint16_t)(col.cities + (e - b));
                }
            if (!mine)
                continue;
            col.name = str16((uint8_t*)p + 0x18);
            memcpy(col.spice, (uint8_t*)p + 0x198, 12);
            col.home = col.planet == r.home_planet;
            r.colonies.push_back(std::move(col));
            if (r.colonies.size() >= 250)
                break;
        }
    }
    return true;
}

// ---------------- the rosters ----------------
static std::map<uint8_t, Roster> g_rosters;
static uint32_t g_changes;
static std::vector<std::function<void(uint8_t)>> g_listeners;

struct Pending {
    uint32_t version = 0;
    uint8_t parts = 0;
    std::set<uint8_t> got;
    Roster r;
};
static std::map<uint8_t, Pending> g_pending;

static std::vector<uint8_t> g_last_bytes; // my roster's content as last sent (change detection)
static uint32_t g_version;
static double g_next_scan, g_last_send;
static bool g_force_send, g_requested;
static std::set<uint8_t> g_players; // session players seen (a new one gets everybody's roster)
static uint32_t g_sent, g_got, g_reqs;

const Roster* roster_of(uint8_t id)
{
    auto it = g_rosters.find(id);
    return it == g_rosters.end() ? nullptr : &it->second;
}

std::vector<uint8_t> roster_ids()
{
    std::vector<uint8_t> v;
    uint8_t me = spaceext::my_id();
    if (g_rosters.count(me))
        v.push_back(me);
    for (auto& [id, r] : g_rosters)
        if (id != me)
            v.push_back(id);
    return v;
}

uint32_t roster_changes() { return g_changes; }
void on_roster_change(std::function<void(uint8_t)> f) { g_listeners.push_back(std::move(f)); }

static void changed(uint8_t id)
{
    g_changes++;
    for (auto& f : g_listeners)
        f(id);
}

static void put_colony(net::Writer& w, const Colony& c)
{
    w.u32(c.planet);
    w.str8(c.name.substr(0, 64));
    for (uint32_t k : c.spice)
        w.u32(k);
    w.str8(c.spice_name.substr(0, 32));
    w.u16(c.cities);
    w.u8(c.home ? 1 : 0);
}

static Colony get_colony(net::Reader& r)
{
    Colony c;
    c.planet = r.u32();
    c.star = c.planet & 0xFFFFFFu;
    c.name = r.str8();
    for (uint32_t& k : c.spice)
        k = r.u32();
    c.spice_name = r.str8();
    c.cities = r.u16();
    c.home = r.u8() & 1;
    return c;
}

static void put_head(net::Writer& w, const Roster& r, bool with_pos)
{
    w.u32(r.home_star);
    w.u32(r.home_planet);
    w.u32(r.place);
    w.f32(with_pos ? r.galaxy_pos.x : 0.f);
    w.f32(with_pos ? r.galaxy_pos.y : 0.f);
    w.f32(with_pos ? r.galaxy_pos.z : 0.f);
    w.str8(r.empire_name.substr(0, 64));
    w.u32(r.color);
    w.u32(r.political_id);
    w.u32((uint32_t)r.money);
}

static void get_head(net::Reader& rd, Roster& r)
{
    r.home_star = rd.u32();
    r.home_planet = rd.u32();
    r.place = rd.u32();
    r.star_now = star_of(r.place);
    r.galaxy_pos.x = rd.f32();
    r.galaxy_pos.y = rd.f32();
    r.galaxy_pos.z = rd.f32();
    r.empire_name = rd.str8();
    r.color = rd.u32();
    r.political_id = rd.u32();
    r.money = (int)rd.u32();
}

// what counts as a change: everything but the ship's galaxy position (it moves while flying) and
// the money (it changes all the time); those go out with the periodic resend
static std::vector<uint8_t> content(const Roster& r)
{
    net::Writer w;
    Roster c = r;
    c.money = 0;
    put_head(w, c, false);
    for (auto& col : r.colonies)
        put_colony(w, col);
    return w.data;
}

static void send_roster(const Roster& r)
{
    // part 0: the head and the first colonies; then as many parts as the colonies need
    std::vector<net::Writer> parts;
    size_t i = 0;
    do {
        net::Writer body;
        if (parts.empty())
            put_head(body, r, true);
        net::Writer cols;
        uint8_t n = 0;
        while (i < r.colonies.size() && n < 255) {
            net::Writer one;
            put_colony(one, r.colonies[i]);
            if (body.size() + cols.size() + one.size() + 8 > kChunkBytes && n > 0)
                break;
            cols.raw(one.data.data(), one.size());
            n++;
            i++;
        }
        body.u8(n);
        body.raw(cols.data.data(), cols.size());
        parts.push_back(std::move(body));
    } while (i < r.colonies.size() && parts.size() < 255);
    for (size_t p = 0; p < parts.size(); p++) {
        net::Writer w;
        w.u32(r.version);
        w.u8((uint8_t)p);
        w.u8((uint8_t)parts.size());
        w.raw(parts[p].data.data(), parts[p].size());
        spaceext::send(K_ROSTER, w);
    }
    g_sent++;
}

static void on_roster(uint8_t origin, net::Reader& rd)
{
    uint32_t version = rd.u32();
    uint8_t part = rd.u8(), parts = rd.u8();
    if (!rd.ok() || !parts || part >= parts)
        return;
    Pending& pd = g_pending[origin];
    if (pd.version != version || pd.parts != parts) {
        pd = Pending{};
        pd.version = version;
        pd.parts = parts;
        pd.r.id = origin;
        pd.r.version = version;
    }
    if (part == 0)
        get_head(rd, pd.r);
    uint8_t n = rd.u8();
    for (uint8_t k = 0; k < n && rd.ok(); k++) {
        Colony c = get_colony(rd);
        if (rd.ok())
            pd.r.colonies.push_back(std::move(c));
    }
    if (!rd.ok()) {
        g_pending.erase(origin);
        return;
    }
    pd.got.insert(part);
    if (pd.got.size() < parts)
        return;
    Roster r = std::move(pd.r);
    g_pending.erase(origin);
    r.updated = H->time_seconds();
    auto old = g_rosters.find(origin);
    bool diff = old == g_rosters.end() || content(old->second) != content(r);
    g_rosters[origin] = std::move(r);
    g_got++;
    if (diff)
        changed(origin);
}

static void roster_tick(double now)
{
    if (!spaceext::connected())
        return;
    // players: drop the rosters of those who left, send mine at once when somebody new is there
    std::set<uint8_t> ids;
    for (auto& p : spaceext::ctx().sess->players())
        ids.insert(p.id);
    for (auto it = g_rosters.begin(); it != g_rosters.end();)
        if (!ids.count(it->first)) {
            uint8_t id = it->first;
            it = g_rosters.erase(it);
            g_pending.erase(id);
            changed(id);
        } else
            ++it;
    for (uint8_t id : ids)
        if (!g_players.count(id) && id != spaceext::my_id())
            g_force_send = true;
    g_players = ids;
    if (!spaceext::in_space())
        return;
    if (!g_requested) { // (re)joined or entered space: ask the others for theirs
        g_requested = true;
        spaceext::send(K_ROSTER_REQ, net::Writer{});
        g_reqs++;
    }
    if (now < g_next_scan && !g_force_send)
        return;
    g_next_scan = now + kScan;
    Roster r;
    r.id = spaceext::my_id();
    if (!read_local(r))
        return;
    std::vector<uint8_t> bytes = content(r);
    bool diff = bytes != g_last_bytes;
    if (diff) {
        g_last_bytes = std::move(bytes);
        g_version++;
    }
    r.version = g_version;
    r.updated = now;
    bool had = g_rosters.count(r.id) != 0;
    g_rosters[r.id] = r;
    if (diff || !had)
        changed(r.id);
    if (diff || g_force_send || now - g_last_send >= kPeriod) {
        g_force_send = false;
        g_last_send = now;
        send_roster(r);
    }
}

static void roster_end()
{
    std::vector<uint8_t> ids;
    for (auto& [id, r] : g_rosters)
        ids.push_back(id);
    g_rosters.clear();
    g_pending.clear();
    g_players.clear();
    g_last_bytes.clear();
    g_requested = false;
    g_force_send = false;
    for (uint8_t id : ids)
        changed(id);
}

// ---------------- colours ----------------
static int color_dist(uint32_t a, uint32_t b)
{
    int d = 0;
    for (int s = 0; s < 24; s += 8)
        d += std::abs((int)((a >> s) & 0xFF) - (int)((b >> s) & 0xFF));
    return d;
}

uint32_t player_color(uint8_t id)
{
    static const uint32_t pal[] = {0xFFFFD040, 0xFF60C8FF, 0xFFFF70D0, 0xFF80FF80, 0xFFFF9040};
    const Roster* r = roster_of(id);
    const Roster* me = roster_of(spaceext::my_id());
    uint32_t c = r ? r->color : 0;
    if (id == spaceext::my_id())
        return c ? c : pal[0];
    if (c && (!me || !me->color || color_dist(c, me->color) > 120))
        return c;
    return pal[1 + id % 4];
}

// ---------------- the feature ----------------
static std::string fmt_roster(const Roster& r)
{
    char b[256];
    snprintf(b, sizeof b, "roster %u (%s) v%u home %u/%08X place %08X at %.2f %.2f %.2f empire '%s' color %08X pol %08X money %d colonies %u age %.1f\n",
             r.id, spaceext::name_of(r.id).c_str(), r.version, r.home_star, r.home_planet, r.place, r.galaxy_pos.x,
             r.galaxy_pos.y, r.galaxy_pos.z, r.empire_name.c_str(), r.color, r.political_id, r.money,
             (unsigned)r.colonies.size(), H->time_seconds() - r.updated);
    std::string s = b;
    for (auto& c : r.colonies) {
        snprintf(b, sizeof b, "  colony %08X star %u '%s' spice %08X '%s' cities %u%s\n", c.planet, c.star,
                 c.name.c_str(), c.spice[0], c.spice_name.c_str(), c.cities, c.home ? " home" : "");
        s += b;
    }
    return s;
}

static std::string status()
{
    char b[160];
    snprintf(b, sizeof b, "map: rosters=%u changes=%u sent=%u got=%u reqs=%u version=%u", (unsigned)g_rosters.size(),
             g_changes, g_sent, g_got, g_reqs, g_version);
    return b;
}

static std::string debug(const std::vector<std::string>& a)
{
    if (a.empty() || a[0] == "status")
        return status() + "\n";
    if (a[0] == "roster") { // coop.space map roster [id]
        std::string s;
        for (uint8_t id : roster_ids())
            if (a.size() < 2 || atoi(a[1].c_str()) == id)
                s += fmt_roster(g_rosters[id]);
        return s.empty() ? "no rosters\n" : s;
    }
    if (a[0] == "send") { // tests: send my roster now
        g_force_send = true;
        return "sending\n";
    }
    return "usage: coop.space map [status | roster [id] | send]\n";
}

static bool init(const SkHost* h, SkMod* m)
{
    H = h;
    M = m;
    a_PlayerEmpire = H->sym("Simulator::GetPlayerEmpire");
    a_StarMgr = H->sym("Simulator::cStarManager::Get");
    a_GetStarRecord = H->sym("Simulator::cStarManager::GetStarRecord");
    a_PlayerUFOGet = H->sym("Simulator::cSimulatorPlayerUFO::Get");
    bool ok = a_PlayerEmpire && a_StarMgr && a_GetStarRecord;
    H->log(M, ok ? SK_INFO : SK_WARN, "space map: roster %s", ok ? "on" : "off (symbols missing)");
    return ok;
}

static void tick(double now) { roster_tick(now); }

static void on_message(uint8_t kind, uint8_t origin, net::Reader& r)
{
    if (kind == K_ROSTER)
        on_roster(origin, r);
    else if (kind == K_ROSTER_REQ)
        g_force_send = true;
}

static bool reg = spaceext::add({.name = "map",
                                 .kind_lo = 10,
                                 .kind_hi = 19,
                                 .init = init,
                                 .tick = tick,
                                 .on_message = on_message,
                                 .on_session_end = roster_end,
                                 .status = status,
                                 .debug = debug});

} // namespace spacemap
