// Co-op contracts (docs/09-coop-design.md, Space 0.7 feature 7; the game side in docs/re/space.md 10):
// jobs made for two or more players, with a reward for each of them. The host offers a contract every
// few minutes (and on demand: `coop.space contracts offer <armada|survey>`); every player sees it on the
// F9 Space page and as a notice, accepts it there, and the host runs it and pays when it is done.
//  * armada: a pirate armada at a star. Its waves come only while the host and another member of the
//    crew are in the same place of that star (the host spawns them: they are host NPC ships, mirrored to
//    the partners by squadron.cpp, so everybody fights the same ships). Destroy every wave in time.
//  * survey: visit N stars near the host's star; the whole crew has to be at each of them together
//    for a few seconds.
// Failure: the time is up, nobody took the offer, or the crew got smaller than needed (a partner left
// the session). A contract never blocks anything: it only adds ships, rows and notices.
// Rewards: money and a tool. Money is one shared budget by default (shared.cpp: the host adds it
// once); in rivals mode every member of the crew gets it. The tool goes to every member of the crew.
//
// MSG_SPACE kinds (space_ext.h): 20 state (host -> all, on a change and every 5 s, so a rejoining
// player sees the current contract), 21 accept (a player -> host), 22 result (host -> all, once).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>

#include "game.h"
#include "space_ext.h"
#include "sporekit_game.hpp"

using skg::cdecl_call;
using skg::thiscall;
using skg::vcall;
using spaceext::tr;

namespace {

enum Kind : uint8_t { K_STATE = 20, K_ACCEPT = 21, K_RESULT = 22 };
enum Type : uint8_t { T_NONE = 0, T_ARMADA = 1, T_SURVEY = 2 };
enum State : uint8_t { S_NONE = 0, S_OFFERED = 1, S_ACTIVE = 2, S_DONE = 3, S_FAILED = 4 };
enum Reason : uint8_t { R_NONE = 0, R_TIMEOUT = 1, R_EXPIRED = 2, R_LEFT = 3, R_CANCELLED = 4 };

const SkHost* H;
SkMod* M;
uintptr_t a_PlayerUFOGet, a_GetAllUFOs, a_SpawnUFO, a_StarMgr, a_GetStarRecord, a_PlayerEmpire, a_AddMoney,
    a_SpaceGame, a_AddItemByKey, a_Context;

template <typename T>
T& F(void* p, uintptr_t off) { return *(T*)((uint8_t*)p + off); }

// cGameDataUFO (docs/re/space.md 0)
constexpr uintptr_t kLoco = 0x34, kPos = 0x38, kRot = 0x44, kMaxHp = 0x534, kState = 0x53C, kHp = 0x540,
                    kType = 0x714;
constexpr int kPirate = 8;            // AmbushPirate: attacks the player anywhere (docs/re/space.md 9.4)
constexpr double kHold = 5.0;         // survey: the crew together at a star this long
constexpr double kOfferTime = 180.0;  // an offer waits this long for its crew
constexpr double kShowEnd = 60.0;     // the result stays on the page this long
constexpr uint8_t kNeeded = 2;

struct Target {
    uint32_t star = 0;
    uint8_t done = 0;
};

struct Contract {
    uint32_t id = 0;
    uint8_t type = T_NONE, state = S_NONE, reason = R_NONE, needed = kNeeded;
    uint32_t money = 0, tool = 0;
    double deadline = 0; // local clock; offered: the offer's end, active: the time limit, done: page end
    std::vector<uint8_t> crew;
    std::vector<Target> targets; // armada: one star
    uint8_t wave = 0, waves = 0, left = 0, up = 0; // armada: current wave, ships of it not destroyed, spawned now
    bool has(uint8_t id) const { return std::find(crew.begin(), crew.end(), id) != crew.end(); }
};

Contract g;                       // the current contract (the host's, a copy on guests)
std::vector<void*> g_ships;       // host: the armada ships spawned now
uint32_t g_ship_place;            // host: the place they were spawned in
uint32_t g_stable_place;          // host: the place the crew check saw last, and since when
double g_stable_since, g_next_wave, g_last_tick, g_last_publish, g_next_offer;
std::vector<double> g_hold;       // host, survey: seconds the crew is together at each target
double g_period = 600;            // auto-offer period (s), 0 = off (coop.space contracts period)
double g_first_delay = 240;       // the first offer after the players meet in space
double g_limit_override;          // tests: the time limit of the next contracts
uint32_t g_rewarded;              // the last contract id this machine was paid for
uint32_t n_offered, n_done, n_failed, n_rewards, n_tools, n_spawned, n_killed;
int g_money_got;
std::mt19937 g_rng{0x5EED};

bool connected() { return spaceext::connected(); }
bool host() { return spaceext::is_host(); }
const spacecoop::Ctx& C() { return spaceext::ctx(); }
double now() { return H->time_seconds(); }

uint32_t star_of(uint32_t place)
{
    if (place & 0x80000000u)
        return place & 0x7FFFFFFFu;
    if (place & 0x40000000u)
        return place & 0xFFFFFFu; // PlanetID: the star in the low 24 bits
    return 0;
}

uint32_t my_place() { return C().my_place ? C().my_place() : 0; }

void* my_ufo()
{
    void* mgr = a_PlayerUFOGet ? cdecl_call<void*>(a_PlayerUFOGet) : nullptr;
    return mgr ? F<void*>(mgr, 0x40) : nullptr;
}

void* star_record(uint32_t id)
{
    void* sm = a_StarMgr ? cdecl_call<void*>(a_StarMgr) : nullptr;
    void* r = sm && a_GetStarRecord && id ? thiscall<void*>(a_GetStarRecord, sm, id) : nullptr;
    return r && !IsBadReadPtr(r, 0xB0) ? r : nullptr;
}

// cStarRecord +0x60 mName (eastl::string16: begin, end), docs/re/space.md 10.1
std::string star_name(uint32_t id)
{
    std::string s;
    if (void* r = star_record(id)) {
        const uint16_t* b = F<const uint16_t*>(r, 0x60);
        const uint16_t* e = F<const uint16_t*>(r, 0x64);
        if (b && e > b && e - b < 64 && !IsBadReadPtr(b, (e - b) * 2))
            for (const uint16_t* p = b; p < e; p++) {
                uint32_t c = *p;
                if (c < 0x80)
                    s += (char)c;
                else if (c < 0x800) {
                    s += (char)(0xC0 | c >> 6);
                    s += (char)(0x80 | (c & 0x3F));
                } else {
                    s += (char)(0xE0 | c >> 12);
                    s += (char)(0x80 | (c >> 6 & 0x3F));
                    s += (char)(0x80 | (c & 0x3F));
                }
            }
    }
    if (s.empty()) {
        char b[24];
        snprintf(b, sizeof b, "%s %06X", tr("star", "звезда"), id);
        s = b;
    }
    return s;
}

std::vector<void*> all_ufos()
{
    std::vector<void*> r;
    void* nm = game::noun_manager();
    void* v = nm && a_GetAllUFOs ? thiscall<void*>(a_GetAllUFOs, nm) : nullptr;
    if (!v)
        return r;
    void** b = F<void**>(v, 0);
    void** e = F<void**>(v, 4);
    for (void** it = b; it && it < e && r.size() < 512; ++it)
        if (*it)
            r.push_back(*it);
    return r;
}

// ---- rewards: money (cEmpire +0xD0) and tools (docs/re/space.md 7.7) ----
void* empire() { return a_PlayerEmpire ? cdecl_call<void*>(a_PlayerEmpire) : nullptr; }
int money()
{
    void* e = empire();
    return e ? F<int>(e, 0xD0) : -1;
}
void add_money(int n)
{
    void* e = empire();
    if (e && a_AddMoney && n)
        thiscall<void>(a_AddMoney, e, n);
}

void* inventory()
{
    void* sg = a_SpaceGame ? *(void**)a_SpaceGame : nullptr;
    return sg ? F<void*>(sg, 0x40) : nullptr;
}

// a tool key {FNV-1 of the tool file name, type, group}: type and group copied from a tool in the
// inventory, as `coop.space tool add` does (docs/re/space.md 9.3)
bool tool_key(uint32_t hash, uint32_t key[3])
{
    void* inv = inventory();
    if (!inv)
        return false;
    int n = vcall<int>(inv, 0x5C / 4);
    for (int i = 0; i < n && i < 256; i++) {
        uint8_t* it = vcall<uint8_t*>(inv, 0x60 / 4, i);
        if (!it || F<int>(it, 0x14) != 2)
            continue;
        const uint32_t* k = vcall<const uint32_t*>(it, 0x18 / 4);
        if (!k)
            continue;
        memcpy(key, k, 12);
        key[0] = hash;
        return true;
    }
    return false;
}

bool has_tool(uint32_t hash)
{
    uint32_t k[3];
    void* inv = inventory();
    return inv && tool_key(hash, k) && vcall<bool>(inv, 0x74 / 4, (const uint32_t*)k);
}

bool give_tool(uint32_t hash)
{
    uint32_t k[3];
    void* inv = inventory();
    if (!inv || !a_AddItemByKey || !tool_key(hash, k) || vcall<bool>(inv, 0x74 / 4, (const uint32_t*)k))
        return false;
    thiscall<void>(a_AddItemByKey, inv, (const uint32_t*)k);
    return vcall<bool>(inv, 0x74 / 4, (const uint32_t*)k);
}

struct ToolInfo {
    uint32_t hash;
    const char* en;
    const char* ru;
};
// tool file names in spacetools~ (0x30608F0B), hashes checked against the game's packages
const ToolInfo kArmadaTools[] = {{0x6DB0189A, "Laser 2", "Лазер 2"},
                                 {0x7B76E38C, "Pulse 2", "Импульс 2"},
                                 {0xFC22027B, "Proton missile 2", "Протонная ракета 2"},
                                 {0x6DB0189B, "Laser 3", "Лазер 3"},
                                 {0x7B76E38D, "Pulse 3", "Импульс 3"},
                                 {0xFC22027A, "Proton missile 3", "Протонная ракета 3"}};
const ToolInfo kSurveyTools[] = {{0x4542380A, "Shield", "Щит"},
                                 {0x44808DE7, "Cloak", "Маскировка"},
                                 {0x931426A0, "Atmosphere generator", "Генератор атмосферы"}};

std::string tool_name(uint32_t hash)
{
    for (auto& t : kArmadaTools)
        if (t.hash == hash)
            return tr(t.en, t.ru);
    for (auto& t : kSurveyTools)
        if (t.hash == hash)
            return tr(t.en, t.ru);
    char b[16];
    snprintf(b, sizeof b, "%08X", hash);
    return b;
}

template <size_t N>
uint32_t first_missing(const ToolInfo (&list)[N])
{
    for (auto& t : list)
        if (!has_tool(t.hash))
            return t.hash;
    return 0;
}

// ---- texts ----
std::string mmss(double secs)
{
    int s = std::max(0, (int)secs);
    char b[16];
    snprintf(b, sizeof b, "%d:%02d", s / 60, s % 60);
    return b;
}

std::string names(const std::vector<uint8_t>& ids)
{
    std::string s;
    for (uint8_t id : ids)
        s += (s.empty() ? "" : ", ") + spaceext::name_of(id);
    return s.empty() ? "-" : s;
}

std::string title(const Contract& c)
{
    if (c.type == T_ARMADA)
        return tr("pirate armada heading for ", "пиратская армада летит к ") +
               star_name(c.targets.empty() ? 0 : c.targets[0].star);
    char b[96];
    snprintf(b, sizeof b, tr("joint survey of %d stars", "совместная разведка %d звёзд"), (int)c.targets.size());
    return b;
}

std::string reward_text(const Contract& c)
{
    std::string s = "$" + std::to_string(c.money);
    if (c.tool)
        s += " + " + tool_name(c.tool);
    return s;
}

std::string reason_text(uint8_t r)
{
    switch (r) {
    case R_TIMEOUT: return tr("the time is up", "время вышло");
    case R_EXPIRED: return tr("nobody took it", "никто не взялся");
    case R_LEFT: return tr("a partner left", "напарник ушёл");
    case R_CANCELLED: return tr("cancelled", "отменён");
    }
    return "?";
}

const char* type_name(uint8_t t) { return t == T_ARMADA ? "armada" : t == T_SURVEY ? "survey" : "none"; }
const char* state_name(uint8_t s)
{
    static const char* n[] = {"none", "offered", "active", "done", "failed"};
    return s <= S_FAILED ? n[s] : "?";
}

// ---- the crew's whereabouts ----
// the StarID of a player (0: the galaxy view, not in space or unknown)
uint32_t star_of_player(uint8_t id)
{
    if (id == spaceext::my_id())
        return spaceext::in_space() ? star_of(my_place()) : 0;
    if (C().partners)
        for (auto& p : C().partners())
            if (p.id == id)
                return star_of(p.place);
    return 0;
}

uint32_t place_of_player(uint8_t id)
{
    if (id == spaceext::my_id())
        return spaceext::in_space() ? my_place() : 0;
    if (C().partners)
        for (auto& p : C().partners())
            if (p.id == id)
                return p.place;
    return 0;
}

// ---- state messages ----
void write_state(net::Writer& w, const Contract& c, double t)
{
    w.u32(c.id);
    w.u8(c.type);
    w.u8(c.state);
    w.u8(c.reason);
    w.u8(c.needed);
    w.u32(c.money);
    w.u32(c.tool);
    w.u32((uint32_t)std::max(0, (int)std::ceil(c.deadline - t)));
    w.u8((uint8_t)c.crew.size());
    for (uint8_t id : c.crew)
        w.u8(id);
    w.u8((uint8_t)c.targets.size());
    for (auto& tg : c.targets) {
        w.u32(tg.star);
        w.u8(tg.done);
    }
    w.u8(c.wave);
    w.u8(c.waves);
    w.u8(c.left);
    w.u8(c.up);
}

bool read_state(net::Reader& r, Contract& c, double t)
{
    c = Contract{};
    c.id = r.u32();
    c.type = r.u8();
    c.state = r.u8();
    c.reason = r.u8();
    c.needed = r.u8();
    c.money = r.u32();
    c.tool = r.u32();
    c.deadline = t + r.u32();
    uint8_t n = r.u8();
    for (int i = 0; i < n && r.ok(); i++)
        c.crew.push_back(r.u8());
    n = r.u8();
    for (int i = 0; i < n && i < 16 && r.ok(); i++) {
        Target tg;
        tg.star = r.u32();
        tg.done = r.u8();
        c.targets.push_back(tg);
    }
    c.wave = r.u8();
    c.waves = r.u8();
    c.left = r.u8();
    c.up = r.u8();
    return r.ok();
}

// the notices of a change (every machine words its own, from the old and the new state)
void announce(const Contract& o, const Contract& c)
{
    if (c.id != o.id && c.state == S_OFFERED) {
        spaceext::notice(tr("New co-op contract: ", "Новый совместный контракт: ") + title(c) + ", " +
                         reward_text(c) + tr(". Accept it on the F9 Space page", ". Принять: F9, страница Космос"));
        return;
    }
    if (c.id != o.id || c.state >= S_DONE)
        return; // the result has its own message
    for (uint8_t id : c.crew)
        if (!o.has(id))
            spaceext::notice(spaceext::name_of(id) + tr(" accepted the contract", " принял(а) контракт"));
    if (c.state == S_ACTIVE && o.state == S_OFFERED)
        spaceext::notice(tr("Contract started: ", "Контракт начался: ") + title(c) + ", " + mmss(c.deadline - now()) +
                         tr(" to do it", " на выполнение"));
    if (c.type == T_ARMADA && c.up && !o.up) {
        char b[160];
        snprintf(b, sizeof b, tr("The armada attacks: wave %d/%d, %d ships", "Армада атакует: волна %d/%d, кораблей: %d"),
                 c.wave, c.waves, c.up);
        spaceext::notice(b);
    }
    if (c.type == T_SURVEY)
        for (size_t i = 0; i < c.targets.size() && i < o.targets.size(); i++)
            if (c.targets[i].done && !o.targets[i].done) {
                int d = 0;
                for (auto& t : c.targets)
                    d += t.done;
                char b[160];
                snprintf(b, sizeof b, tr("%s surveyed together (%d/%d)", "%s разведана вместе (%d/%d)"),
                         star_name(c.targets[i].star).c_str(), d, (int)c.targets.size());
                spaceext::notice(b);
            }
}

// host: the new state goes to everybody (and to this machine's notices)
Contract g_sent;
void publish()
{
    double t = now();
    announce(g_sent, g);
    g_sent = g;
    g_last_publish = t;
    net::Writer w;
    write_state(w, g, t);
    spaceext::send(K_STATE, w);
}

// every machine: the reward of a finished contract, once
void on_result(uint32_t id, bool ok, uint8_t reason, uint32_t money_, uint32_t tool, const std::vector<uint8_t>& crew,
               uint8_t type)
{
    bool mine = std::find(crew.begin(), crew.end(), spaceext::my_id()) != crew.end();
    if (!ok) {
        spaceext::notice(tr("Contract failed: ", "Контракт провален: ") + reason_text(reason));
        return;
    }
    (void)type;
    if (g_rewarded == id)
        return;
    g_rewarded = id;
    // money: one shared budget (the host adds it once, shared.cpp gives it to everybody), in rivals
    // mode every member of the crew gets it
    bool pay = spaceext::rivals() ? mine : host();
    if (pay) {
        add_money((int)money_);
        g_money_got += (int)money_;
    }
    bool got_tool = mine && tool && give_tool(tool);
    if (got_tool)
        n_tools++;
    n_rewards++;
    std::string s = tr("Contract done! Reward: $", "Контракт выполнен! Награда: $") + std::to_string(money_);
    if (!spaceext::rivals())
        s += tr(" to the shared budget", " в общий бюджет");
    if (got_tool)
        s += " + " + tool_name(tool);
    else if (mine && tool)
        s += tr(" (the tool is already in your cargo)", " (инструмент у вас уже есть)");
    if (!mine)
        s = names(crew) + tr(" completed a contract: $", " выполнили контракт: $") + std::to_string(money_);
    spaceext::notice(s);
}

void finish(bool ok, uint8_t reason)
{
    g.state = ok ? S_DONE : S_FAILED;
    g.reason = reason;
    g.up = 0;
    g.deadline = now() + kShowEnd;
    g_ships.clear(); // the ships left over stay as ordinary pirates
    ok ? n_done++ : n_failed++;
    g_next_offer = now() + g_period;
    publish();
    net::Writer w;
    w.u32(g.id);
    w.u8(ok);
    w.u8(reason);
    w.u32(g.money);
    w.u32(g.tool);
    w.u8(g.type);
    w.u8((uint8_t)g.crew.size());
    for (uint8_t id : g.crew)
        w.u8(id);
    spaceext::send(K_RESULT, w);
    on_result(g.id, ok, reason, g.money, g.tool, g.crew, g.type);
}

// ---- host: offers ----
std::vector<uint32_t> near_stars(uint32_t from, size_t n)
{
    std::vector<std::pair<float, uint32_t>> v;
    void* sm = a_StarMgr ? cdecl_call<void*>(a_StarMgr) : nullptr;
    void* fr = star_record(from);
    if (!sm || !fr || IsBadReadPtr(sm, 0xD0))
        return {};
    auto& q0 = F<float[3]>(fr, 0x3C);
    // cStarManager +0xC8: 20-byte sectors, first dword = cStarRecord* array (docs/re/space.md 9.1)
    uint8_t* sb = F<uint8_t*>(sm, 0xC8);
    uint8_t* se = F<uint8_t*>(sm, 0xCC);
    if (!sb || se <= sb || (se - sb) / 20 > 4096 || IsBadReadPtr(sb, se - sb))
        return {};
    for (uint8_t* sec = sb; sec + 20 <= se; sec += 20) {
        void** b = F<void**>(sec, 0);
        void** e = F<void**>(sec, 4);
        if (!b || e <= b || e - b > 4096 || IsBadReadPtr(b, (e - b) * 4))
            continue;
        for (void** it = b; it < e; ++it) {
            void* r = *it;
            if (!r || IsBadReadPtr(r, 0xB0) || r == fr)
                continue;
            int type = F<int>(r, 0x48); // StarType: 4.. are real stars (1 core, 2 black hole, 3 disk)
            if (type < 4 || type > 16 || F<uint8_t>(r, 0xAC) == 0) // + at least one planet
                continue;
            auto& q = F<float[3]>(r, 0x3C);
            float dx = q[0] - q0[0], dy = q[1] - q0[1], dz = q[2] - q0[2];
            v.push_back({dx * dx + dy * dy + dz * dz, F<uint32_t>(r, 0x70)});
        }
    }
    std::sort(v.begin(), v.end());
    if (v.size() > n * 3)
        v.resize(n * 3);
    std::shuffle(v.begin(), v.end(), g_rng);
    std::vector<uint32_t> out;
    for (size_t i = 0; i < v.size() && out.size() < n; i++)
        out.push_back(v[i].second);
    return out;
}

// the armada's star: the star of a colony of the host or of a player now (feature 1's colony roster
// is not merged yet: docs/known-issues.md)
uint32_t armada_star()
{
    std::vector<uint32_t> c;
    if (void* e = empire()) { // cEmpire +0x88: vector<cStarRecord*> (GetStars 0xC30A10 / 0xC30A30)
        void** b = F<void**>(e, 0x88);
        void** en = F<void**>(e, 0x8C);
        if (b && en > b && en - b < 512 && !IsBadReadPtr(b, (en - b) * 4))
            for (void** it = b; it < en; ++it)
                if (*it && !IsBadReadPtr(*it, 0x74))
                    c.push_back(F<uint32_t>(*it, 0x70));
    }
    if (uint32_t s = star_of(my_place()))
        c.push_back(s);
    if (C().partners)
        for (auto& p : C().partners())
            if (uint32_t s = star_of(p.place))
                c.push_back(s);
    if (c.empty())
        return 0;
    return c[std::uniform_int_distribution<size_t>(0, c.size() - 1)(g_rng)];
}

std::string offer(uint8_t type, const std::string& arg)
{
    if (!host())
        return "only the host offers contracts";
    if (!spaceext::in_space())
        return "not in space";
    if (g.state == S_OFFERED || g.state == S_ACTIVE)
        return "a contract is running (coop.space contracts cancel)";
    Contract c;
    c.id = (g_rng() & 0x7FFFFFFF) | 1;
    c.type = type;
    c.state = S_OFFERED;
    c.deadline = now() + kOfferTime;
    if (type == T_ARMADA) {
        uint32_t s = arg == "here" ? star_of(my_place()) : !arg.empty() ? (uint32_t)strtoul(arg.c_str(), nullptr, 0)
                                                                         : armada_star();
        if (!s || !star_record(s))
            return "no star for the armada";
        c.targets.push_back({s, 0});
        c.money = 25000;
        c.tool = first_missing(kArmadaTools);
        c.waves = 2;
    } else {
        uint32_t from = star_of(my_place());
        size_t n = arg.empty() ? 3 : (size_t)std::clamp(atoi(arg.c_str()), 1, 6);
        for (uint32_t s : near_stars(from, n))
            c.targets.push_back({s, 0});
        if (c.targets.empty())
            return "no stars near the host (in the galaxy view?)";
        c.money = 15000;
        c.tool = first_missing(kSurveyTools);
    }
    if (!c.tool)
        c.money *= 2; // every tool of the list is in the cargo already
    g = c;
    g_ships.clear();
    g_hold.assign(g.targets.size(), 0);
    n_offered++;
    publish();
    return std::string("offered ") + type_name(type) + " id " + std::to_string(g.id);
}

double limit_of(uint8_t type)
{
    if (g_limit_override > 0)
        return g_limit_override;
    return type == T_ARMADA ? 600 : 900;
}

void accept(uint8_t who)
{
    if (!host() || (g.state != S_OFFERED && g.state != S_ACTIVE) || g.has(who))
        return;
    g.crew.push_back(who);
    if (g.state == S_OFFERED && g.crew.size() >= g.needed) {
        g.state = S_ACTIVE;
        g.deadline = now() + limit_of(g.type);
        if (g.type == T_ARMADA) {
            g.wave = 1;
            g.left = (uint8_t)(g.crew.size() + 1);
        }
    }
    publish();
}

// the button / `coop.space contracts accept`
std::string accept_mine()
{
    if (g.state != S_OFFERED && g.state != S_ACTIVE)
        return "no contract to accept";
    if (g.has(spaceext::my_id()))
        return "already in the crew";
    if (host()) {
        accept(spaceext::my_id());
        return "accepted";
    }
    net::Writer w;
    w.u32(g.id);
    spaceext::send(K_ACCEPT, w);
    return "accept sent";
}

std::string jump_to(uint32_t star)
{
    char b[24];
    snprintf(b, sizeof b, "0x%X", star);
    std::string r = spacecoop::debug({"jumpto", b}); // the native travel of spacecoop.cpp (docs/re/space.md 7.3)
    if (r != "jumping")
        spaceext::notice(tr("Can't jump: ", "Прыжок невозможен: ") + r);
    return r;
}

// ---- host: the armada ----
int crew_with_me()
{
    uint32_t here = my_place();
    int n = 0;
    for (uint8_t id : g.crew)
        if (place_of_player(id) == here)
            n++;
    return n;
}

void spawn_wave(int n, double t)
{
    void* me = my_ufo();
    if (!me || !a_SpawnUFO)
        return;
    auto& p = F<float[3]>(me, kPos);
    auto& q = F<float[4]>(me, kRot);
    const float r = 60.f; // around the host's ship
    for (int i = 0; i < n; i++) {
        uint32_t pol = 0xFFFFFFFFu; // no empire: pirates
        // model null: the game's default UFO model (SpawnUFO 0x102ACC0, docs/re/space.md 10.2)
        void* u = cdecl_call<void*>(a_SpawnUFO, kPirate, &pol, (void*)nullptr);
        if (!u)
            continue;
        float a = 6.2831853f * (float)i / (float)n;
        float pp[3] = {p[0] + r * std::cos(a), p[1] + 15.f, p[2] + r * std::sin(a)};
        vcall<void>((uint8_t*)u + kLoco, 0x44 / 4, (const float*)pp, (const float*)q); // Teleport
        // an armada ship is tougher than a lone pirate: one ship can't take a wave alone
        F<float>(u, kMaxHp) *= 1.5f;
        F<float>(u, kHp) = F<float>(u, kMaxHp);
        g_ships.push_back(u);
        n_spawned++;
    }
    g_ship_place = my_place();
    (void)t;
}

void armada_tick(double t)
{
    if (g.targets.empty())
        return;
    uint32_t place = spaceext::in_space() ? my_place() : 0;
    bool changed = false;
    if (!g_ships.empty()) {
        if (place != g_ship_place) {
            // the game frees the NPC ships of a place on every context switch (docs/re/space.md 9.4):
            // the rest of the wave comes back when the crew is there together again
            g_ships.clear();
        } else {
            auto list = all_ufos();
            for (auto it = g_ships.begin(); it != g_ships.end();) {
                void* u = *it;
                bool listed = std::find(list.begin(), list.end(), u) != list.end();
                bool dead = !listed || !game::object_alive(u) || F<int>(u, kState) == 2 || F<float>(u, kHp) <= 0 ||
                            F<int>(u, kType) != kPirate;
                if (dead) {
                    it = g_ships.erase(it);
                    if (g.left)
                        g.left--;
                    n_killed++;
                    changed = true;
                } else
                    ++it;
            }
        }
        if (g.up != g_ships.size()) {
            g.up = (uint8_t)g_ships.size();
            changed = true;
        }
    }
    if (!g.left && g_ships.empty()) {
        if (g.wave >= g.waves) {
            finish(true, R_NONE);
            return;
        }
        g.wave++;
        g.left = (uint8_t)(g.crew.size() + g.wave);
        g_next_wave = t + 8;
        changed = true;
    }
    // a wave comes when the host and another member of the crew are in one place of the star
    int ctx = a_Context ? cdecl_call<int>(a_Context) : -1;
    void* me = my_ufo();
    bool ready = g_ships.empty() && g.left && star_of(place) == g.targets[0].star && (ctx == 0 || ctx == 1) && me &&
                 F<int>(me, kState) != 2 && crew_with_me() >= g.needed && t >= g_next_wave;
    if (!ready || place != g_stable_place) {
        if (place != g_stable_place) {
            g_stable_place = place;
            g_stable_since = t;
        }
    }
    if (ready && t - g_stable_since >= 3) {
        spawn_wave(g.left, t);
        g.up = (uint8_t)g_ships.size();
        changed = true;
    }
    if (changed)
        publish();
}

// ---- host: the survey ----
void survey_tick(double dt)
{
    bool changed = false;
    if (g_hold.size() != g.targets.size())
        g_hold.assign(g.targets.size(), 0);
    for (size_t i = 0; i < g.targets.size(); i++) {
        auto& tg = g.targets[i];
        if (tg.done)
            continue;
        bool all = !g.crew.empty();
        for (uint8_t id : g.crew)
            if (star_of_player(id) != tg.star)
                all = false;
        g_hold[i] = all ? g_hold[i] + dt : 0;
        if (g_hold[i] >= kHold) {
            tg.done = 1;
            changed = true;
        }
    }
    bool done = true;
    for (auto& tg : g.targets)
        done = done && tg.done;
    if (done) {
        finish(true, R_NONE);
        return;
    }
    if (changed)
        publish();
}

void host_tick(double t)
{
    double dt = std::min(1.0, t - g_last_tick);
    g_last_tick = t;
    // the crew: players still in the session
    if (g.state == S_OFFERED || g.state == S_ACTIVE) {
        auto pl = C().sess->players();
        size_t before = g.crew.size();
        g.crew.erase(std::remove_if(g.crew.begin(), g.crew.end(),
                                    [&](uint8_t id) {
                                        return std::none_of(pl.begin(), pl.end(), [&](auto& p) { return p.id == id; });
                                    }),
                     g.crew.end());
        if (g.crew.size() != before) {
            if (g.state == S_ACTIVE && g.crew.size() < g.needed) {
                finish(false, R_LEFT);
                return;
            }
            publish();
        }
    }
    if (g.state == S_OFFERED && t > g.deadline) {
        finish(false, R_EXPIRED);
        return;
    }
    if (g.state == S_ACTIVE && t > g.deadline) {
        finish(false, R_TIMEOUT);
        return;
    }
    if (g.state == S_ACTIVE && g.type == T_ARMADA)
        armada_tick(t);
    else if (g.state == S_ACTIVE && g.type == T_SURVEY)
        survey_tick(dt);
    if ((g.state == S_DONE || g.state == S_FAILED) && t > g.deadline) {
        g = Contract{};
        publish();
    }
    // a new offer every few minutes while the players are in space together
    bool together = spaceext::in_space() && C().partners && !C().partners().empty();
    if (!together || g_period <= 0)
        g_next_offer = 0;
    else if (!g_next_offer)
        g_next_offer = t + std::min(g_first_delay, g_period);
    else if (g.state == S_NONE && t > g_next_offer) {
        std::string r = offer(std::uniform_int_distribution<int>(0, 1)(g_rng) ? T_ARMADA : T_SURVEY, "");
        H->log(M, SK_INFO, "contracts: auto offer: %s", r.c_str());
        g_next_offer = t + g_period;
    }
    if (g.state != S_NONE && t - g_last_publish > 5)
        publish(); // rejoining players get the contract within 5 s
}

// ---- registry callbacks ----
bool init(const SkHost* h, SkMod* m)
{
    H = h;
    M = m;
    auto S = [](const char* n) { return H->sym(n); };
    a_PlayerUFOGet = S("Simulator::cSimulatorPlayerUFO::Get");
    a_GetAllUFOs = S("Simulator::GetAllUFOs");
    a_SpawnUFO = S("Simulator::SpawnUFO");
    a_StarMgr = S("Simulator::cStarManager::Get");
    a_GetStarRecord = S("Simulator::cStarManager::GetStarRecord");
    a_PlayerEmpire = S("Simulator::GetPlayerEmpire");
    a_AddMoney = S("Simulator::cEmpire::AddMoney");
    a_SpaceGame = S("Simulator::cSimulatorSpaceGame::ptr");
    a_AddItemByKey = S("Simulator::cPlayerInventory::AddItemByKey");
    a_Context = S("SporeCoop::Space_GetCurrentContext");
    g_rng.seed((uint32_t)(h->time_seconds() * 1000) ^ 0xC0B0u);
    return a_PlayerUFOGet && a_GetAllUFOs && a_SpawnUFO && a_StarMgr && a_GetStarRecord && a_PlayerEmpire &&
           a_AddMoney && a_SpaceGame && a_AddItemByKey && a_Context;
}

void tick(double t)
{
    if (!connected()) {
        if (g.state != S_NONE)
            g = Contract{};
        return;
    }
    if (host()) {
        static double last;
        if (t - last < 0.25)
            return;
        last = t;
        host_tick(t);
    } else if ((g.state == S_DONE || g.state == S_FAILED) && t > g.deadline + 10)
        g = Contract{}; // the host's "none" got lost
}

void on_message(uint8_t kind, uint8_t origin, net::Reader& r)
{
    double t = now();
    if (kind == K_STATE && !host()) {
        Contract c;
        if (!read_state(r, c, t))
            return;
        announce(g, c);
        g = c;
    } else if (kind == K_ACCEPT && host()) {
        uint32_t id = r.u32();
        if (r.ok() && id == g.id)
            accept(origin);
    } else if (kind == K_RESULT && !host()) {
        uint32_t id = r.u32();
        uint8_t ok = r.u8(), reason = r.u8();
        uint32_t mo = r.u32(), tool = r.u32();
        uint8_t type = r.u8(), n = r.u8();
        std::vector<uint8_t> crew;
        for (int i = 0; i < n && r.ok(); i++)
            crew.push_back(r.u8());
        if (r.ok())
            on_result(id, ok, reason, mo, tool, crew, type);
    }
}

void on_session_end()
{
    g = Contract{};
    g_sent = Contract{};
    g_ships.clear();
    g_next_offer = 0;
}

std::vector<spaceext::Row> rows()
{
    std::vector<spaceext::Row> v;
    if (!spaceext::in_space() || !connected() || g.state == S_NONE)
        return v;
    double t = now();
    uint8_t me = spaceext::my_id();
    bool in = g.has(me);
    uint32_t here = star_of(my_place());
    auto jump_row = [&](spaceext::Row& r, uint32_t star) {
        if (star && star != here) {
            r.button = tr("Jump there", "Прыгнуть");
            r.on_click = [star] { jump_to(star); };
        }
    };
    auto accept_row = [&](spaceext::Row& r) {
        if (!in && (g.state == S_OFFERED || g.state == S_ACTIVE)) {
            r.button = tr("Accept", "Принять");
            r.on_click = [] { accept_mine(); };
        }
    };
    char b[200];
    if (g.state == S_DONE || g.state == S_FAILED) {
        spaceext::Row r;
        r.text = g.state == S_DONE ? tr("Contract done: ", "Контракт выполнен: ") + reward_text(g)
                                   : tr("Contract failed: ", "Контракт провален: ") + reason_text(g.reason);
        r.color = g.state == S_DONE ? 0xFF60FF60 : 0xFFFF6040;
        v.push_back(std::move(r));
        return v;
    }
    if (g.state == S_OFFERED) {
        spaceext::Row r1, r2;
        snprintf(b, sizeof b, tr(": %d ships needed", ": нужно кораблей: %d"), (int)g.needed);
        r1.text = tr("Contract: ", "Контракт: ") + title(g) + b;
        r1.color = 0xFFFFD040;
        accept_row(r1);
        if (in) {
            snprintf(b, sizeof b, tr(" (accepted, %d more)", " (принят, ещё %d)"), (int)(g.needed - g.crew.size()));
            r1.text += b;
        }
        r2.text = tr("Reward each: ", "Награда каждому: ") + reward_text(g) + ", " + mmss(g.deadline - t) +
                  tr(" to accept", " на ответ");
        if (g.type == T_ARMADA)
            jump_row(r2, g.targets[0].star);
        v.push_back(std::move(r1));
        v.push_back(std::move(r2));
        return v;
    }
    // active
    spaceext::Row r1;
    r1.color = 0xFFFFD040;
    if (g.type == T_ARMADA) {
        uint32_t s = g.targets[0].star;
        if (g.up)
            snprintf(b, sizeof b, tr(": wave %d/%d, %d ships, ", ": волна %d/%d, кораблей %d, "), g.wave, g.waves, g.left);
        else
            snprintf(b, sizeof b, tr(": wave %d/%d waits for %d ships, ", ": волна %d/%d ждёт %d корабля, "), g.wave,
                     g.waves, (int)g.needed);
        r1.text = tr("Armada at ", "Армада у ") + star_name(s) + b + mmss(g.deadline - t);
        jump_row(r1, s);
        v.push_back(std::move(r1));
    } else {
        int d = 0;
        for (auto& tg : g.targets)
            d += tg.done;
        snprintf(b, sizeof b, tr("Survey: %d/%d stars, ", "Разведка: %d/%d звёзд, "), d, (int)g.targets.size());
        r1.text = b + mmss(g.deadline - t);
        v.push_back(std::move(r1));
        for (auto& tg : g.targets) {
            spaceext::Row r;
            if (tg.done) {
                r.text = "  " + star_name(tg.star) + tr(": surveyed", ": разведана");
                r.color = 0xFF60FF60;
            } else {
                int n = 0;
                for (uint8_t id : g.crew)
                    n += star_of_player(id) == tg.star;
                snprintf(b, sizeof b, tr(": crew here %d/%d", ": экипаж здесь %d/%d"), n, (int)g.crew.size());
                r.text = "  " + star_name(tg.star) + b;
                jump_row(r, tg.star);
            }
            v.push_back(std::move(r));
        }
    }
    spaceext::Row r2;
    r2.text = tr("Crew: ", "Экипаж: ") + names(g.crew) + tr(", reward ", ", награда ") + reward_text(g);
    accept_row(r2);
    v.push_back(std::move(r2));
    return v;
}

void draw_overlay()
{
    if (g.state != S_ACTIVE || !g.has(spaceext::my_id()) || !spaceext::in_space())
        return;
    int sw = 0, sh = 0;
    H->screen_size(&sw, &sh);
    char b[200];
    double left = g.deadline - now();
    if (g.type == T_ARMADA)
        snprintf(b, sizeof b, tr("CONTRACT  armada wave %d/%d  ships %d  %s", "КОНТРАКТ  армада, волна %d/%d  кораблей %d  %s"),
                 g.wave, g.waves, g.left, mmss(left).c_str());
    else {
        int d = 0;
        for (auto& tg : g.targets)
            d += tg.done;
        snprintf(b, sizeof b, tr("CONTRACT  survey %d/%d stars  %s", "КОНТРАКТ  разведка %d/%d звёзд  %s"), d,
                 (int)g.targets.size(), mmss(left).c_str());
    }
    H->overlay_text(sw / 2 - (int)strlen(b) * 9 / 2, 70, left < 60 ? 0xFFFF8040 : 0xFFFFD040, b);
}

std::string status()
{
    char b[400];
    std::string crew;
    for (uint8_t id : g.crew)
        crew += (crew.empty() ? "" : ",") + std::to_string(id);
    int d = 0;
    for (auto& tg : g.targets)
        d += tg.done;
    snprintf(b, sizeof b,
             "contracts: id=%u type=%s state=%s reason=%u crew=%s needed=%u secs=%d star=%u wave=%u/%u ships_left=%u "
             "ships_up=%u targets=%d/%d money=%u tool=%08X offered=%u done=%u failed=%u rewards=%u tools_got=%u "
             "money_got=%d spawned=%u killed=%u period=%.0f\n",
             g.id, type_name(g.type), state_name(g.state), g.reason, crew.empty() ? "-" : crew.c_str(), g.needed,
             g.state ? (int)std::ceil(g.deadline - now()) : 0, g.targets.empty() ? 0u : g.targets[0].star, g.wave,
             g.waves, g.left, g.up, d, (int)g.targets.size(), g.money, g.tool, n_offered, n_done, n_failed, n_rewards,
             n_tools, g_money_got, n_spawned, n_killed, g_period);
    return b;
}

// coop.space contracts [offer <armada|survey> [here|<star>|<n>] | accept | cancel | period <s> |
//                       limit <s> | ships | targets | money | tool <hash>]
std::string debug(const std::vector<std::string>& a)
{
    if (a.empty())
        return status();
    char b[200];
    if (a[0] == "offer" && a.size() > 1)
        return offer(a[1] == "armada" ? T_ARMADA : T_SURVEY, a.size() > 2 ? a[2] : "") + "\n";
    if (a[0] == "accept")
        return accept_mine() + "\n";
    if (a[0] == "cancel") {
        if (!host() || (g.state != S_OFFERED && g.state != S_ACTIVE))
            return "nothing to cancel (host only)\n";
        finish(false, R_CANCELLED);
        return "cancelled\n";
    }
    if (a[0] == "period" && a.size() > 1) {
        g_period = atof(a[1].c_str());
        g_next_offer = 0;
        snprintf(b, sizeof b, "auto offers every %.0f s (0 = off)\n", g_period);
        return b;
    }
    if (a[0] == "limit" && a.size() > 1) {
        g_limit_override = atof(a[1].c_str());
        snprintf(b, sizeof b, "time limit of new contracts %.0f s (0 = the default)\n", g_limit_override);
        return b;
    }
    if (a[0] == "ships") { // host: the armada ships spawned now (the pointers coop.fleet list prints)
        std::string s;
        for (void* u : g_ships) {
            snprintf(b, sizeof b, "armada %p\n", u);
            s += b;
        }
        return s.empty() ? "no armada ships\n" : s;
    }
    if (a[0] == "targets") {
        std::string s;
        for (auto& tg : g.targets) {
            snprintf(b, sizeof b, "target %u 0x%X %s done=%d\n", tg.star, tg.star, star_name(tg.star).c_str(), tg.done);
            s += b;
        }
        return s.empty() ? "no targets\n" : s;
    }
    if (a[0] == "money") {
        snprintf(b, sizeof b, "money %d\n", money());
        return b;
    }
    if (a[0] == "tool" && a.size() > 1) {
        uint32_t h = (uint32_t)strtoul(a[1].c_str(), nullptr, 16);
        snprintf(b, sizeof b, "tool %08X %s: %s\n", h, tool_name(h).c_str(), has_tool(h) ? "have" : "no");
        return b;
    }
    if (a[0] == "star" && a.size() > 1) {
        uint32_t s = (uint32_t)strtoul(a[1].c_str(), nullptr, 0);
        snprintf(b, sizeof b, "star %u: %s\n", s, star_name(s).c_str());
        return b;
    }
    return "usage: coop.space contracts [offer <armada|survey> [here|<star>|<n>] | accept | cancel | period <s> | "
           "limit <s> | ships | targets | money | tool <hex> | star <id>]\n";
}

bool reg = spaceext::add({.name = "contracts",
                          .kind_lo = 20,
                          .kind_hi = 29,
                          .init = init,
                          .tick = tick,
                          .on_message = on_message,
                          .on_session_end = on_session_end,
                          .draw_overlay = draw_overlay,
                          .rows = rows,
                          .status = status,
                          .debug = debug});

} // namespace
