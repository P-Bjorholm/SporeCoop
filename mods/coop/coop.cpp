// SporeCoop mod: creature-stage co-op ("pack" mode). See docs/03-gameplay.md.
#include <winsock2.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cmath>
#include <cstdio>
#include <deque>
#include <map>
#include <string>

#include "driver.h"
#include "game.h"
#include "npc.h"
#include "world.h"
#include "net/interp.h"
#include "net/session.h"
#include "sporekit.h"

static const SkHost* H;
static SkMod* M;

void coop_register_debug_commands(const SkHost* host, SkMod* self); // debug.cpp

#define LOG(...) H->log(M, SK_INFO, __VA_ARGS__)
#define SAY(...) H->out_printf(out, __VA_ARGS__)

namespace {

struct Remote {
    uint8_t id = 0;
    std::string name;
    net::SnapshotBuffer buf;
    void* puppet = nullptr;
    drive::State drv;
    bool echo = false; // local loopback test remote
};

net::Session g_sess;
std::map<uint8_t, Remote> g_remotes;
std::string g_name = "Player";
double g_last_send = 0;
void* g_last_avatar = nullptr;
std::deque<std::pair<double, std::string>> g_feed;
bool g_overlay = true;
bool g_share_world = true; // host sends its save to joining guests
bool g_anim_log = false;
double g_last_time_sync = 0;
float g_dna_sent = -1;      // host: last broadcast total
uint32_t g_dna_rx = 0, g_dna_fwd = 0;

// DNA earned locally: the host's total is authoritative
bool on_dna(float delta)
{
    if (g_sess.role() == net::Role::Client && g_sess.connected() && delta > 0) {
        net::Writer w;
        w.f32(delta);
        g_sess.send_reliable_to(0, net::MSG_DNA_ADD, w);
        g_dna_fwd++;
        return false; // applied when the host's total comes back
    }
    return true;
}
bool g_tag_self = false; // debug: draw a tag over our own avatar to check the projection
float g_dbg_sc[3];
float g_puppet_scale = 1.15f;

// echo test: mirrors our own avatar as a fake remote player
bool g_echo = false;
float g_echo_offset = 6.f;
double g_echo_delay = 0.0;
std::deque<std::pair<double, net::PlayerState>> g_echo_q;

void feed(const std::string& s)
{
    g_feed.emplace_back(H->time_seconds(), s);
    while (g_feed.size() > 8)
        g_feed.pop_front();
    LOG("%s", s.c_str());
}

uint32_t build_id()
{
    // first 4 bytes of the exe sha256: peers must run the same game build
    return (uint32_t)strtoul(std::string(H->exe_sha256()).substr(0, 8).c_str(), nullptr, 16);
}

void despawn(Remote& r)
{
    if (r.puppet) {
        game::destroy_creature(r.puppet);
        r.puppet = nullptr;
    }
}

void forget_puppets() // world changed: puppets were destroyed with it
{
    for (auto& [id, r] : g_remotes) {
        if (r.puppet)
            game::puppet_unregister(r.puppet);
        r.puppet = nullptr;
    }
}

net::PlayerState local_state(void* av)
{
    net::PlayerState st;
    if (!av)
        return st;
    st.flags = net::PF_IN_WORLD;
    st.pos = game::position(av);
    st.rot = game::orientation(av);
    st.vel = game::velocity(av);
    float sp = std::sqrt(st.vel.x * st.vel.x + st.vel.y * st.vel.y + st.vel.z * st.vel.z);
    if (sp > 0.3f)
        st.flags |= net::PF_MOVING;
    if (sp > 6.f)
        st.flags |= net::PF_RUNNING;
    if (game::is_dead(av))
        st.flags |= net::PF_DEAD;
    st.health = game::health(av);
    st.max_health = game::max_health(av);
    st.species = game::species_instance(av);
    return st;
}

void drive_puppet(Remote& r, void* av, double now)
{
    net::PlayerState s;
    if (!r.buf.sample(now, s))
        return;
    // dead players are hidden until they respawn at the nest
    bool in_world = (s.flags & net::PF_IN_WORLD) && !(s.flags & net::PF_DEAD) && r.buf.age(now) < 5.0;
    if (!in_world) {
        despawn(r);
        return;
    }
    if (!r.puppet) {
        r.puppet = game::spawn_creature(s.pos, game::species_profile(av), game::avatar_herd());
        if (!r.puppet) {
            feed("failed to spawn creature for " + r.name);
            return;
        }
        game::puppet_register(r.puppet);
        game::teleport(r.puppet, s.pos, s.rot);
        game::set_scale(r.puppet, game::scale(r.puppet) * g_puppet_scale);
        r.drv = drive::State{};
        feed(r.name + " appeared in the world");
    }
    void* p = r.puppet;
    drive::step(p, s.pos, s.rot, s.vel, r.drv, now);
    // M1: puppets are invulnerable mirrors of the remote health
    float mh = game::max_health(p);
    if (mh > 0)
        *(float*)((uint8_t*)p + game::kHpOff) = mh;
}

void on_frame(void*, int, void*)
{
    double now = H->time_seconds();
    g_sess.update();
    if (g_sess.role() == net::Role::Host)
        world::tick(g_sess);
    npc::tick(now);
    if (g_sess.role() == net::Role::Host && game::avatar()) {
        float total = game::dna();
        if (total != g_dna_sent) {
            g_dna_sent = total;
            net::Writer w;
            w.f32(total);
            for (auto& p : g_sess.players())
                if (p.id != 0)
                    g_sess.send_reliable_to(p.id, net::MSG_DNA, w);
        }
    }
    if (g_sess.role() == net::Role::Host && now - g_last_time_sync > 1.0) {
        g_last_time_sync = now;
        net::Quat q;
        if (game::time_of_day(q)) {
            net::Writer w;
            w.f32(q.x); w.f32(q.y); w.f32(q.z); w.f32(q.w);
            for (auto& p : g_sess.players())
                if (p.id != 0)
                    g_sess.send_unreliable_to(p.id, net::MSG_WORLD_TIME, w);
        }
    }

    void* av = game::avatar();
    if (av != g_last_avatar) {
        if (g_last_avatar)
            forget_puppets();
        g_last_avatar = av;
    }

    if (now - g_last_send >= 0.05) {
        g_last_send = now;
        net::PlayerState st = local_state(av);
        if (g_sess.connected())
            g_sess.send_state(st);
        if (g_echo && av) {
            // fake remote: our own state, shifted sideways, delayed
            st.id = 200;
            st.time_ms = (uint32_t)(now * 1000.0);
            net::Quat q = st.rot;
            // local "right" vector from the orientation quaternion (x axis)
            float rx = 1 - 2 * (q.y * q.y + q.z * q.z), ry = 2 * (q.x * q.y + q.w * q.z),
                  rz = 2 * (q.x * q.z - q.w * q.y);
            st.pos.x += rx * g_echo_offset;
            st.pos.y += ry * g_echo_offset;
            st.pos.z += rz * g_echo_offset;
            g_echo_q.emplace_back(now + g_echo_delay, st);
        }
    }
    while (!g_echo_q.empty() && g_echo_q.front().first <= now) {
        auto& r = g_remotes[200];
        r.id = 200;
        r.name = "Echo";
        r.echo = true;
        r.buf.push(g_echo_q.front().second, now);
        g_echo_q.pop_front();
    }

    for (auto& [id, r] : g_remotes) {
        if (!av) {
            if (r.puppet)
                forget_puppets();
            continue;
        }
        drive_puppet(r, av, now);
    }
}

int text_width(const std::string& s) { return (int)s.size() * 9; } // Arial 18 bold, approx.

void on_overlay(void*, int, void*)
{
    if (!g_overlay)
        return;
    int sw = 0, sh = 0;
    H->screen_size(&sw, &sh);
    // name tags above remote players
    void* av = game::avatar();
    if (g_tag_self && av) {
        net::Vec3 p = game::position(av);
        float sc[2] = {0, 0}, w[3] = {p.x, p.y, p.z};
        int ok = H->world_to_screen(w, sc);
        g_dbg_sc[0] = sc[0]; g_dbg_sc[1] = sc[1]; g_dbg_sc[2] = (float)ok;
        if (ok)
            H->overlay_text((int)sc[0] - 4, (int)sc[1] - 10, 0xFFFF4040, "+");
    }
    for (auto& [id, r] : g_remotes) {
        if (!r.puppet || !av)
            continue;
        net::Vec3 p = game::position(r.puppet);
        float len = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
        float h = game::bounding_radius(r.puppet) * 1.3f + 0.3f;
        float up = len > 1 ? h / len : 0; // above the head, along the planet normal
        float w[3] = {p.x + p.x * up, p.y + p.y * up, p.z + p.z * up}, sc[2];
        if (H->world_to_screen(w, sc)) {
            std::string tag = r.name;
            H->overlay_text((int)sc[0] - text_width(tag) / 2, (int)sc[1] - 10, 0xFF80FF80, tag.c_str());
        }
    }
    std::string head;
    if (g_sess.role() == net::Role::Host)
        head = "CO-OP host :" + std::to_string(g_sess.port()) + "  " + std::to_string(g_sess.players().size()) +
               "/" + std::to_string(net::kMaxPlayers);
    else if (g_sess.role() == net::Role::Client)
        head = std::string("CO-OP guest  ") + (g_sess.connected() ? "connected" : "connecting...");
    else if (g_echo)
        head = "CO-OP echo test";
    if (head.empty())
        return;
    int x = sw > 0 ? sw - 480 : 900, y = 70;
    H->overlay_text(x, y, 0xFFFFD040, head.c_str());
    y += 22;
    for (auto& p : g_sess.players()) {
        std::string line = (p.id == g_sess.my_id() ? "> " : "  ") + p.name;
        H->overlay_text(x, y, 0xFFFFFFFF, line.c_str());
        y += 20;
    }
    double now = H->time_seconds();
    y += 8;
    for (auto& [t, s] : g_feed) {
        if (now - t > 12)
            continue;
        H->overlay_text(x, y, 0xFFB0E0FF, s.c_str());
        y += 20;
    }
}

void on_anim(void* creature, uint32_t anim, int block, bool disable_blend, bool to_target)
{
    void* av = game::avatar();
    if (!av || creature != av)
        return;
    if (g_anim_log)
        LOG("avatar anim 0x%08X block=%d%s%s", anim, block, disable_blend ? " noblend" : "", to_target ? " (to)" : "");
    net::AnimEvent ev;
    ev.id = g_sess.my_id();
    ev.anim = anim;
    ev.block = block;
    ev.flags = (disable_blend ? 1 : 0) | (to_target ? 2 : 0);
    if (g_sess.connected()) {
        net::Writer w;
        ev.write(w);
        g_sess.send_event(net::MSG_ANIM, w);
    }
    if (g_echo) { // loopback test: replay on the echo puppet
        auto it = g_remotes.find(200);
        if (it != g_remotes.end() && it->second.puppet)
            game::play_animation(it->second.puppet, anim, block, disable_blend);
    }
}

uint32_t g_anims_rx = 0, g_anims_played = 0;

void on_remote_anim(const net::AnimEvent& ev)
{
    g_anims_rx++;
    if (g_anim_log)
        LOG("remote %u anim 0x%08X", ev.id, ev.anim);
    auto it = g_remotes.find(ev.id);
    if (it == g_remotes.end() || !it->second.puppet)
        return;
    game::play_animation(it->second.puppet, ev.anim, ev.block, ev.flags & 1);
    g_anims_played++;
}

void setup_callbacks()
{
    g_sess.build = build_id();
    g_sess.cb.log = [](const std::string& s) { LOG("net: %s", s.c_str()); };
    g_sess.cb.connected = [] { feed("connected to host"); };
    g_sess.cb.disconnected = [](const std::string& why) {
        feed("disconnected: " + why);
        npc::on_session_end();
        for (auto& [id, r] : g_remotes)
            despawn(r);
        g_remotes.clear();
    };
    g_sess.cb.player_join = [](const net::PlayerInfo& p) {
        g_dna_sent = -1; // resend the DNA total to the newcomer
        auto& r = g_remotes[p.id];
        r.id = p.id;
        r.name = p.name;
        feed(p.name + " joined the pack");
        if (g_sess.role() == net::Role::Host && g_share_world) {
            std::string err;
            if (!world::share_with(p.id, err))
                feed("cannot share the world: " + err);
            else
                feed("sending the world to " + p.name);
        }
    };
    g_sess.cb.custom = [](uint8_t type, uint8_t from, net::Reader& r) {
        if (type == net::MSG_ANIM) {
            net::AnimEvent ev;
            if (ev.read(r))
                on_remote_anim(ev);
            return;
        }
        if (type == net::MSG_DNA && g_sess.role() == net::Role::Client) {
            float total = r.f32();
            if (r.ok() && game::avatar()) {
                float d = total - game::dna();
                if (d != 0)
                    game::dna_add_raw(d); // through the game so the UI/progress updates
                g_dna_rx++;
            }
            return;
        }
        if (type == net::MSG_DNA_ADD && g_sess.role() == net::Role::Host) {
            float d = r.f32();
            if (r.ok() && d > 0 && d < 10000 && game::avatar()) {
                game::dna_add_raw(d);
                feed(g_sess.name_of(from) + " earned " + std::to_string((int)d) + " DNA");
            }
            return;
        }
        if (type == net::MSG_WORLD_TIME && g_sess.role() == net::Role::Client) {
            net::Quat q;
            q.x = r.f32(); q.y = r.f32(); q.z = r.f32(); q.w = r.f32();
            if (r.ok() && game::avatar())
                game::set_time_of_day(q);
            return;
        }
        switch (type) {
        case net::MSG_NPC_SPAWN: case net::MSG_NPC_DESPAWN: case net::MSG_NPC_STATE:
        case net::MSG_DAMAGE_REQ: case net::MSG_DAMAGE:
            npc::on_message(type, from, r);
            return;
        }
        if (g_sess.role() == net::Role::Client && from == 0)
            world::on_message(type, r, g_sess.name_of(0));
    };
    g_sess.cb.player_leave = [](uint8_t id, uint8_t) {
        auto it = g_remotes.find(id);
        if (it == g_remotes.end())
            return;
        feed(it->second.name + " left");
        despawn(it->second);
        g_remotes.erase(it);
    };
    g_sess.cb.player_state = [](const net::PlayerState& s) {
        auto& r = g_remotes[s.id];
        r.id = s.id;
        if (r.name.empty())
            r.name = g_sess.name_of(s.id);
        r.buf.push(s, H->time_seconds());
    };
    g_sess.cb.chat = [](uint8_t id, const std::string& t) { feed("<" + g_sess.name_of(id) + "> " + t); };
}

// ---------------- commands ----------------
int c_host(void*, int argc, const char* const* argv, SkOut* out)
{
    uint16_t port = argc > 1 ? (uint16_t)atoi(argv[1]) : (uint16_t)(net::kDefaultPort + H->instance_id() - 1);
    if (argc > 2)
        g_name = argv[2];
    std::string err;
    if (!g_sess.host(port, g_name, err)) {
        SAY("host failed: %s", err.c_str());
        return 1;
    }
    feed("hosting on port " + std::to_string(g_sess.port()));
    SAY("hosting on UDP %u as '%s'", g_sess.port(), g_name.c_str());
    return 0;
}

int c_join(void*, int argc, const char* const* argv, SkOut* out)
{
    if (argc < 2) {
        SAY("usage: coop.join <ip[:port]> [name]");
        return 1;
    }
    if (argc > 2)
        g_name = argv[2];
    std::string err;
    if (!g_sess.join(argv[1], g_name, err)) {
        SAY("join failed: %s", err.c_str());
        return 1;
    }
    SAY("joining %s as '%s'", argv[1], g_name.c_str());
    return 0;
}

int c_leave(void*, int, const char* const*, SkOut* out)
{
    g_sess.leave("user");
    npc::on_session_end();
    for (auto& [id, r] : g_remotes)
        despawn(r);
    g_remotes.clear();
    SAY("left session");
    return 0;
}

int c_status(void*, int, const char* const*, SkOut* out)
{
    SAY("%s%s", g_sess.status().c_str(), world::status().c_str());
    SAY("remote animations: received %u, played %u\n", g_anims_rx, g_anims_played);
    SAY("%s", npc::status().c_str());
    SAY("dna=%.1f (rx %u totals, forwarded %u gains)\n", game::dna(), g_dna_rx, g_dna_fwd);
    void* av = game::avatar();
    SAY("mode=0x%08X avatar=%p overlay-self=(%.1f %.1f ok=%.0f)\n", game::mode_id(), av, g_dbg_sc[0], g_dbg_sc[1], g_dbg_sc[2]);
    double now = H->time_seconds();
    for (auto& [id, r] : g_remotes) {
        net::PlayerState s;
        bool have = r.buf.sample(now, s);
        SAY("remote %u '%s' puppet=%p state=%s age=%.2fs pos=(%.1f %.1f %.1f)\n", id, r.name.c_str(),
            r.puppet, have ? "yes" : "no", r.buf.age(now), s.pos.x, s.pos.y, s.pos.z);
        if (r.puppet) {
            net::Vec3 p = game::position(r.puppet);
            SAY("   puppet pos=(%.1f %.1f %.1f) err=%.2f\n", p.x, p.y, p.z, net::dist(p, s.pos));
        }
    }
    return 0;
}

int c_name(void*, int argc, const char* const* argv, SkOut* out)
{
    if (argc > 1)
        g_name = argv[1];
    SAY("name: %s", g_name.c_str());
    return 0;
}

int c_say(void*, int argc, const char* const* argv, SkOut* out)
{
    std::string t;
    for (int i = 1; i < argc; i++) {
        if (i > 1) t += ' ';
        t += argv[i];
    }
    if (!g_sess.connected()) {
        SAY("not connected");
        return 1;
    }
    g_sess.send_chat(t);
    SAY("sent");
    return 0;
}

int c_avatar(void*, int, const char* const*, SkOut* out)
{
    void* av = game::avatar();
    if (!av) {
        SAY("no avatar (mode 0x%08X)", game::mode_id());
        return 1;
    }
    auto st = local_state(av);
    SAY("avatar=%p species=%p key=0x%08X\npos=(%.2f %.2f %.2f) |r|=%.1f\nrot=(%.3f %.3f %.3f %.3f)\n"
        "vel=(%.2f %.2f %.2f)\nhp=%.1f/%.1f flags=0x%02X",
        av, game::species_profile(av), st.species, st.pos.x, st.pos.y, st.pos.z,
        std::sqrt(st.pos.x * st.pos.x + st.pos.y * st.pos.y + st.pos.z * st.pos.z), st.rot.x, st.rot.y,
        st.rot.z, st.rot.w, st.vel.x, st.vel.y, st.vel.z, st.health, st.max_health, st.flags);
    return 0;
}

int c_echo(void*, int argc, const char* const* argv, SkOut* out)
{
    g_echo = argc < 2 || strcmp(argv[1], "off") != 0;
    if (argc > 2)
        g_echo_offset = (float)atof(argv[2]);
    if (argc > 3)
        g_echo_delay = atof(argv[3]);
    if (!g_echo) {
        auto it = g_remotes.find(200);
        if (it != g_remotes.end()) {
            despawn(it->second);
            g_remotes.erase(it);
        }
        g_echo_q.clear();
    }
    SAY("echo %s offset=%.1f delay=%.2f", g_echo ? "on" : "off", g_echo_offset, g_echo_delay);
    return 0;
}

int c_share(void*, int argc, const char* const* argv, SkOut* out)
{
    if (g_sess.role() != net::Role::Host) {
        SAY("only the host shares its world");
        return 1;
    }
    int n = 0;
    for (auto& p : g_sess.players()) {
        if (p.id == 0 || (argc > 1 && atoi(argv[1]) != p.id))
            continue;
        std::string err;
        if (world::share_with(p.id, err))
            n++;
        else
            SAY("player %u: %s\n", p.id, err.c_str());
    }
    SAY("sharing the world with %d player(s)", n);
    return 0;
}

int c_npc(void*, int argc, const char* const* argv, SkOut* out)
{
    if (argc > 1 && !strcmp(argv[1], "list")) {
        SAY("%s%s", npc::status().c_str(), npc::list().c_str());
        return 0;
    }
    if (argc > 4 && !strcmp(argv[1], "hurt")) {
        bool ok = npc::debug_hurt((uint8_t)atoi(argv[2]), (uint16_t)atoi(argv[3]), (float)atof(argv[4]));
        SAY(ok ? "puppet hit through the damage filter" : "no such npc/puppet");
        return ok ? 0 : 1;
    }
    if (argc > 3 && !strcmp(argv[1], "hit")) {
        bool ok = npc::debug_hit((uint16_t)atoi(argv[2]), (float)atof(argv[3]));
        SAY(ok ? "hit sent through the damage filter" : "no such spawned proxy");
        return ok ? 0 : 1;
    }
    if (argc > 1)
        npc::set_enabled(strcmp(argv[1], "off") != 0);
    SAY("npc sync %s\n%s", npc::enabled() ? "on" : "off", npc::status().c_str());
    return 0;
}

int c_overlay(void*, int argc, const char* const* argv, SkOut* out)
{
    if (argc > 1 && !strcmp(argv[1], "anims")) {
        g_anim_log = !g_anim_log;
        SAY("avatar animation log %s", g_anim_log ? "on" : "off");
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "self")) {
        g_tag_self = !g_tag_self;
        SAY("self tag %s", g_tag_self ? "on" : "off");
        return 0;
    }
    g_overlay = argc < 2 || strcmp(argv[1], "off") != 0;
    SAY("overlay %s", g_overlay ? "on" : "off");
    return 0;
}

void on_unload()
{
    g_sess.leave("mod unload");
    npc::on_session_end();
    for (auto& [id, r] : g_remotes)
        despawn(r);
    g_remotes.clear();
}

SkModInfo g_info = {SK_ABI_VERSION, "coop", "0.1.0", on_unload};

} // namespace

extern "C" SK_EXPORT const SkModInfo* sporekit_mod_main(const SkHost* host, SkMod* self)
{
    H = host;
    M = self;
    if (!game::init(H, M)) {
        H->log(M, SK_ERROR, "required game symbols missing - coop disabled");
        return nullptr;
    }
    char nm[64];
    H->config_str(M, "coop", "name", "Player", nm, sizeof nm);
    g_name = nm;
    g_share_world = H->config_int(M, "coop", "share_world", 1) != 0;
    world::init(H, M, feed);
    game::set_anim_listener(on_anim);
    game::set_dna_listener(on_dna);
    npc::Ctx ctx;
    ctx.sess = &g_sess;
    ctx.puppet_of = [](uint8_t id) -> void* {
        auto it = g_remotes.find(id);
        return it == g_remotes.end() ? nullptr : it->second.puppet;
    };
    ctx.player_pos = [](uint8_t id, net::Vec3& pos) {
        auto it = g_remotes.find(id);
        if (it == g_remotes.end() || it->second.buf.empty())
            return false;
        const auto& st = it->second.buf.latest();
        if (!(st.flags & net::PF_IN_WORLD) || it->second.buf.age(H->time_seconds()) > 3.0)
            return false;
        pos = st.pos;
        return true;
    };
    ctx.feed = feed;
    npc::init(H, M, ctx);
    npc::set_enabled(H->config_int(M, "coop", "share_npcs", 1) != 0);
    game::set_damage_filter(npc::damage_filter);
    setup_callbacks();
    H->subscribe(M, SK_EV_FRAME, on_frame, nullptr);
    H->subscribe(M, SK_EV_OVERLAY, on_overlay, nullptr);
    const int MT = SK_CMD_MAIN_THREAD;
    H->command_add(M, "coop.host", "coop.host [port] [name] - host a session", c_host, nullptr, MT);
    H->command_add(M, "coop.join", "coop.join <ip[:port]> [name] - join a host", c_join, nullptr, MT);
    H->command_add(M, "coop.leave", "leave the session", c_leave, nullptr, MT);
    H->command_add(M, "coop.status", "session, players and puppets", c_status, nullptr, MT);
    H->command_add(M, "coop.name", "coop.name <nick>", c_name, nullptr, MT);
    H->command_add(M, "coop.say", "coop.say <text> - chat", c_say, nullptr, MT);
    H->command_add(M, "coop.avatar", "print local avatar state", c_avatar, nullptr, MT);
    H->command_add(M, "coop.echo", "coop.echo [on|off] [offset] [delay] - loopback puppet test", c_echo, nullptr, MT);
    H->command_add(M, "coop.overlay", "coop.overlay on|off|self", c_overlay, nullptr, MT);
    H->command_add(M, "coop.npc", "coop.npc [on|off|list|hit <id> <dmg>] - host-authoritative NPC sync", c_npc, nullptr, MT);
    H->command_add(M, "coop.share", "coop.share [player id] - (re)send the host world (save) to guests", c_share,
                   nullptr, MT);
    coop_register_debug_commands(H, M);
    H->cheat_register("coop", "coop.",
                      "coop host [port] | join <ip[:port]> | leave | status | name <nick> | say <text>");
    LOG("coop ready (name '%s')", g_name.c_str());
    return &g_info;
}
