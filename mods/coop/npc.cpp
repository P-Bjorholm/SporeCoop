#include "npc.h"
#include "modes.h"
#include "tribe.h"
#include "teamplay.h"

#include <algorithm>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "creations.h"
#include "driver.h"
#include "game.h"
#include "sporekit_game.hpp"

namespace npc {

static const SkHost* H;
static SkMod* M [[maybe_unused]];
static Ctx C;
static bool g_enabled = true;

static constexpr float kRelevance = 110.f;  // m around a guest
static constexpr float kHysteresis = 20.f;  // keep until kRelevance + this
static constexpr double kStateInterval = 0.1;
static constexpr float kRtsRelevance = 250.f; // tribe stage: m around the player tribe

// NPC creatures are shared in the creature stage and, around the player tribe, in the tribe stage
static bool npc_stage() { return game::in_creature_stage() || game::mode_id() == 0x1654C02; }
static bool rts_stage() { return game::mode_id() == 0x1654C02; }

// ------------------------------------------------------------------ host side
struct HostNpc {
    uint16_t id;
    double last_seen;
};
static std::unordered_map<void*, HostNpc> g_host_npcs;  // creature -> id
static std::unordered_map<uint16_t, void*> g_host_by_id;
static std::map<uint8_t, std::set<uint16_t>> g_known;  // guest -> net ids it mirrors
static uint16_t g_next_id = 1;
static double g_last_state = 0;
static double g_frozen_since = 0;                       // host: no creature updates since then
static std::unordered_set<void*> g_seen_now;            // creatures alive this frame

// ------------------------------------------------------------------ guest side
struct Proxy {
    void* c = nullptr;
    game::Key key{};
    uint8_t flags = 0;
    float health = 0, max_health = 0;
    bool killed = false;
    int fail = 0; // 1 species, 2 herd, 3 create
    double last_rx = 0; // local time of its last spawn / state from the host
    net::SnapshotBuffer buf;
    drive::State drv;
};
static std::map<uint16_t, Proxy> g_proxies;
static std::unordered_map<void*, uint16_t> g_proxy_of;
static std::map<uint64_t, void*> g_herds; // species instance/group -> herd for proxies
// guest: the game's own herd of each species (seen on the local NPCs the update filter removes).
// Proxies go into it: Create then gives them the herd's archetype tuning (+0xE84) like any NPC of
// the nest (social games, epic flag, DNA worth need it; docs/re/creature.md 8), and the species
// still has its creatures where the game counts them
static std::map<uint64_t, void*> g_local_herds;
static uint32_t g_proxies_in_nest = 0;
static uint64_t herd_key(const game::Key& k) { return ((uint64_t)k.group << 32) | k.instance; }
static std::vector<void*> g_kill;          // local NPCs to remove (guest)
static uint32_t g_proxy_lost = 0;
static double g_last_states_rx = -100; // guest: the last NPC state batch from the host
static uint32_t g_stale_dropped = 0;   // guest: proxies the host stopped talking about (ghosts)
static uint32_t g_removed_local = 0, g_hits_sent = 0, g_hits_applied = 0, g_damage_in = 0, g_kills_credited = 0;

static bool g_host_away = false; // guest: the host is in the main menu, our own game runs the NPCs
static uint32_t g_away_proxies = 0;
static uint32_t g_local_alive = 0; // guest: own NPCs that ran Update last frame (host away: the game's)
static bool is_host() { return C.sess && C.sess->role() == net::Role::Host; }
static bool is_guest() { return C.sess && C.sess->role() == net::Role::Client && C.sess->connected(); }

bool is_proxy(void* c) { return g_proxy_of.count(c) != 0; }
void set_enabled(bool on) { g_enabled = on; }
bool enabled() { return g_enabled; }

// called from inside the game's creature update loop: keep it cheap
static bool update_filter(void* c)
{
    if (!g_enabled || !is_guest() || !npc_stage())
        return true;
    if (game::is_puppet(c) || c == game::avatar())
        return true;
    if (g_host_away) // the host left the world: the guest plays on alone with its own NPCs
        return true;
    if (void* h = game::herd_of(c))
        if (h != game::avatar_herd())
            g_local_herds[herd_key(game::species_key(c))] = h;
    g_kill.push_back(c); // a local NPC: the host owns NPCs
    return false;
}

void init(const SkHost* host, SkMod* mod, const Ctx& ctx)
{
    H = host;
    M = mod;
    C = ctx;
    game::set_update_filter(update_filter);
}

static net::NpcState host_state(uint16_t id, void* c)
{
    net::NpcState s;
    s.net_id = id;
    s.pos = game::position(c);
    s.rot = game::orientation(c);
    s.vel = game::velocity(c);
    s.health = game::health(c);
    s.flags = (game::is_dead(c) ? net::NF_DEAD : 0) | (game::impressed(c) ? net::NF_IMPRESSED : 0);
    return s;
}

static void send_spawn(uint8_t guest, uint16_t id, void* c)
{
    net::NpcSpawn sp;
    sp.net_id = id;
    game::Key k = game::species_key(c);
    sp.key[0] = k.instance;
    sp.key[1] = k.type;
    sp.key[2] = k.group;
    sp.scale = game::scale(c);
    void* av = game::avatar();
    sp.flags = (game::is_dead(c) ? net::NF_DEAD : 0) |
               (av && game::herd_of(c) && game::herd_of(c) == game::herd_of(av) ? net::NF_FAMILY : 0);
    sp.pos = game::position(c);
    sp.rot = game::orientation(c);
    sp.health = game::health(c);
    sp.max_health = game::max_health(c);
    net::Writer w;
    sp.write(w);
    C.sess->send_reliable_to(guest, net::MSG_NPC_SPAWN, w);
}

static void send_despawn(uint8_t guest, uint16_t id)
{
    net::Writer w;
    w.u16(id);
    w.u8(0);
    C.sess->send_reliable_to(guest, net::MSG_NPC_DESPAWN, w);
}

static void host_tick(double now, const std::vector<void*>& creatures)
{
    void* av = game::avatar();
    net::Vec3 center;
    bool rts = rts_stage() && C.stage_center && C.stage_center(center);
    if (!av && !rts)
        return;
    // no creature updated this frame: the simulation stands still (pause menu, the death caption
    // waiting for OK, a cinematic). Not a reason to forget them: the guests lost every NPC after 2 s
    // and got them again when the host went on (play_creature.py 2026-10-23)
    if (creatures.empty()) {
        g_frozen_since = g_frozen_since > 0 ? g_frozen_since : now;
        return;
    }
    if (g_frozen_since > 0) { // the frozen time does not count as unseen
        double gap = now - g_frozen_since;
        for (auto& [c, hn] : g_host_npcs)
            hn.last_seen += gap;
        g_frozen_since = 0;
    }
    g_seen_now.clear();
    for (void* c : creatures) {
        if (c == av || game::is_puppet(c) || game::is_destroyed(c))
            continue;
        g_seen_now.insert(c);
        auto it = g_host_npcs.find(c);
        if (it == g_host_npcs.end()) {
            uint16_t id = g_next_id++;
            if (g_next_id == 0xFFFF)
                g_next_id = 1;
            g_host_npcs[c] = {id, now};
            g_host_by_id[id] = c;
        } else
            it->second.last_seen = now;
    }
    // forget creatures that stopped updating (destroyed / unloaded)
    for (auto it = g_host_npcs.begin(); it != g_host_npcs.end();) {
        if (now - it->second.last_seen > 2.0) {
            uint16_t id = it->second.id;
            for (auto& [g, ks] : g_known)
                if (ks.erase(id))
                    send_despawn(g, id);
            g_host_by_id.erase(id);
            it = g_host_npcs.erase(it);
        } else
            ++it;
    }
    bool send_states = now - g_last_state >= kStateInterval;
    if (send_states)
        g_last_state = now;
    for (auto& p : C.sess->players()) {
        if (p.id == 0)
            continue;
        auto& known = g_known[p.id];
        net::Vec3 gp;
        float relevance = kRelevance;
        if (rts) {
            gp = center;
            relevance = kRtsRelevance;
        } else if (!C.player_pos(p.id, gp)) {
            for (uint16_t id : known)
                send_despawn(p.id, id);
            known.clear();
            continue;
        }
        std::vector<net::NpcState> batch;
        for (void* c : g_seen_now) {
            uint16_t id = g_host_npcs[c].id;
            float d = net::dist(game::position(c), gp);
            bool k = known.count(id) != 0;
            if (!k && d < relevance) {
                send_spawn(p.id, id, c);
                known.insert(id);
                k = true;
            } else if (k && d > relevance + kHysteresis) {
                send_despawn(p.id, id);
                known.erase(id);
                k = false;
            }
            if (k && send_states)
                batch.push_back(host_state(id, c));
        }
        // unreliable batches that fit in one packet
        size_t per = (net::kMaxPacket - 64) / net::NpcState::kSize;
        for (size_t i = 0; i < batch.size(); i += per) {
            net::Writer w;
            w.u32(net::now_ms());
            size_t n = std::min(per, batch.size() - i);
            w.u8((uint8_t)n);
            for (size_t j = 0; j < n; j++)
                batch[i + j].write(w);
            C.sess->send_unreliable_to(p.id, net::MSG_NPC_STATE, w);
        }
    }
    for (auto it = g_known.begin(); it != g_known.end();) { // guests that left
        bool present = false;
        for (auto& p : C.sess->players())
            if (p.id == it->first)
                present = true;
        it = present ? std::next(it) : g_known.erase(it);
    }
}

static void* proxy_herd(const game::Key& k, uint8_t flags, const net::Vec3& pos, void* species)
{
    if (flags & net::NF_FAMILY)
        if (void* h = game::avatar_herd())
            return h;
    uint64_t hk = herd_key(k);
    auto lh = g_local_herds.find(hk);
    if (lh != g_local_herds.end()) {
        if (game::object_alive(lh->second)) {
            g_proxies_in_nest++;
            return lh->second;
        }
        g_local_herds.erase(lh);
    }
    // not seen on a local NPC yet (proxies made before the game's NPCs ran an update): the nest
    // herd of the species among the game objects
    struct Find {
        void* species;
        void* found;
    } f{species, nullptr};
    game::for_each_object(
        [](void* o, uint32_t noun, void* u) {
            auto* f = (Find*)u;
            if (noun != game::kHerdNoun || !game::object_alive(o) || game::herd_species(o) != f->species ||
                !game::herd_nest(o))
                return true;
            f->found = o;
            return false;
        },
        &f);
    if (f.found) {
        g_local_herds[hk] = f.found;
        g_proxies_in_nest++;
        return f.found;
    }
    auto it = g_herds.find(hk);
    if (it != g_herds.end())
        return it->second;
    void* h = game::create_herd(pos, species);
    if (h)
        g_herds[hk] = h;
    return h;
}

static void destroy_proxy(uint16_t id)
{
    auto it = g_proxies.find(id);
    if (it == g_proxies.end())
        return;
    if (it->second.c) {
        g_proxy_of.erase(it->second.c);
        if (game::is_destroyed(it->second.c)) // the game removed it already: only forget it
            game::puppet_unregister(it->second.c);
        else
            game::destroy_creature(it->second.c);
    }
    g_proxies.erase(it);
}

static void guest_tick(double now)
{
    void* av = game::avatar();
    // remove local NPCs collected by the update filter
    std::sort(g_kill.begin(), g_kill.end());
    g_kill.erase(std::unique(g_kill.begin(), g_kill.end()), g_kill.end());
    for (void* c : g_kill)
        if (c != av && !game::is_puppet(c) && !game::is_destroyed(c)) {
            game::destroy_creature(c);
            g_removed_local++;
        }
    g_kill.clear();
    if (!av && !rts_stage()) // the tribe stage has no avatar
        return;
    // the first frames of a world: the update filter meets the game's own NPCs and learns their
    // herds (g_local_herds) before the proxies are made
    static void* s_av = nullptr;
    static double s_av_t0 = 0;
    if (av != s_av) {
        s_av = av;
        s_av_t0 = now;
    }
    if (now - s_av_t0 < 1.0)
        return;
    // A proxy the host stopped sending states for while it sends them for the others is not an
    // NPC of the host any more (its id belongs to a world the host has left: before 2026-10-25 the
    // host renumbered its NPCs after its editor or a load without a despawn, and the guest kept
    // the old ones for good: a second, living copy of every NPC, a kill of the host's left it
    // standing). A host that stands still (pause menu) sends no states at all: nothing is dropped.
    if (now - g_last_states_rx < 0.5) {
        std::vector<uint16_t> stale;
        for (auto& [id, px] : g_proxies)
            if (now - px.last_rx > 5.0)
                stale.push_back(id);
        for (uint16_t id : stale) {
            destroy_proxy(id);
            g_stale_dropped++;
        }
    }
    for (auto& [id, px] : g_proxies) {
        net::PlayerState s;
        if (!px.buf.sample(now, s))
            continue;
        // the guest's own game destroyed the proxy (its tribe killed it, a cleanup): forget the
        // object at once (it is freed later; it was driven every frame after that) and make a new
        // one unless the host says the NPC is dead. NPCs vanished from guests this way in a 4-player
        // tribe run (coverage 26-48 % for 4 minutes).
        if (px.c && game::is_destroyed(px.c)) {
            g_proxy_of.erase(px.c);
            game::puppet_unregister(px.c);
            px.c = nullptr;
            px.drv = drive::State{};
            g_proxy_lost++;
        }
        if (!px.c && (s.flags & net::NF_DEAD))
            continue;
        if (!px.c) {
            void* species = game::species_by_key(px.key);
            void* herd = species ? proxy_herd(px.key, px.flags, s.pos, species) : nullptr;
            int fail = !species ? 1 : !herd ? 2 : 0;
            if (!fail) {
                px.c = game::spawn_creature(s.pos, species, herd);
                if (!px.c)
                    fail = 3;
            }
            if (fail) {
                // a species of the host's world this game lacks (the host's own creations and its
                // Sporepedia downloads populate its planet): the host sends it, the next try works
                if (fail == 1)
                    creations::want_species({px.key.instance, px.key.type, px.key.group});
                if (px.fail != fail)
                    H->log(M, SK_WARN, "npc %u: proxy spawn failed (%s) key=%08X/%08X/%08X", id,
                           fail == 1 ? "unknown species" : fail == 2 ? "no herd" : "Create failed", px.key.instance,
                           px.key.type, px.key.group);
                px.fail = fail;
                continue;
            }
            game::puppet_register(px.c); // no local AI
            g_proxy_of[px.c] = id;
            game::teleport(px.c, s.pos, s.rot);
        }
        if (!px.killed)
            drive::step(px.c, s.pos, s.rot, s.vel, px.drv, now, 0, true);
        else { // a corpse lies where the host has it: the guest's dead proxy shot off into the sky
               // (~2.5 km/s away from the planet; driven, it stayed ~80 m up: play_tribe.py)
            *(net::Vec3*)((uint8_t*)px.c + game::kVelOff) = {0, 0, 0};
            game::teleport(px.c, s.pos, s.rot);
        }
        if ((s.flags & net::NF_DEAD) && !px.killed) {
            px.killed = true;
            // play the death locally with a just-lethal hit: the knockback grows with the damage
            // (1e6 threw the corpse ~150 m up, it hung 15-80 m away from the host's corpse for
            // the rest of its life: play_creature.py 2026-10-23)
            game::apply_damage(px.c, std::max(game::health(px.c), 0.f) + 0.5f, 0, nullptr);
        } else if (!px.killed && s.health > 0)
            game::set_health(px.c, s.health);
        if (!px.killed) // the host's NPC was impressed (by anybody): the proxy shows it too
            game::set_impressed(px.c, (s.flags & net::NF_IMPRESSED) != 0);
    }
}

void tick(double now)
{
    const auto& creatures = game::creatures_updated(); // must run every frame
    // creature stage only: adventure creatures are scripted by the adventure on every machine
    if (!g_enabled || !C.sess || !npc_stage())
        return;
    if (is_host())
        host_tick(now, creatures);
    else if (is_guest()) {
        uint32_t n = 0;
        for (void* c : creatures)
            n += !game::is_puppet(c) && c != game::avatar();
        g_local_alive = n;
        bool away = C.host_away && C.host_away();
        if (away && !g_host_away) { // the host's NPCs stand frozen and nothing removes them: go
            std::vector<uint16_t> ids;
            for (auto& [id, px] : g_proxies)
                ids.push_back(id);
            g_away_proxies += (uint32_t)ids.size();
            for (uint16_t id : ids)
                destroy_proxy(id);
            g_herds.clear();
        }
        g_host_away = away;
        if (!away)
            guest_tick(now);
    }
}

void on_world_change()
{
    // the host's NPCs of the old world are gone and the new ones get new ids: the guests drop the
    // old ones (they mirrored them for good before 2026-10-25: a living copy of every NPC next to
    // the new one after each editor or load of the host)
    if (is_host())
        for (auto& [g, ks] : g_known)
            for (uint16_t id : ks)
                send_despawn(g, id);
    for (auto& [id, px] : g_proxies) {
        if (px.c)
            game::puppet_unregister(px.c);
        px.c = nullptr;
        px.killed = false;
        px.drv = drive::State{};
    }
    g_proxy_of.clear();
    g_herds.clear();
    g_local_herds.clear();
    g_kill.clear();
    g_host_npcs.clear();
    g_host_by_id.clear();
    for (auto& [g, ks] : g_known)
        ks.clear(); // host re-sends spawns
}

void on_session_end()
{
    std::vector<uint16_t> ids;
    for (auto& [id, px] : g_proxies)
        ids.push_back(id);
    for (uint16_t id : ids)
        destroy_proxy(id);
    g_herds.clear();
    g_local_herds.clear();
    g_host_npcs.clear();
    g_host_by_id.clear();
    g_known.clear();
    g_kill.clear();
}

void on_message(uint8_t type, uint8_t from, net::Reader& r)
{
    double now = H->time_seconds();
    switch (type) {
    case net::MSG_NPC_SPAWN: {
        net::NpcSpawn sp;
        if (!is_guest() || !sp.read(r))
            return;
        destroy_proxy(sp.net_id);
        Proxy& px = g_proxies[sp.net_id];
        px.key = {sp.key[0], sp.key[1], sp.key[2]};
        px.flags = sp.flags;
        px.health = sp.health;
        px.max_health = sp.max_health;
        net::PlayerState s;
        // the spawn carries no host time: older than any state. The guest's own clock here made
        // every state older than this snapshot whenever the guest's clock was ahead of the host's
        // (another PC, another Wine bottle): they were all dropped and the NPC stood still
        s.time_ms = 0;
        s.pos = sp.pos;
        s.rot = sp.rot;
        s.flags = sp.flags;
        s.health = sp.health;
        px.buf.delay = 0.15;
        px.buf.push(s, now);
        px.last_rx = now;
        break;
    }
    case net::MSG_NPC_DESPAWN: {
        if (!is_guest())
            return;
        uint16_t id = r.u16();
        destroy_proxy(id);
        break;
    }
    case net::MSG_NPC_STATE: {
        if (!is_guest())
            return;
        uint32_t t = r.u32();
        uint8_t n = r.u8();
        for (int i = 0; i < n && r.ok(); i++) {
            net::NpcState st;
            if (!st.read(r))
                break;
            auto it = g_proxies.find(st.net_id);
            if (it == g_proxies.end())
                continue;
            net::PlayerState s;
            s.time_ms = t;
            s.pos = st.pos;
            s.rot = st.rot;
            s.vel = st.vel;
            s.health = st.health;
            s.flags = st.flags;
            it->second.buf.push(s, now);
            it->second.last_rx = now;
        }
        g_last_states_rx = now;
        break;
    }
    case net::MSG_DAMAGE_REQ: { // host: a guest hit one of our NPCs
        net::DamageReq q;
        if (!is_host() || !q.read(r))
            return;
        auto it = g_host_by_id.find(q.net_id);
        if (it == g_host_by_id.end() || !g_seen_now.count(it->second))
            return;
        void* npc = it->second;
        void* attacker = C.puppet_of(from);
        float dmg = teamplay::on_player_hit(npc, from, q.damage);
        // kill credit: the game pays DNA, goal progress and the kill popup only for a kill by the
        // player's own avatar (TakeDamage 0xC07A78: attacker spatial vt 0x58, docs/re/creature.md 2).
        // A guest's puppet is not it, so a guest's kill paid nothing. The pack shares its progress:
        // the lethal hit lands as the host avatar's (one hit, the target dies, no aggro to move).
        void* av = game::avatar();
        if (av && !game::is_dead(npc) && game::health(npc) > 0 && game::health(npc) <= dmg) {
            attacker = av;
            g_kills_credited++;
        }
        game::apply_damage(npc, dmg, q.type, attacker);
        g_hits_applied++;
        break;
    }
    case net::MSG_DAMAGE: { // guest: the host says we were hit
        net::Damage d;
        if (!is_guest() || !d.read(r) || d.player != C.sess->my_id())
            return;
        void* av = game::avatar();
        if (!av)
            return;
        void* attacker = nullptr;
        auto it = g_proxies.find(d.attacker);
        if (it != g_proxies.end())
            attacker = it->second.c;
        game::apply_damage(av, teamplay::on_own_damage(av, d.damage), d.type, attacker);
        g_damage_in++;
        break;
    }
    }
}

bool damage_filter(void* c, float& damage, int type, void* attacker)
{
    if (!g_enabled || !C.sess || !C.sess->connected())
        return true;
    void* av = game::avatar();
    if (is_guest()) {
        auto it = g_proxy_of.find(c);
        if (it != g_proxy_of.end()) { // hit on a host NPC: the host applies it
            if (attacker == av) {
                net::DamageReq q;
                q.net_id = it->second;
                q.damage = damage;
                q.type = type;
                net::Writer w;
                q.write(w);
                C.sess->send_reliable_to(0, net::MSG_DAMAGE_REQ, w);
                g_hits_sent++;
            }
            return false;
        }
        if (game::is_puppet(c))
            return false; // no friendly fire on other players
        if (c == av)
            damage = teamplay::on_own_damage(av, damage);
        return true;
    }
    if (is_host()) {
        if (game::mode_id() == modes::kTribe)
            tribe::note_animal_hit(c, attacker);
        if (c == av) {
            damage = teamplay::on_own_damage(av, damage);
            return true;
        }
        if (!game::is_puppet(c)) {
            if (attacker == av)
                damage = teamplay::on_player_hit(c, 0, damage);
            return true;
        }
        // a guest's puppet was hit: forward real NPC hits to that guest
        for (auto& p : C.sess->players()) {
            if (p.id == 0 || C.puppet_of(p.id) != c)
                continue;
            if (attacker && attacker != av && !game::is_puppet(attacker)) {
                net::Damage d;
                d.player = p.id;
                d.damage = damage;
                d.type = type;
                auto it = g_host_npcs.find(attacker);
                d.attacker = it != g_host_npcs.end() ? it->second.id : 0xFFFF;
                net::Writer w;
                d.write(w);
                C.sess->send_reliable_to(p.id, net::MSG_DAMAGE, w);
            }
        }
        return false; // puppets mirror the guest's own health
    }
    return true;
}

std::string status()
{
    char b[384];
    if (is_host()) {
        size_t known = 0;
        for (auto& [g, ks] : g_known)
            known += ks.size();
        snprintf(b, sizeof b, "npc(host): tracked=%u alive=%u mirrored=%u hits_applied=%u guest_kills=%u\n",
                 (unsigned)g_host_npcs.size(), (unsigned)g_seen_now.size(), (unsigned)known, g_hits_applied,
                 g_kills_credited);
    } else {
        unsigned spawned = 0;
        for (auto& [id, px] : g_proxies)
            spawned += px.c != nullptr;
        snprintf(b, sizeof b, "npc(guest): proxies=%u spawned=%u local_removed=%u hits_sent=%u damage_in=%u lost=%u host_away=%d "
                 "away_dropped=%u local_alive=%u in_nest=%u nests=%u stale_dropped=%u\n",
                 (unsigned)g_proxies.size(), spawned, g_removed_local, g_hits_sent, g_damage_in, g_proxy_lost, (int)g_host_away,
                 g_away_proxies, g_local_alive, g_proxies_in_nest, (unsigned)g_local_herds.size(), g_stale_dropped);
    }
    return b;
}

} // namespace npc

namespace npc {

std::string list()
{
    std::string out;
    char b[200];
    void* av = game::avatar();
    net::Vec3 ap = av ? game::position(av) : net::Vec3{};
    if (is_host()) {
        double now = H->time_seconds();
        for (auto& [c, hn] : g_host_npcs) {
            if (!g_seen_now.count(c)) { // maybe freed already: no field reads
                snprintf(b, sizeof b, "  #%u %p not updated for %d ms\n", hn.id, c, (int)((now - hn.last_seen) * 1000));
                out += b;
                continue;
            }
            net::Vec3 p = game::position(c);
            // " DEAD" right after dist: tests match "dist=N( DEAD)?"
            snprintf(b, sizeof b, "  #%u %p species=%08X hp=%.1f dist=%.1f%s pos=(%.1f %.1f %.1f)%s\n", hn.id, c,
                     game::species_key(c).instance, game::health(c), net::dist(p, ap), game::is_dead(c) ? " DEAD" : "",
                     p.x, p.y, p.z, game::pack_member(c) ? " pack" : "");
            out += b;
        }
    } else {
        double now = H->time_seconds();
        for (auto& [k, h] : g_local_herds) {
            snprintf(b, sizeof b, "  nest %08X/%08X herd=%p alive=%d noun=%08X\n", (uint32_t)k, (uint32_t)(k >> 32), h,
                     (int)game::object_alive(h), game::noun_id(h));
            out += b;
        }
        for (auto& [id, px] : g_proxies) {
            net::PlayerState st;
            bool have = px.buf.sample(now, st);
            float err = px.c && have ? net::dist(game::position(px.c), st.pos) : -1.f;
            snprintf(b, sizeof b, "  #%u species=%08X %s hp=%.1f dist=%.1f err=%.2f age=%.2f%s want=(%.1f %.1f %.1f) puppet=%d ptr=%p herd=%p\n",
                     id, px.key.instance, px.c ? "spawned" : "pending", px.c ? game::health(px.c) : 0.f,
                     px.c ? net::dist(game::position(px.c), ap) : -1.f, err, px.buf.age(now),
                     px.killed ? " DEAD" : "", st.pos.x, st.pos.y, st.pos.z, px.c ? (int)game::is_puppet(px.c) : -1, px.c, px.c ? game::herd_of(px.c) : nullptr);
            out += b;
        }
    }
    return out;
}

bool debug_hit(uint16_t id, float damage)
{
    auto it = g_proxies.find(id);
    if (it == g_proxies.end() || !it->second.c || !game::avatar())
        return false;
    game::hit(it->second.c, damage, 0, game::avatar());
    return true;
}

} // namespace npc

namespace npc {
void* proxy(uint16_t id)
{
    auto it = g_proxies.find(id);
    return it == g_proxies.end() ? nullptr : it->second.c;
}

void* host_npc(uint16_t id)
{
    auto it = g_host_by_id.find(id);
    return it != g_host_by_id.end() && g_seen_now.count(it->second) && game::object_alive(it->second) ? it->second
                                                                                                     : nullptr;
}

int host_id(void* c)
{
    auto it = g_host_npcs.find(c);
    return it == g_host_npcs.end() ? -1 : it->second.id;
}

int proxy_id(void* c)
{
    auto it = g_proxy_of.find(c);
    return it == g_proxy_of.end() ? -1 : it->second;
}

int proxy_id_of_ai(void* ai)
{
    for (auto& [c, id] : g_proxy_of)
        if (c && *(void**)((uint8_t*)c + 0xB4C) == ai) // cCreatureAnimal +0xB4C: the AI block
            return id;
    return -1;
}

// host: the pack recruits NPC #id, as the end of a social game with an ally does (tests)
bool debug_recruit(uint16_t id)
{
    auto it = g_host_by_id.find(id);
    if (!is_host() || it == g_host_by_id.end() || !g_seen_now.count(it->second))
        return false;
    static uintptr_t get = H->sym("SporeCoop::cPosseSimulator_Get"), add = H->sym("SporeCoop::cPosseSimulator_AddMember");
    void* posse = get && add ? skg::cdecl_call<void*>(get) : nullptr;
    return posse && skg::thiscall<bool>(add, posse, it->second);
}

// host: an NPC of the given species (crt instance) a few metres from the avatar, in a herd of its
// own (tests: a species only the host's profile has, like a creation of its My Creations)
void* debug_spawn(uint32_t instance)
{
    void* av = game::avatar();
    void* sp = av ? game::species_by_key({instance, 0x2B978C46u, 0x40626200u}) : nullptr;
    if (!sp)
        return nullptr;
    net::Vec3 p = game::position(av);
    p.x += 4.f;
    void* herd = game::create_herd(p, sp);
    return herd ? game::spawn_creature(p, sp, herd) : nullptr;
}

bool debug_host_hit(uint16_t id, float damage)
{
    auto it = g_host_by_id.find(id);
    if (it == g_host_by_id.end() || !g_seen_now.count(it->second) || !game::avatar())
        return false;
    game::hit(it->second, damage, 0, game::avatar()); // through the damage filter
    return true;
}

bool debug_hurt(uint8_t player, uint16_t id, float damage)
{
    auto it = g_host_by_id.find(id);
    void* pup = C.puppet_of(player);
    if (it == g_host_by_id.end() || !g_seen_now.count(it->second) || !pup)
        return false;
    game::hit(pup, damage, 0, it->second);
    return true;
}
std::vector<syncdbg::Entry> sync_entries()
{
    std::vector<syncdbg::Entry> v;
    if (C.sess && C.sess->role() == net::Role::Host) {
        for (auto& [c, h] : g_host_npcs) // updated this frame: an unloaded one may be freed already
            if (g_seen_now.count(c) && game::object_alive(c) && !game::is_dead(c)) // living ones only
                v.push_back({"n" + std::to_string(h.id), game::position(c), game::orientation(c)});
    } else {
        for (auto& [id, px] : g_proxies)
            if (px.c && game::object_alive(px.c) && !px.killed)
                v.push_back({"n" + std::to_string(id), game::position(px.c), game::orientation(px.c)});
    }
    return v;
}

} // namespace npc
