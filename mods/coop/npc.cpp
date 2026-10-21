#include "npc.h"

#include <algorithm>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "driver.h"
#include "game.h"

namespace npc {

static const SkHost* H;
static SkMod* M [[maybe_unused]];
static Ctx C;
static bool g_enabled = true;

static constexpr float kRelevance = 110.f;  // m around a guest
static constexpr float kHysteresis = 20.f;  // keep until kRelevance + this
static constexpr double kStateInterval = 0.1;

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
static std::unordered_set<void*> g_seen_now;            // creatures alive this frame

// ------------------------------------------------------------------ guest side
struct Proxy {
    void* c = nullptr;
    game::Key key{};
    uint8_t flags = 0;
    float health = 0, max_health = 0;
    bool killed = false;
    int fail = 0; // 1 species, 2 herd, 3 create
    net::SnapshotBuffer buf;
    drive::State drv;
};
static std::map<uint16_t, Proxy> g_proxies;
static std::unordered_map<void*, uint16_t> g_proxy_of;
static std::map<uint64_t, void*> g_herds; // species instance/group -> herd for proxies
static std::vector<void*> g_kill;          // local NPCs to remove (guest)
static uint32_t g_removed_local = 0, g_hits_sent = 0, g_hits_applied = 0, g_damage_in = 0;

static bool is_host() { return C.sess && C.sess->role() == net::Role::Host; }
static bool is_guest() { return C.sess && C.sess->role() == net::Role::Client && C.sess->connected(); }

bool is_proxy(void* c) { return g_proxy_of.count(c) != 0; }
void set_enabled(bool on) { g_enabled = on; }
bool enabled() { return g_enabled; }

// called from inside the game's creature update loop: keep it cheap
static bool update_filter(void* c)
{
    if (!g_enabled || !is_guest())
        return true;
    if (game::is_puppet(c) || c == game::avatar())
        return true;
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
    s.flags = game::is_dead(c) ? net::NF_DEAD : 0;
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
    g_seen_now.clear();
    if (!av)
        return;
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
        if (!C.player_pos(p.id, gp)) {
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
            if (!k && d < kRelevance) {
                send_spawn(p.id, id, c);
                known.insert(id);
                k = true;
            } else if (k && d > kRelevance + kHysteresis) {
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
    uint64_t hk = ((uint64_t)k.group << 32) | k.instance;
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
    if (!av)
        return;
    for (auto& [id, px] : g_proxies) {
        net::PlayerState s;
        if (!px.buf.sample(now, s))
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
        drive::step(px.c, s.pos, s.rot, s.vel, px.drv, now);
        if ((s.flags & net::NF_DEAD) && !px.killed) {
            px.killed = true;
            game::apply_damage(px.c, 1e6f, 0, nullptr); // play the death locally
        } else if (!px.killed && s.health > 0)
            game::set_health(px.c, s.health);
    }
}

void tick(double now)
{
    const auto& creatures = game::creatures_updated(); // must run every frame
    if (!g_enabled || !C.sess)
        return;
    if (is_host())
        host_tick(now, creatures);
    else if (is_guest())
        guest_tick(now);
}

void on_session_end()
{
    std::vector<uint16_t> ids;
    for (auto& [id, px] : g_proxies)
        ids.push_back(id);
    for (uint16_t id : ids)
        destroy_proxy(id);
    g_herds.clear();
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
        s.time_ms = net::now_ms();
        s.pos = sp.pos;
        s.rot = sp.rot;
        s.flags = sp.flags;
        s.health = sp.health;
        px.buf.delay = 0.15;
        px.buf.push(s, now);
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
        }
        break;
    }
    case net::MSG_DAMAGE_REQ: { // host: a guest hit one of our NPCs
        net::DamageReq q;
        if (!is_host() || !q.read(r))
            return;
        auto it = g_host_by_id.find(q.net_id);
        if (it == g_host_by_id.end() || !g_seen_now.count(it->second))
            return;
        void* attacker = C.puppet_of(from);
        game::apply_damage(it->second, q.damage, q.type, attacker);
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
        game::apply_damage(av, d.damage, d.type, attacker);
        g_damage_in++;
        break;
    }
    }
}

bool damage_filter(void* c, float damage, int type, void* attacker)
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
        return true;
    }
    if (is_host()) {
        if (!game::is_puppet(c))
            return true;
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
    char b[256];
    if (is_host()) {
        size_t known = 0;
        for (auto& [g, ks] : g_known)
            known += ks.size();
        snprintf(b, sizeof b, "npc(host): tracked=%u alive=%u mirrored=%u hits_applied=%u\n",
                 (unsigned)g_host_npcs.size(), (unsigned)g_seen_now.size(), (unsigned)known, g_hits_applied);
    } else {
        unsigned spawned = 0;
        for (auto& [id, px] : g_proxies)
            spawned += px.c != nullptr;
        snprintf(b, sizeof b, "npc(guest): proxies=%u spawned=%u local_removed=%u hits_sent=%u damage_in=%u\n",
                 (unsigned)g_proxies.size(), spawned, g_removed_local, g_hits_sent, g_damage_in);
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
        for (auto& [c, hn] : g_host_npcs) {
            if (!g_seen_now.count(c))
                continue;
            snprintf(b, sizeof b, "  #%u species=%08X hp=%.1f dist=%.1f%s\n", hn.id, game::species_key(c).instance,
                     game::health(c), net::dist(game::position(c), ap), game::is_dead(c) ? " DEAD" : "");
            out += b;
        }
    } else {
        for (auto& [id, px] : g_proxies) {
            snprintf(b, sizeof b, "  #%u species=%08X %s hp=%.1f dist=%.1f%s\n", id, px.key.instance,
                     px.c ? "spawned" : "pending", px.c ? game::health(px.c) : 0.f,
                     px.c ? net::dist(game::position(px.c), ap) : -1.f, px.killed ? " DEAD" : "");
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
bool debug_hurt(uint8_t player, uint16_t id, float damage)
{
    auto it = g_host_by_id.find(id);
    void* pup = C.puppet_of(player);
    if (it == g_host_by_id.end() || !g_seen_now.count(it->second) || !pup)
        return false;
    game::hit(pup, damage, 0, it->second);
    return true;
}
} // namespace npc
