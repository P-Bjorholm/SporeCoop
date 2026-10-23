// SporeCoop mod: creature-stage co-op ("pack" mode). See docs/03-gameplay.md.
// the release version (tools/package.sh names the zip after it)
#ifndef COOP_VERSION
#define COOP_VERSION "0.5.0"
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <iphlpapi.h>

#include <atomic>
#include <cmath>
#include <thread>
#include <cstdio>
#include <deque>
#include <map>
#include <string>

#include "coop_ui.h"
#include "driver.h"
#include "game.h"
#include "modes.h"
#include "shared.h"
#include "sui.h"
#include "teamplay.h"
#include "tribe.h"
#include "cellsync.h"
#include "civ.h"
#include "autoupdate.h"
#include "spacecoop.h"
#include "squadron.h"
#include "npc.h"
#include "world.h"
#include "net/discovery.h"
#include "net/interp.h"
#include "net/session.h"
#include "creations.h"
#include "notify.h"
#include "syncdbg.h"
#include "sporekit.h"
#include "sporekit_game.hpp"

static const SkHost* H;
static SkMod* M;
extern const SkHost* g_host_for_modes; // modes.cpp
extern SkMod* g_mod_for_modes;

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
    const modes::Adapter* puppet_adapter = nullptr; // adapter that created `puppet`
    bool echo = false; // local loopback test remote
    uint64_t world_sent = 0; // host: (mode << 32 | save crc) last sent to this guest
    double other_mode_since = -1; // host: since when the guest plays another mode
    bool joined_phase = false; // host: the guest played the host's mode after the last world send
    bool needs_world = false;  // host: joined while the host was not playing; send the world later
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
    notify::show(s); // a notification panel; the full history is in the co-op window
}

// ---- lobby: chat log, ready flags, round trip times, LAN discovery ----
std::deque<std::string> g_chat;
std::map<uint8_t, bool> g_ready;   // by player id (the host keeps the truth)
std::map<uint8_t, uint8_t> g_roles; // civilization roles by player id (the host keeps the truth)
std::map<uint8_t, int> g_lobby_rtt; // guest: host-measured rtt of every player
bool g_lobby_dirty = false;
double g_last_lobby = 0;
net::Discovery g_disc;
bool g_disc_on = false;

void chat_log(const std::string& s)
{
    g_chat.push_back(s);
    while (g_chat.size() > 50)
        g_chat.pop_front();
}

// an event everybody sees (feed + lobby chat): guests send it to the host, the host relays it
void notice(const std::string& text, int from = -1)
{
    feed(text);
    chat_log("* " + text);
    net::Writer w;
    w.str8(text);
    if (g_sess.role() == net::Role::Host)
        g_sess.broadcast_reliable(net::MSG_NOTICE, w, from);
    else if (g_sess.role() == net::Role::Client && from < 0)
        g_sess.send_reliable_to(0, net::MSG_NOTICE, w);
}

struct LocalAddr {
    std::string ip;
    uint32_t broadcast; // network byte order
};
std::vector<LocalAddr> local_addresses()
{
    static std::vector<LocalAddr> cache;
    static DWORD at = 0;
    if (!cache.empty() && GetTickCount() - at < 10000)
        return cache;
    at = GetTickCount();
    cache.clear();
    ULONG len = 16384;
    std::vector<uint8_t> buf(len);
    auto* aa = (IP_ADAPTER_ADDRESSES*)buf.data();
    if (GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                             nullptr, aa, &len) != NO_ERROR)
        return cache;
    for (auto* a = aa; a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
            continue;
        for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
            auto* sin = (sockaddr_in*)u->Address.lpSockaddr;
            if (!sin || sin->sin_family != AF_INET)
                continue;
            uint32_t ip = sin->sin_addr.s_addr;
            const uint8_t* b = (const uint8_t*)&ip;
            if (b[0] == 127 || (b[0] == 169 && b[1] == 254))
                continue;
            int plen = u->OnLinkPrefixLength;
            uint32_t mask = plen <= 0 ? 0 : htonl(plen >= 32 ? 0xFFFFFFFFu : ~(0xFFFFFFFFu >> plen));
            char s[32];
            snprintf(s, sizeof s, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
            cache.push_back({s, (ip & mask) | ~mask});
        }
    }
    return cache;
}

void lan_scan(bool on)
{
    if (on == g_disc_on)
        return;
    g_disc_on = on;
    if (!on) {
        g_disc.stop();
        return;
    }
    g_disc.targets = {0xFFFFFFFFu, 0x0100007Fu};
    for (auto& a : local_addresses())
        if (a.broadcast != 0xFFFFFFFFu)
            g_disc.targets.push_back(a.broadcast);
    g_disc.start();
}

uint32_t build_id()
{
    // first 4 bytes of the exe sha256: peers must run the same game build
    return (uint32_t)strtoul(std::string(H->exe_sha256()).substr(0, 8).c_str(), nullptr, 16);
}

void despawn(Remote& r)
{
    if (r.puppet && r.puppet_adapter)
        r.puppet_adapter->despawn(r.puppet);
    r.puppet = nullptr;
    r.puppet_adapter = nullptr;
}

void forget_puppets() // world changed: puppets were destroyed with it
{
    for (auto& [id, r] : g_remotes) {
        if (r.puppet)
            game::puppet_unregister(r.puppet);
        r.puppet = nullptr;
        r.puppet_adapter = nullptr;
    }
}

uint32_t g_mode = 0, g_world = 0;
bool g_save_on_share = true; // host saves before sending the world, so guests get the current state
const modes::Adapter* g_adapter = nullptr;
void* g_local = nullptr; // entity the local player controls (adapter specific)

net::PlayerState local_state()
{
    net::PlayerState st;
    st.mode = g_mode;
    st.world = g_world;
    if (g_adapter && g_local)
        g_adapter->fill(g_local, st);
    return st;
}

// remote `s` is visible here only when both play the same mode in the same world
bool same_place(const net::PlayerState& s) { return s.mode == g_mode && s.world == g_world && g_world != 0; }

void drive_puppet(Remote& r, double now)
{
    net::PlayerState s;
    if (!r.buf.sample(now, s))
        return;
    // dead players are hidden until they respawn
    bool visible = g_adapter && g_local && (s.flags & net::PF_IN_WORLD) && !(s.flags & net::PF_DEAD) &&
                   r.buf.age(now) < 5.0 && (r.echo || same_place(s));
    if (r.puppet && r.puppet_adapter && r.puppet_adapter->alive && !r.puppet_adapter->alive(r.puppet)) {
        r.puppet = nullptr; // removed by the game itself: show it again
        r.puppet_adapter = nullptr;
    }
    if (!visible || (r.puppet && r.puppet_adapter != g_adapter)) {
        despawn(r);
        if (!visible)
            return;
    }
    if (!r.puppet) {
        r.puppet = g_adapter->spawn(s, g_local);
        if (!r.puppet) {
            feed("failed to show " + r.name);
            return;
        }
        r.puppet_adapter = g_adapter;
        r.drv = drive::State{};
        feed(r.name + " appeared in the world");
    }
    g_adapter->drive(r.puppet, s, r.drv, now);
}

bool stage_mode(uint32_t m)
{
    return m == modes::kCell || m == modes::kCreature || m == modes::kTribe || m == modes::kCiv ||
           m == modes::kSpace;
}

void notice(const std::string& text, int from); // feed + chat log on every player
void add_beacon(const net::Vec3& p, const std::string& text, double seconds, uint32_t argb);
bool start_action_vote(const std::string& text, const std::string& done, std::function<void()> action);

// Phase events of the host. The species is shared: when the host comes back from an editor
// (evolution inside a stage) or starts a new stage, every guest gets the new world at once.
uint32_t g_last_stage = 0;  // last stage the local player played
bool g_was_in_editor = false;

// ---- evolution vote: the host's editor request (next stage or evolution inside a stage) waits
// until every player is ready (or 60 s / "Start now"), then the game goes on ----
struct Vote {
    bool active = false;
    void* request = nullptr; // Editors::EditorRequest*, one reference held
    double t0 = 0;
    std::string text;
    int left = 0;
    uint32_t editor = 0; // the editor the host opens (EditorRequest+0xC); guests open the same for their own creature
};
Vote g_vote;
bool g_vote_bypass = false;
bool g_vote_opened_ui = false; // the vote opened the co-op window: it closes again afterwards
double g_vote_last_send = 0;
using Submit_t = bool(SK_CDECL*)(void*);
Submit_t o_Submit;
void broadcast_vote();
void set_ready(bool on);

std::function<void()> g_vote_action; // a vote for something else than an editor (a superweapon ...)
std::string g_vote_done = "evolution starts";

// a vote whose result runs `action` (outside the frame) when everybody is ready or time is up
bool start_action_vote(const std::string& text, const std::string& done, std::function<void()> action)
{
    if (g_vote.active)
        return false;
    g_vote = Vote{true, nullptr, H->time_seconds(), text, 60, 0u};
    g_vote_action = std::move(action);
    g_vote_done = done;
    for (auto& p : g_sess.players())
        g_ready[p.id] = p.id == 0;
    g_lobby_dirty = true;
    broadcast_vote();
    notice(text + ": press I'm ready (F9)", -1);
    g_vote_opened_ui = !coop_ui::is_open();
    coop_ui::open(true);
    return true;
}

void start_vote(void* req, const std::string& text)
{
    g_vote_action = nullptr;
    g_vote_done = "evolution starts";
    if (req)
        skg::vcall<int>(req, 1); // AddRef: kept until the vote ends
    g_vote = Vote{true, req, H->time_seconds(), text, 60, req ? skg::field<uint32_t>(req, 0xC) : 0u};
    for (auto& p : g_sess.players())
        g_ready[p.id] = p.id == 0;
    g_lobby_dirty = true;
    broadcast_vote();
    notice(text + ": press I'm ready (F9)", -1);
    g_vote_opened_ui = !coop_ui::is_open();
    coop_ui::open(true);
}

// Selecting a creature in the creature stage: for a member of the player's pack (+0xB58 bit 1) the
// game opens the pack panel with data from +0xE84, which our puppets and proxies do not have (the
// game crashed reading (+0xE84)->+0x32C). Such a creature is not a pack member while selected.
typedef void(SK_CDECL* SelectUI_t)(void*, void*, void*, uint32_t, float);
SelectUI_t o_SelectUI;
unsigned g_select_guards; // selections that needed the guard (tests)
void SK_CDECL h_SelectUI(void* sel, void* a2, void* a3, uint32_t a4, float a5)
{
    uint32_t* flags = nullptr;
    void* obj = sel && !IsBadReadPtr(sel, 8) ? *(void**)((uint8_t*)sel + 4) : nullptr;
    void* cr = obj ? skg::vcall<void*>(obj, 0xC / 4, 0xD0036E08u) : nullptr; // cCreatureAnimal
    if (cr && !IsBadReadPtr((uint8_t*)cr + 0xE84, 4) && *(void**)((uint8_t*)cr + 0xE84) == nullptr) {
        flags = (uint32_t*)((uint8_t*)cr + 0xB58);
        if (!(*flags & 1))
            flags = nullptr;
    }
    if (flags) {
        *flags &= ~1u;
        g_select_guards++;
    }
    o_SelectUI(sel, a2, a3, a4, a5);
    if (flags)
        *flags |= 1u;
}

// The own creature usually comes from the Creature Creator (My Creations) and may hold parts that
// are not in the stage editor's part list. The editor then marks that error (bit 5) as blocking
// when it loads the creature, and "done" only shows the error and stays in the editor (cancel
// works). In the own editor such parts are allowed: the bit is dropped from the blocking set
// (docs/re/creations.md 7).
bool g_own_editor = false; // the own editor was requested and the player has not left it yet
using BlockingErrors_t = uint32_t*(SK_THISCALL*)(void*, uint32_t*, void*, void*, int);
BlockingErrors_t o_BlockingErrors;
unsigned g_part_list_waivers; // tests: how often the part-list error was waived
uint32_t* SK_THISCALL h_BlockingErrors(void* ed, uint32_t* out, void* res, void* model, int flag)
{
    uint32_t* r = o_BlockingErrors(ed, out, res, model, flag);
    if (g_own_editor && out && (out[0] & 0x20u)) {
        out[0] &= ~0x20u;
        g_part_list_waivers++;
    }
    return r;
}

// the creature editor for the own creature (EditorRequest + Submit, like ModAPI mods do)
bool open_own_editor(uint32_t editor_id)
{
    uintptr_t ctor = H->sym("EditorRequest_ctor"), submit = H->sym("Editors::EditorRequest::Submit");
    void* av = game::avatar();
    if (!ctor || !submit || !av)
        return false;
    // the game's operator new (0xF47240: size, name, 0, 0, 0, 0), as at 0xD42F1E
    void* req = skg::cdecl_call<void*>(H->rebase(0xF47240), (size_t)0x9C, "EditorRequest", 0, 0, 0, 0);
    skg::thiscall<void*>(ctor, req);
    skg::field<uint32_t>(req, 0xC) = editor_id ? editor_id : 0x465C50BAu; // CreatureEditorExtraLarge
    game::Key k = game::species_key(av);
    skg::field<uint32_t>(req, 0x10) = k.instance;
    skg::field<uint32_t>(req, 0x14) = k.type;
    skg::field<uint32_t>(req, 0x18) = k.group;
    g_vote_bypass = true;
    g_own_editor = true; // the editor loads the creature after Submit, on a later frame
    bool r = skg::cdecl_call<bool>(submit, req);
    g_vote_bypass = false;
    if (!r)
        g_own_editor = false;
    return r;
}

bool SK_CDECL h_Submit(void* req)
{
    uint32_t mode = game::mode_id();
    if (!g_vote_bypass && req && stage_mode(mode) && g_sess.connected() && g_sess.players().size() > 1) {
        if (g_sess.role() == net::Role::Host) {
            if (!g_vote.active)
                start_vote(req, g_name + " wants to evolve (" + modes::mode_name(mode) + ")");
            return false; // runs when the vote is over
        }
        notice(g_name + " evolves its own creature", -1);
        set_ready(true);
    }
    return o_Submit(req);
}

void finish_vote(const char* why)
{
    if (!g_vote.active)
        return;
    void* req = g_vote.request;
    g_vote.active = false;
    broadcast_vote();
    if (g_vote_opened_ui)
        coop_ui::open(false);
    g_vote_opened_ui = false;
    notice(g_vote_done + " (" + why + ")", -1);
    if (g_vote_action) {
        static std::function<void()> run;
        run = std::move(g_vote_action);
        g_vote_action = nullptr;
        H->run_outside_frame([](void*) { run(); }, nullptr);
    }
    if (req) // editors create D3D resources: outside the frame
        H->run_outside_frame(
            [](void* r) {
                g_vote_bypass = true;
                o_Submit(r);
                g_vote_bypass = false;
                skg::vcall<int>(r, 2); // Release
            },
            req);
}

void broadcast_vote()
{
    if (g_sess.role() != net::Role::Host)
        return;
    net::Writer w;
    w.u8(g_vote.active ? 1 : 0);
    w.u8((uint8_t)std::max(0, g_vote.left));
    w.str8(g_vote.text);
    w.u32(g_vote.editor);
    g_sess.broadcast_reliable(net::MSG_VOTE, w);
}

void vote_tick(double now)
{
    if (!g_vote.active || g_sess.role() != net::Role::Host)
        return;
    g_vote.left = (int)(60 - (now - g_vote.t0));
    bool all = true;
    for (auto& p : g_sess.players())
        all &= g_ready.count(p.id) && g_ready[p.id];
    if (all)
        finish_vote("everybody is ready");
    else if (g_vote.left <= 0)
        finish_vote("time is up");
    else if (g_sess.players().size() <= 1)
        finish_vote("alone");
    else if (now - g_vote_last_send > 1.0) {
        g_vote_last_send = now;
        broadcast_vote();
    }
}

void on_mode_change(uint32_t from, uint32_t to)
{
    bool editor = to == modes::kEdit || to == modes::kEditor;
    if (editor && stage_mode(from))
        g_was_in_editor = true;
    if (!editor && (from == modes::kEdit || from == modes::kEditor))
        g_own_editor = false;
    if (!stage_mode(to))
        return;
    bool new_stage = g_last_stage && to != g_last_stage;
    bool evolved = g_was_in_editor && !new_stage;
    g_was_in_editor = false;
    uint32_t last = g_last_stage;
    g_last_stage = to;
    if (g_sess.role() != net::Role::Host || !g_share_world || g_remotes.empty() || !last)
        return;
    if (!new_stage && !evolved)
        return;
    for (auto& [id, r] : g_remotes)
        if (!r.echo)
            r.needs_world = true; // sent by follow_phase as soon as the host controls something
    notice(new_stage ? g_name + " entered the " + modes::mode_name(to) + " stage: everybody follows"
                     : g_name + " evolved the species: everybody gets the new body",
           -1);
}

// The host decides when a phase ends: when the host plays a stage and a guest stays in another
// stage (or its editor) for a few seconds, the host world is sent again and the guest loads it.
void follow_phase(double now)
{
    if (g_sess.role() != net::Role::Host || !g_share_world || !g_adapter || g_adapter->own_worlds ||
        !stage_mode(g_mode))
        return;
    uint64_t key = ((uint64_t)g_mode << 32) | modes::current_world();
    for (auto& [id, r] : g_remotes) {
        if (r.echo)
            continue;
        if (r.needs_world && g_local) { // joined while the host was in a menu
            r.needs_world = false;
            r.world_sent = key;
            r.joined_phase = false;
            std::string err;
            if (world::share_with(id, err, g_save_on_share))
                feed("sending the world to " + r.name);
            else
                feed("cannot share the world: " + err);
            continue;
        }
        if (r.buf.empty())
            continue;
        const auto& ls = r.buf.latest();
        // a guest in an editor is left alone (it may be evolving inside the same stage); a guest
        // that plays another stage than the host is pulled back into the host's stage
        bool behind = ls.mode != g_mode && stage_mode(ls.mode);
        if (!behind) {
            r.other_mode_since = -1;
            if (ls.mode == g_mode)
                r.joined_phase = true;
            continue;
        }
        if (r.other_mode_since < 0)
            r.other_mode_since = now;
        // once per phase, or again when the guest had followed and then left the phase
        if (now - r.other_mode_since < 5.0 || (r.world_sent == key && !r.joined_phase))
            continue;
        r.world_sent = key;
        r.joined_phase = false;
        std::string err;
        if (world::share_with(id, err, g_save_on_share))
            feed("next phase: sending the world to " + r.name);
        else
            feed("cannot share the world: " + err);
    }
}

void send_lobby()
{
    net::Writer w;
    auto pl = g_sess.players();
    w.u8((uint8_t)pl.size());
    for (auto& p : pl) {
        w.u8(p.id);
        w.u8(g_ready[p.id] ? 1 : 0);
        int rtt = p.id == 0 ? 0 : g_sess.rtt_of(p.id);
        w.u16((uint16_t)(rtt < 0 ? 0xFFFF : rtt));
    }
    for (auto& p : pl) // roles after the main table (older guests stop reading before them)
        w.u8(g_roles[p.id]);
    for (auto& p : pl)
        if (p.id != 0)
            g_sess.send_reliable_to(p.id, net::MSG_LOBBY, w);
}

void lobby_tick(double now)
{
    if (g_sess.role() == net::Role::Host) {
        if (g_lobby_dirty || now - g_last_lobby > 1.0) {
            g_lobby_dirty = false;
            g_last_lobby = now;
            send_lobby();
            const char* mode = modes::mode_name(g_mode);
            std::string world = modes::current_world_name();
            if (world.size() > 4 && world.substr(world.size() - 4) == ".spo")
                world.resize(world.size() - 4);
            g_sess.lobby_info = std::string(mode) + (world.empty() || !g_adapter ? "" : " - " + world);
        }
    }
    if (g_disc_on)
        g_disc.update(net::now_ms());
}

// Network keepalive without frames: a modal game dialog (e.g. "game saved") can stop the frames
// while the window still pumps messages; the session and the world transfer then keep running
// from the window procedure, so guests do not time out.
std::atomic<double> g_last_frame{0};
std::atomic<bool> g_keepalive_run{false}, g_keepalive_queued{false};
std::thread g_keepalive;

void keepalive_task(void*)
{
    g_keepalive_queued = false;
    if (H->time_seconds() - g_last_frame < 0.5 || g_sess.role() == net::Role::None)
        return;
    g_sess.update();
    if (g_sess.role() == net::Role::Host)
        world::tick(g_sess);
}

void keepalive_start()
{
    g_keepalive_run = true;
    g_keepalive = std::thread([] {
        while (g_keepalive_run) {
            Sleep(100);
            if (!g_keepalive_queued && H->time_seconds() - g_last_frame > 0.5 && g_last_frame > 0) {
                g_keepalive_queued = true;
                H->run_outside_frame(keepalive_task, nullptr);
            }
        }
    });
}

void keepalive_stop()
{
    g_keepalive_run = false;
    if (g_keepalive.joinable())
        g_keepalive.join();
}

void on_frame(void*, int, void*)
{
    double now = H->time_seconds();
    g_last_frame = now;
    notify::tick(now);
    creations::tick(now);
    g_sess.update();
    lobby_tick(now);
    vote_tick(now);
    coop_ui::tick();
    if (g_sess.role() == net::Role::Host)
        world::tick(g_sess);
    else
        world::guest_tick(); // a received world waiting for the player to leave its editor
    npc::tick(now);
    shared::tick(g_sess, game::mode_id(), now);
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

    uint32_t prev_mode = g_mode;
    g_mode = game::mode_id();
    if (g_mode != prev_mode)
        on_mode_change(prev_mode, g_mode);
    g_adapter = modes::for_mode(g_mode);
    g_world = modes::current_place(g_adapter);
    g_local = g_adapter ? g_adapter->local() : nullptr;
    void* av = game::avatar();
    if (g_local != g_last_avatar) {
        if (g_last_avatar) {
            forget_puppets();
            npc::on_world_change();
            tribe::on_world_change();
        }
        g_last_avatar = g_local;
    }

    follow_phase(now);
    tribe::tick(now);
    cellsync::tick(now);
    civ::tick(now);
    squadron::tick(now);
    autoupdate::tick();
    spacecoop::tick(now);
    if (g_sess.connected() && g_mode == modes::kCreature)
        teamplay::tick(game::avatar(), now);

    if (now - g_last_send >= 0.05) {
        g_last_send = now;
        net::PlayerState st = local_state();
        if (g_sess.connected())
            g_sess.send_state(st);
        if (g_echo && g_local) {
            // fake remote: our own state, shifted sideways, delayed
            st.id = 200;
            st.time_ms = (uint32_t)(now * 1000.0);
            st.mode = g_mode;
            st.world = g_world;
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

    (void)av;
    for (auto& [id, r] : g_remotes) {
        if (!g_local) {
            if (r.puppet)
                forget_puppets();
            continue;
        }
        drive_puppet(r, now);
    }
}

int text_width(const std::string& s) { return (int)s.size() * 9; } // Arial 18 bold, approx.

// world marks: "look here" pings and beacons of the stage modules (a city under attack, a
// partner's ship ...): a label at the world point, an arrow on the screen edge when it is off screen
struct Mark {
    std::string text;
    net::Vec3 pos;
    uint32_t mode;
    double until;
    uint32_t argb;
};
std::vector<Mark> g_marks;

void add_mark(uint8_t from, const net::Vec3& p, uint32_t mode)
{
    g_marks.push_back({"[ ! ] " + g_sess.name_of(from), p, mode, H->time_seconds() + 6.0, 0xFFFFD040});
    feed(g_sess.name_of(from) + " pings a place");
}

void add_beacon(const net::Vec3& p, const std::string& text, double seconds, uint32_t argb)
{
    for (auto& m : g_marks) // the same beacon again: refresh it (it may follow a moving target)
        if (m.text == text || (m.argb == argb && net::dist(m.pos, p) < 1.f && m.text.substr(0, 6) == text.substr(0, 6))) {
            m.text = text;
            m.pos = p;
            m.until = H->time_seconds() + seconds;
            return;
        }
    g_marks.push_back({text, p, g_mode, H->time_seconds() + seconds, argb});
}

void draw_marks()
{
    double now = H->time_seconds();
    int sw = 0, sh = 0;
    H->screen_size(&sw, &sh);
    for (size_t i = 0; i < g_marks.size();) {
        if (g_marks[i].until < now) {
            g_marks.erase(g_marks.begin() + i);
            continue;
        }
        const Mark& m = g_marks[i++];
        if (m.mode != g_mode)
            continue;
        float w[3] = {m.pos.x, m.pos.y, m.pos.z}, sc[2] = {0, 0};
        bool blink = ((int)(now * 4)) & 1;
        uint32_t col = blink ? m.argb : (m.argb & 0xFF000000) | ((m.argb >> 1) & 0x7F7F7F);
        bool front = H->world_to_screen(w, sc) != 0;
        if (front && sc[0] >= 0 && sc[1] >= 0 && sc[0] < sw && sc[1] < sh) {
            H->overlay_text((int)sc[0] - text_width(m.text) / 2, (int)sc[1] - 10, col, m.text.c_str());
            continue;
        }
        if (sw <= 0)
            continue;
        // off screen: an arrow on the edge towards it (behind the camera: at the bottom)
        float cx = sw / 2.f, cy = sh / 2.f;
        float dx = front ? sc[0] - cx : 0.f, dy = front ? sc[1] - cy : 1.f;
        float k = std::min(std::fabs(dx) > 1 ? (cx - 90) / std::fabs(dx) : 1e9f,
                           std::fabs(dy) > 0.5f ? (cy - 60) / std::fabs(dy) : 1e9f);
        if (!front)
            k = cy - 60;
        int x = (int)(cx + dx * k), y = (int)(cy + dy * k);
        const char* arrow = std::fabs(dx) > std::fabs(dy) ? (dx > 0 ? "\u25BA" : "\u25C4") : (dy > 0 ? "\u25BC" : "\u25B2");
        std::string t = std::string(arrow) + " " + m.text;
        H->overlay_text(x - text_width(t) / 2, y - 10, col, t.c_str());
    }
}

// tribe stage: the owner's name over every member of the player tribe (own members in gold)
void draw_owner_tags()
{
    tribe::for_each_owned(
        [](void* c, uint8_t owner, void*) {
            net::Vec3 p = game::position(c);
            float len = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
            float up = len > 1 ? 2.6f / len : 0;
            float w[3] = {p.x + p.x * up, p.y + p.y * up, p.z + p.z * up}, sc[2];
            if (!H->world_to_screen(w, sc))
                return;
            std::string n = owner == g_sess.my_id() ? g_name : g_sess.name_of(owner);
            if (tribe::is_chief(c))
                n = "\u2605 " + n + "'s chief";
            H->overlay_text((int)sc[0] - text_width(n) / 2, (int)sc[1] - 10,
                            owner == g_sess.my_id() ? 0xFFFFD040 : 0xFF80C8FF, n.c_str());
        },
        nullptr);
}

// civilization: owner names over the player's cities and vehicles
void draw_civ_tags()
{
    civ::for_each_owned(
        [](void*, const net::Vec3& p, uint8_t owner, bool city, void*) {
            float len = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
            float up = len > 1 ? (city ? 30.f : 6.f) / len : 0;
            float w[3] = {p.x + p.x * up, p.y + p.y * up, p.z + p.z * up}, sc[2];
            if (!H->world_to_screen(w, sc))
                return;
            std::string n = owner == g_sess.my_id() ? g_name : g_sess.name_of(owner);
            if (city)
                n = "[" + n + "]";
            H->overlay_text((int)sc[0] - text_width(n) / 2, (int)sc[1] - 10,
                            owner == g_sess.my_id() ? 0xFFFFD040 : 0xFF80C8FF, n.c_str());
        },
        nullptr);
}

void on_overlay(void*, int, void*)
{
    if (!g_overlay)
        return;
    int sw = 0, sh = 0;
    H->screen_size(&sw, &sh);
    draw_marks();
    cellsync::draw_overlay();
    spacecoop::draw_overlay();
    draw_owner_tags();
    draw_civ_tags();
    // name tags above remote players
    void* av = g_local;
    if (g_tag_self && av) {
        net::Vec3 p = modes::entity_pos(g_adapter, av);
        float sc[2] = {0, 0}, w[3] = {p.x, p.y, p.z};
        int ok = H->world_to_screen(w, sc);
        g_dbg_sc[0] = sc[0]; g_dbg_sc[1] = sc[1]; g_dbg_sc[2] = (float)ok;
        if (ok)
            H->overlay_text((int)sc[0] - 4, (int)sc[1] - 10, 0xFFFF4040, "+");
    }
    for (auto& [id, r] : g_remotes) {
        if (!r.puppet || !av)
            continue;
        auto* ad = r.puppet_adapter;
        net::Vec3 p = modes::entity_pos(ad, r.puppet);
        float len = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
        float h = ad->tag_height ? ad->tag_height(r.puppet) : 3.f;
        float up = len > 1 ? h / len : 0; // above the head, along the planet normal
        float w[3] = {p.x + p.x * up, p.y + p.y * up, p.z + p.z * up}, sc[2];
        if (ad->flat) {
            w[0] = p.x;
            w[1] = p.y + h; // cell world: screen up is +y on the plane
            w[2] = p.z;
        }
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
    if (g_sess.connected() && g_mode == modes::kCreature) {
        std::string t = teamplay::status();
        if (!t.empty()) {
            H->overlay_text(x, y, 0xFF8CF08C, t.c_str());
            y += 20;
        }
    }
    for (auto& p : g_sess.players()) {
        std::string line = (p.id == g_sess.my_id() ? "> " : "  ") + p.name;
        auto it = g_remotes.find(p.id);
        if (p.id != g_sess.my_id() && it != g_remotes.end() && !it->second.buf.empty()) {
            const auto& ls = it->second.buf.latest();
            line += std::string("  [") + modes::mode_name(ls.mode) + (same_place(ls) || ls.mode != g_mode ? "" : ", other world") + "]";
        }
        H->overlay_text(x, y, 0xFFFFFFFF, line.c_str());
        y += 20;
    }
    // notices are notification panels now (notify.cpp); the history is in the co-op window
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
        chat_log("* disconnected: " + why);
        g_ready.clear();
        g_lobby_rtt.clear();
        npc::on_session_end();
        tribe::on_session_end();
        cellsync::on_session_end();
        creations::on_session_end();
        civ::on_session_end();
        squadron::on_session_end();
        spacecoop::on_session_end();
        for (auto& [id, r] : g_remotes)
            despawn(r);
        g_remotes.clear();
    };
    g_sess.cb.player_join = [](const net::PlayerInfo& p) {
        shared::on_session_change();
        creations::on_player_join(p.id);
        g_dna_sent = -1; // resend the DNA total to the newcomer
        auto& r = g_remotes[p.id];
        r.id = p.id;
        r.name = p.name;
        feed(p.name + " joined the pack");
        chat_log("* " + p.name + " joined");
        g_lobby_dirty = true;
        bool own_worlds = g_adapter && g_adapter->own_worlds;
        bool playing = g_adapter && g_local && stage_mode(g_mode);
        if (g_sess.role() == net::Role::Host && g_share_world && !own_worlds && !playing) {
            // in menus or loading screens there is no world to send yet (the current save name
            // is a stale snapshot): send it when the host starts playing
            r.needs_world = true;
            feed(p.name + " waits for your world: load or start a game");
        } else if (g_sess.role() == net::Role::Host && g_share_world && !own_worlds) {
            std::string err;
            bool save = g_save_on_share && g_adapter; // only while playing, not from menus
            r.world_sent = ((uint64_t)g_mode << 32) | modes::current_world();
            r.joined_phase = false;
            if (!world::share_with(p.id, err, save))
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
        if (type == net::MSG_VOTE && g_sess.role() == net::Role::Client) {
            bool on = r.u8() != 0;
            int left = r.u8();
            std::string text = r.str8();
            if (!r.ok())
                return;
            uint32_t editor = r.left() >= 4 ? r.u32() : 0;
            bool started = on && !g_vote.active, ended = !on && g_vote.active;
            if (on)
                g_vote.editor = editor;
            // the evolution starts: everybody evolves its own creature in its own editor
            if (ended && g_vote.editor && g_mode == modes::kCreature && g_local)
                H->run_outside_frame( // editors create D3D resources: outside the frame
                    [](void* id) {
                        bool ok = open_own_editor((uint32_t)(uintptr_t)id);
                        LOG("own editor %s", ok ? "opened" : "not opened");
                    },
                    (void*)(uintptr_t)g_vote.editor);
            g_vote.active = on;
            g_vote.left = left;
            g_vote.text = text;
            if (started) {
                g_ready[g_sess.my_id()] = false;
                g_vote_opened_ui = !coop_ui::is_open();
                coop_ui::open(true);
            }
            if (ended && g_vote_opened_ui) {
                coop_ui::open(false);
                g_vote_opened_ui = false;
            }
            return;
        }
        if (type == net::MSG_NOTICE) {
            std::string t = r.str8();
            if (!r.ok())
                return;
            if (g_sess.role() == net::Role::Host)
                notice(t, from); // relay to the other guests
            else {
                feed(t);
                chat_log("* " + t);
            }
            return;
        }
        if (type == net::MSG_ROLE && g_sess.role() == net::Role::Host) {
            uint8_t role = r.u8();
            if (r.ok() && role < 4) {
                g_roles[from] = role;
                g_lobby_dirty = true;
            }
            return;
        }
        if (type == net::MSG_READY && g_sess.role() == net::Role::Host) {
            uint8_t on = r.u8();
            if (r.ok()) {
                g_ready[from] = on != 0;
                g_lobby_dirty = true;
            }
            return;
        }
        if (type == net::MSG_LOBBY && g_sess.role() == net::Role::Client) {
            uint8_t n = r.u8();
            std::map<uint8_t, bool> ready;
            std::map<uint8_t, int> rtt;
            for (int i = 0; i < n && r.ok(); i++) {
                uint8_t id = r.u8();
                ready[id] = r.u8() != 0;
                uint16_t t = r.u16();
                rtt[id] = t == 0xFFFF ? -1 : t;
            }
            if (r.ok()) {
                bool mine = g_ready[g_sess.my_id()];
                g_ready = ready;
                g_ready[g_sess.my_id()] = mine; // our own flag is ours until the host confirms it
                g_lobby_rtt = rtt;
                if (r.left() >= ready.size()) {
                    uint8_t my_role = g_roles[g_sess.my_id()];
                    for (auto& [id, on] : ready)
                        g_roles[id] = r.u8();
                    g_roles[g_sess.my_id()] = my_role;
                }
            }
            return;
        }
        if (type == net::MSG_RES || type == net::MSG_RES_ADD) {
            shared::on_message(g_sess, type, from, r);
            return;
        }
        if (type == net::MSG_PING_MARK) { // u8 origin, vec3, u32 mode
            uint8_t origin = r.u8();
            net::Vec3 p;
            p.x = r.f32(); p.y = r.f32(); p.z = r.f32();
            uint32_t mode = r.u32();
            if (!r.ok())
                return;
            if (g_sess.role() == net::Role::Host) { // relay to the other guests
                origin = from;
                net::Writer w;
                w.u8(origin);
                w.f32(p.x); w.f32(p.y); w.f32(p.z); w.u32(mode);
                g_sess.broadcast_reliable(net::MSG_PING_MARK, w, from);
            }
            add_mark(origin, p, mode);
            return;
        }
        if (type == net::MSG_SPACE) {
            spacecoop::on_message(type, from, r);
            return;
        }
        if (type >= net::MSG_UFO_STATE && type <= net::MSG_UFO_DAMAGE) {
            squadron::on_message(type, from, r);
            return;
        }
        if ((type >= net::MSG_CIV_STATE && type <= net::MSG_CIV_BUILD) || type == net::MSG_CIV_ALARM) {
            civ::on_message(type, from, r);
            return;
        }
        if (type == net::MSG_CREATION) {
            creations::on_message(type, from, r);
            return;
        }
        if ((type >= net::MSG_CELL_STATE && type <= net::MSG_CELL_FOOD) || type == net::MSG_CELL_KO) {
            cellsync::on_message(type, from, r);
            return;
        }
        if (type == net::MSG_TRIBE_STATE || type == net::MSG_TRIBE_CMD) {
            tribe::on_message(type, from, r);
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
        chat_log("* " + it->second.name + " left");
        g_ready.erase(id);
        g_lobby_dirty = true;
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
    g_sess.cb.chat = [](uint8_t id, const std::string& t) {
        feed("<" + g_sess.name_of(id) + "> " + t);
        chat_log(g_sess.name_of(id) + ": " + t);
    };
}

// ---------------- actions (commands and UI) ----------------
uint16_t default_port() { return (uint16_t)(net::kDefaultPort + H->instance_id() - 1); }

void reset_lobby()
{
    g_ready.clear();
    g_lobby_rtt.clear();
    g_chat.clear();
}

bool do_host(uint16_t port, std::string& err)
{
    reset_lobby();
    if (!g_sess.host(port, g_name, err))
        return false;
    feed("hosting on port " + std::to_string(g_sess.port()));
    chat_log("* hosting on UDP port " + std::to_string(g_sess.port()));
    g_lobby_dirty = true;
    return true;
}

bool do_join(const std::string& addr, std::string& err)
{
    reset_lobby();
    if (!g_sess.join(addr, g_name, err))
        return false;
    chat_log("* joining " + addr);
    return true;
}

void do_leave()
{
    g_sess.leave("user");
    npc::on_session_end();
    spacecoop::on_session_end();
    for (auto& [id, r] : g_remotes)
        despawn(r);
    g_remotes.clear();
    reset_lobby();
}

const char* role_name(uint8_t r)
{
    return r == 1 ? "military" : r == 2 ? "economic" : r == 3 ? "religious" : "none";
}

void set_role(uint8_t role)
{
    g_roles[g_sess.my_id()] = role;
    if (g_sess.role() == net::Role::Client) {
        net::Writer w;
        w.u8(role);
        g_sess.send_reliable_to(0, net::MSG_ROLE, w);
    } else
        g_lobby_dirty = true;
    notice(g_name + " governs as " + std::string(role_name(role)) + " now", -1);
}

void set_ready(bool on)
{
    g_ready[g_sess.my_id()] = on;
    if (g_sess.role() == net::Role::Client) {
        net::Writer w;
        w.u8(on ? 1 : 0);
        g_sess.send_reliable_to(0, net::MSG_READY, w);
    } else
        g_lobby_dirty = true;
}

int share_all()
{
    int n = 0;
    if (!(g_adapter && g_local)) { // not playing: no world yet, send it once the host plays
        for (auto& [id, r] : g_remotes)
            if (!r.echo)
                r.needs_world = true;
        feed("the world goes to everyone when you load or start a game");
        return 0;
    }
    for (auto& p : g_sess.players()) {
        std::string err;
        if (p.id != 0 && world::share_with(p.id, err, g_save_on_share && g_adapter))
            n++;
        else if (p.id != 0)
            feed("cannot share the world: " + err);
    }
    return n;
}

// ---------------- commands ----------------
int c_host(void*, int argc, const char* const* argv, SkOut* out)
{
    uint16_t port = argc > 1 ? (uint16_t)atoi(argv[1]) : default_port();
    if (argc > 2)
        g_name = argv[2];
    std::string err;
    if (!do_host(port, err)) {
        SAY("host failed: %s", err.c_str());
        return 1;
    }
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
    if (!do_join(argv[1], err)) {
        SAY("join failed: %s", err.c_str());
        return 1;
    }
    SAY("joining %s as '%s'", argv[1], g_name.c_str());
    return 0;
}

int c_leave(void*, int, const char* const*, SkOut* out)
{
    do_leave();
    SAY("left session");
    return 0;
}

int c_status(void*, int, const char* const*, SkOut* out)
{
    SAY("%s%s", g_sess.status().c_str(), world::status().c_str());
    SAY("remote animations: received %u, played %u\n", g_anims_rx, g_anims_played);
    SAY("%s", npc::status().c_str());
    SAY("%s", tribe::status().c_str());
    SAY("%s", cellsync::status().c_str());
    SAY("%s", notify::status().c_str());
    SAY("%s", creations::status().c_str());
    SAY("%s", civ::status().c_str());
    SAY("%s", squadron::status().c_str());
    SAY("%s", spacecoop::status().c_str());
    SAY("dna=%.1f (rx %u totals, forwarded %u gains)\n", game::dna(), g_dna_rx, g_dna_fwd);
    for (size_t i = g_chat.size() > 6 ? g_chat.size() - 6 : 0; i < g_chat.size(); i++)
        SAY("chat: %s\n", g_chat[i].c_str());
    void* av = g_local;
    SAY("mode=0x%08X local=%p overlay-self=(%.1f %.1f ok=%.0f)\n", game::mode_id(), av, g_dbg_sc[0], g_dbg_sc[1], g_dbg_sc[2]);
    double now = H->time_seconds();
    for (auto& [id, r] : g_remotes) {
        net::PlayerState s;
        bool have = r.buf.sample(now, s);
        SAY("remote %u '%s' puppet=%p state=%s age=%.2fs pos=(%.1f %.1f %.1f)\n", id, r.name.c_str(),
            r.puppet, have ? "yes" : "no", r.buf.age(now), s.pos.x, s.pos.y, s.pos.z);
        // tests parse the "puppet pos" line as the one right after the "remote" line: keep it there
        if (r.puppet) {
            net::Vec3 p = modes::entity_pos(r.puppet_adapter, r.puppet);
            float rot_err = -1;
            if (g_mode == modes::kCell) { // facing error in degrees
                net::Quat q;
                modes::mat_to_quat((float*)((uint8_t*)r.puppet + 0x5C), q);
                float dot = std::fabs(q.x * s.rot.x + q.y * s.rot.y + q.z * s.rot.z + q.w * s.rot.w);
                rot_err = 2.f * std::acos(std::min(1.f, dot)) * 57.2958f;
            }
            SAY("   puppet pos=(%.1f %.1f %.1f) err=%.2f rot_err=%.1f species=%08X\n", p.x, p.y, p.z,
                net::dist(p, s.pos), rot_err,
                g_mode == modes::kCreature ? game::species_key(r.puppet).instance
                : g_mode == modes::kCell   ? *(uint32_t*)((uint8_t*)r.puppet + 0xFC) // cell model
                                           : 0u);
        }
        // tests: both players are in the same world and place right now
        SAY("together %u %d\n", id, !r.buf.empty() && r.buf.age(now) < 3.0 && same_place(r.buf.latest()) ? 1 : 0);
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
    void* av = g_local;
    if (!av) {
        SAY("nothing controlled (mode 0x%08X %s, adapter %s)", game::mode_id(), modes::mode_name(game::mode_id()),
            g_adapter ? g_adapter->name : "none");
        return 1;
    }
    auto st = local_state();
    SAY("%s: local=%p place=0x%08X world '%s'\n", g_adapter->name, av, g_world, modes::current_world_name().c_str());
    SAY("species=%p key=0x%08X\npos=(%.2f %.2f %.2f) |r|=%.1f\nrot=(%.3f %.3f %.3f %.3f)\n"
        "vel=(%.2f %.2f %.2f)\nhp=%.1f/%.1f flags=0x%02X",
        g_adapter->mode == modes::kSpace ? nullptr : game::species_profile(av), st.species, st.pos.x, st.pos.y, st.pos.z,
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

int c_res(void*, int argc, const char* const* argv, SkOut* out)
{
    if (argc >= 4 && !strcmp(argv[1], "add")) {
        std::string err;
        if (!shared::debug_add(argv[2], atof(argv[3]), err)) {
            SAY("%s", err.c_str());
            return 1;
        }
    }
    SAY("%s", shared::status().c_str());
    return 0;
}

// coop.ping: mark the place under the mouse (RTS stages) or where you stand, for everybody
int c_ping(void*, int, const char* const*, SkOut* out)
{
    if (!g_local || g_sess.role() == net::Role::None) {
        SAY("play and join a session first");
        return 1;
    }
    net::PlayerState st = local_state();
    net::Writer w;
    w.u8(g_sess.my_id());
    w.f32(st.pos.x); w.f32(st.pos.y); w.f32(st.pos.z); w.u32(g_mode);
    if (g_sess.role() == net::Role::Host)
        g_sess.broadcast_reliable(net::MSG_PING_MARK, w);
    else
        g_sess.send_reliable_to(0, net::MSG_PING_MARK, w);
    add_mark(g_sess.my_id(), st.pos, g_mode);
    SAY("pinged (%.1f %.1f %.1f)", st.pos.x, st.pos.y, st.pos.z);
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
    if (argc > 3 && !strcmp(argv[1], "hosthit")) { // host avatar hits host NPC #id
        bool ok = npc::debug_host_hit((uint16_t)atoi(argv[2]), (float)atof(argv[3]));
        SAY(ok ? "host hit applied" : "no such npc");
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

// ---------------- UI ----------------
std::vector<coop_ui::PlayerRow> ui_players()
{
    std::vector<coop_ui::PlayerRow> v;
    bool host = g_sess.role() == net::Role::Host;
    for (auto& p : g_sess.players()) {
        coop_ui::PlayerRow r;
        r.id = p.id;
        r.name = p.name;
        r.host = p.id == 0;
        r.me = p.id == g_sess.my_id();
        r.ready = g_ready.count(p.id) && g_ready[p.id];
        if (r.me)
            r.stage = modes::mode_name(g_mode);
        else {
            auto it = g_remotes.find(p.id);
            if (it != g_remotes.end() && !it->second.buf.empty()) {
                const auto& ls = it->second.buf.latest();
                r.stage = modes::mode_name(ls.mode);
                if (ls.mode == g_mode && !same_place(ls) && g_adapter)
                    r.stage += ", other world";
            } else
                r.stage = "...";
        }
        if (!r.me)
            r.ping_ms = host || p.id == 0 ? g_sess.rtt_of(p.id) : (g_lobby_rtt.count(p.id) ? g_lobby_rtt[p.id] : -1);
        v.push_back(r);
    }
    return v;
}

std::string ui_session_line()
{
    char b[256];
    if (g_sess.role() == net::Role::Host)
        snprintf(b, sizeof b, "Hosting on UDP port %u  -  %d/%d players", g_sess.port(), (int)g_sess.players().size(),
                 net::kMaxPlayers);
    else if (g_sess.connected())
        snprintf(b, sizeof b, "Connected to %s (%s)", g_sess.name_of(0).c_str(), g_sess.host_address().c_str());
    else
        snprintf(b, sizeof b, "Connecting to %s ...", g_sess.host_address().c_str());
    return b;
}

std::string ui_world_line()
{
    if (g_vote.active) {
        int ready = 0, n = 0;
        for (auto& p : g_sess.players()) {
            n++;
            ready += g_ready.count(p.id) && g_ready[p.id];
        }
        char b[200];
        snprintf(b, sizeof b, "%s - ready %d/%d - %d s", g_vote.text.c_str(), ready, n, g_vote.left);
        return b;
    }
    uint32_t got = 0, total = 0;
    if (g_sess.role() == net::Role::Client && world::progress(got, total) && total > 0) {
        char b[160];
        snprintf(b, sizeof b, "Receiving %s's world: %u%%", g_sess.name_of(0).c_str(),
                 (unsigned)((uint64_t)got * 100 / total));
        return b;
    }
    // the host is evolving in an editor: everybody gets the new body afterwards
    uint32_t host_mode = g_sess.role() == net::Role::Host ? g_mode
                         : (g_remotes.count(0) && !g_remotes[0].buf.empty() ? g_remotes[0].buf.latest().mode : 0);
    if (host_mode == modes::kEdit || host_mode == modes::kEditor)
        return g_sess.name_of(0) + " is evolving the species in the editor: everybody gets the new body";
    if (g_sess.role() == net::Role::Host && g_sess.players().size() > 1 && !(g_adapter && g_local))
        return "Load or start a game: the guests get your world automatically";
    if (g_sess.role() == net::Role::Client && g_sess.connected() && !g_adapter)
        return "Waiting for the host's world...";
    return "";
}

std::vector<std::pair<std::string, net::Vec3>> partners()
{
    std::vector<std::pair<std::string, net::Vec3>> v;
    double now = H->time_seconds();
    for (auto& [id, r] : g_remotes) {
        if (r.echo || r.buf.empty() || r.buf.age(now) > 3.0)
            continue;
        const auto& s = r.buf.latest();
        if ((s.flags & net::PF_IN_WORLD) && !(s.flags & net::PF_DEAD) && same_place(s))
            v.push_back({r.name, s.pos});
    }
    return v;
}

int c_team(void*, int argc, const char* const* argv, SkOut* out)
{
    if (argc > 1 && !strcmp(argv[1], "point")) { // tests: where the first partner is on the screen
        for (auto& [id, r] : g_remotes)
            if (r.puppet && !r.echo) {
                net::Vec3 p = modes::entity_pos(r.puppet_adapter, r.puppet);
                float w[3] = {p.x, p.y, p.z}, sc[2] = {0, 0};
                int ok = H->world_to_screen(w, sc);
                uint8_t* cr = (uint8_t*)r.puppet;
                SAY("point %d %d %d flags=%08X e84=%p guards=%u", ok, (int)sc[0], (int)sc[1],
                    g_mode == modes::kCreature ? *(uint32_t*)(cr + 0xB58) : 0u,
                    g_mode == modes::kCreature ? *(void**)(cr + 0xE84) : nullptr, g_select_guards);
                return 0;
            }
        SAY("no partner");
        return 1;
    }
    if (argc > 2 && !strcmp(argv[1], "hp") && g_local && g_mode == modes::kCreature) { // tests
        game::set_health(g_local, (float)atof(argv[2]));
        SAY("hp set");
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "come")) { // tests: our creature next to the first partner
        for (auto& [id, r] : g_remotes)
            if (r.puppet && g_local && g_mode == modes::kCreature) {
                net::Vec3 p = modes::entity_pos(r.puppet_adapter, r.puppet);
                net::Vec3 me = modes::entity_pos(g_adapter, g_local);
                float d = net::dist(p, me);
                // 3 m from the partner, on our side of it
                net::Vec3 at = d > 0.1f ? net::Vec3{p.x + (me.x - p.x) * 3.f / d, p.y + (me.y - p.y) * 3.f / d,
                                                    p.z + (me.z - p.z) * 3.f / d}
                                        : p;
                game::teleport(g_local, at, net::Quat{0, 0, 0, 1});
                SAY("moved next to %s (%.1f m away before)", r.name.c_str(), d);
                return 0;
            }
        SAY("no partner");
        return 1;
    }
    if (argc > 1)
        teamplay::set_enabled(strcmp(argv[1], "off") != 0);
    SAY("teamplay %s\n%s", teamplay::enabled() ? "on" : "off", teamplay::status().c_str());
    return 0;
}

// the stage action of the co-op window: give the selected own units to a partner
uint8_t give_target()
{
    for (auto& p : g_sess.players())
        if (p.id != g_sess.my_id())
            return p.id;
    return 0xFF;
}

int give_selected_to(uint8_t to)
{
    if (to == 0xFF)
        return 0;
    if (g_mode == modes::kTribe)
        return tribe::give_selected(to);
    if (g_mode == modes::kCiv)
        return civ::give_selected(to);
    if (g_mode == modes::kSpace)
        return spacecoop::give_spice(10);
    return 0;
}

// tests (chaos bot): what can be clicked right now: "kind id owner sx sy" per visible unit
std::string targets()
{
    std::string out;
    char b[96];
    auto add = [&](const char* kind, int id, int owner, const net::Vec3& p) {
        float w[3] = {p.x, p.y, p.z}, sc[2];
        if (!H->world_to_screen(w, sc))
            return;
        snprintf(b, sizeof b, "%s %d %d %d %d\n", kind, id, owner, (int)sc[0], (int)sc[1]);
        out += b;
    };
    for (auto& [id, r] : g_remotes)
        if (r.puppet && r.puppet_adapter)
            add("player", id, id, modes::entity_pos(r.puppet_adapter, r.puppet));
    if (g_local && g_adapter)
        add("me", g_sess.my_id(), g_sess.my_id(), modes::entity_pos(g_adapter, g_local));
    struct U {
        std::string* out;
        decltype(add)* f;
    } u{&out, &add};
    tribe::for_each_owned(
        [](void* c, uint8_t owner, void* p) { (*((U*)p)->f)("member", -1, owner, game::position(c)); }, &u);
    civ::for_each_owned(
        [](void*, const net::Vec3& pos, uint8_t owner, bool city, void* p) {
            (*((U*)p)->f)(city ? "city" : "vehicle", -1, owner, pos);
        },
        &u);
    return out;
}

void init_ui()
{
    coop_ui::Api a;
    a.name = [] { return g_name; };
    a.set_name = [](const std::string& n) { g_name = n; };
    a.join = do_join;
    a.host = do_host;
    a.leave = do_leave;
    a.role = [] { return g_sess.role() == net::Role::Host ? 1 : g_sess.role() == net::Role::Client ? 2 : 0; };
    a.connected = [] { return g_sess.connected(); };
    a.session_line = ui_session_line;
    a.give = [](std::string& what, std::string& to) {
        uint8_t t = give_target();
        if (!g_sess.connected() || t == 0xFF)
            return 0;
        to = g_sess.name_of(t);
        int n = 0;
        if (g_mode == modes::kTribe) {
            n = tribe::own_selected();
            what = "member";
        } else if (g_mode == modes::kCiv) {
            bool city = false;
            n = civ::own_selected(&city);
            what = city ? "city" : "vehicle";
        } else if (g_mode == modes::kSpace) {
            std::string sto;
            n = spacecoop::spice(sto);
            if (n < 0)
                return 0;
            what = "spice";
        }
        return n;
    };
    a.jump = [](std::string& to) { return g_sess.connected() && spacecoop::can_jump(to); };
    a.update = autoupdate::ui;
    a.update_do = autoupdate::start_update;
    a.jump_do = [] {
        H->run_outside_frame(
            [](void*) {
                std::string e = spacecoop::jump(0xFF);
                if (!e.empty())
                    notice("can't jump: " + e, -1);
            },
            nullptr);
    };
    a.give_do = [] { give_selected_to(give_target()); };
    a.civ_role = [] { return (int)g_roles[g_sess.my_id()]; };
    a.civ_role_cycle = [](int d) { set_role((uint8_t)(((int)g_roles[g_sess.my_id()] + d + 4) % 4)); };
    a.creation = [](int& kind, std::string& name, int& own) {
        if (!g_sess.connected() || !(g_mode == modes::kCreature || g_mode == modes::kCell || g_mode == modes::kSpace))
            return false;
        creations::Kind k = g_mode == modes::kCell ? creations::kCell
                            : g_mode == modes::kSpace ? creations::kUfo : creations::kCreature;
        creations::Option o = creations::mine(k);
        kind = k;
        name = o.name;
        own = (int)creations::options(k).size();
        return true;
    };
    a.creation_cycle = [](int dir) {
        creations::Kind k = g_mode == modes::kCell ? creations::kCell : creations::kCreature;
        if (g_mode == modes::kSpace)
            return; // the ship is the one of the own empire
        creations::cycle(k, dir);
    };
    a.players = ui_players;
    a.world_line = ui_world_line;
    a.ready = [] { return g_ready.count(g_sess.my_id()) && g_ready[g_sess.my_id()]; };
    a.set_ready = set_ready;
    a.share_world = [] {
        if (g_vote.active)
            finish_vote("the host started it");
        else
            share_all();
    };
    a.vote_active = [] { return g_vote.active; };
    a.say = [](const std::string& t) {
        if (g_sess.connected())
            g_sess.send_chat(t);
    };
    a.chat = [] { return std::vector<std::string>(g_chat.begin(), g_chat.end()); };
    a.lan_games = [] {
        std::vector<coop_ui::LanGame> v;
        for (auto& g : g_disc.games(net::now_ms()))
            v.push_back({g.info.name, g.host.str(), g.info.info, g.info.players, g.info.max_players});
        return v;
    };
    a.lan_scan = lan_scan;
    a.local_addresses = [] {
        std::vector<std::string> v;
        for (auto& l : local_addresses())
            v.push_back(l.ip);
        return v;
    };
    a.mode = [] { return game::mode_id(); };
    a.default_port = default_port();
    coop_ui::init(H, M, std::move(a));
}

int c_uistate(void*, int argc, const char* const* argv, SkOut* out)
{
    if (argc > 1 && !strcmp(argv[1], "log")) {
        coop_ui::g_log_msgs = !coop_ui::g_log_msgs;
        SAY("ui message log %s", coop_ui::g_log_msgs ? "on" : "off");
        return 0;
    }
    if (argc > 1)
        coop_ui::open(atoi(argv[1]) != 0);
    SAY("%s", coop_ui::debug_state().c_str());
    return 0;
}

void on_unload()
{
    keepalive_stop();
    notify::shutdown();
    coop_ui::shutdown();
    lan_scan(false);
    g_sess.leave("mod unload");
    npc::on_session_end();
    for (auto& [id, r] : g_remotes)
        despawn(r);
    g_remotes.clear();
}

SkModInfo g_info = {SK_ABI_VERSION, "coop", COOP_VERSION, on_unload};

} // namespace

extern "C" SK_EXPORT const SkModInfo* sporekit_mod_main(const SkHost* host, SkMod* self)
{
    H = host;
    M = self;
    g_host_for_modes = host;
    g_mod_for_modes = self;
    if (!game::init(H, M)) {
        H->log(M, SK_ERROR, "required game symbols missing - coop disabled");
        return nullptr;
    }
    char nm[64];
    H->config_str(M, "coop", "name", "Player", nm, sizeof nm);
    g_name = nm;
    g_share_world = H->config_int(M, "coop", "share_world", 1) != 0;
    g_save_on_share = H->config_int(M, "coop", "save_before_share", 1) != 0;
    world::init(H, M, feed);
    shared::init(H, M);
    world::set_auto_load(H->config_int(M, "coop", "auto_load_world", 1) != 0);
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
    ctx.stage_center = [](net::Vec3& p) { return tribe::center(p); };
    npc::init(H, M, ctx);
    npc::set_enabled(H->config_int(M, "coop", "share_npcs", 1) != 0);
    game::set_damage_filter(npc::damage_filter);
    setup_callbacks();
    H->subscribe(M, SK_EV_FRAME, on_frame, nullptr);
    keepalive_start();
    H->subscribe(M, SK_EV_OVERLAY, on_overlay, nullptr);
    const int MT = SK_CMD_MAIN_THREAD;
    H->command_add(M, "coop.host", "coop.host [port] [name] - host a session", c_host, nullptr, MT);
    H->command_add(M, "coop.join", "coop.join <ip[:port]> [name] - join a host", c_join, nullptr, MT);
    H->command_add(M, "coop.leave", "leave the session", c_leave, nullptr, MT);
    H->command_add(M, "coop.targets", "coop.targets - clickable units on screen: kind id owner x y (tests)",
                   [](void*, int, const char* const*, SkOut* out) {
                       SAY("%s", targets().c_str());
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.status", "session, players and puppets", c_status, nullptr, MT);
    H->command_add(M, "coop.name", "coop.name <nick>", c_name, nullptr, MT);
    H->command_add(M, "coop.say", "coop.say <text> - chat", c_say, nullptr, MT);
    H->command_add(M, "coop.avatar", "print local avatar state", c_avatar, nullptr, MT);
    H->command_add(M, "coop.echo", "coop.echo [on|off] [offset] [delay] - loopback puppet test", c_echo, nullptr, MT);
    H->command_add(M, "coop.overlay", "coop.overlay on|off|self", c_overlay, nullptr, MT);
    H->command_add(M, "coop.npc", "coop.npc [on|off|list|hit <id> <dmg>] - host-authoritative NPC sync", c_npc, nullptr, MT);
    H->command_add(M, "coop.res", "coop.res [add <food|money> <n>] - shared resources", c_res, nullptr, MT);
    H->command_add(M, "coop.ping", "coop.ping - show everybody the place under your mouse / where you are", c_ping,
                   nullptr, MT);
    H->command_add(M, "coop.share", "coop.share [player id] - (re)send the host world (save) to guests", c_share,
                   nullptr, MT);
    coop_register_debug_commands(H, M);
    teamplay::Ctx tc;
    tc.partners = partners;
    tc.notice = [](const std::string& t) { notice(t, -1); };
    tc.my_name = [] { return g_name; };
    teamplay::init(H, M, std::move(tc));
    if (uintptr_t a = H->sym("Simulator::CreatureSelectionUI"))
        H->hook_create(M, (void*)a, (void*)h_SelectUI, (void**)&o_SelectUI);
    if (uintptr_t a = H->sym("Editors::EditorRequest::Submit"))
        H->hook_create(M, (void*)a, (void*)h_Submit, (void**)&o_Submit);
    if (uintptr_t a = H->sym("Editors::cEditor::GetBlockingErrors"))
        H->hook_create(M, (void*)a, (void*)h_BlockingErrors, (void**)&o_BlockingErrors);
    H->command_add(M, "coop.vote", "coop.vote [test|go] - evolution vote (test: a vote without an editor)",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       if (argc > 1 && !strcmp(argv[1], "test") && g_sess.role() == net::Role::Host && !g_vote.active) {
                           start_vote(nullptr, g_name + " wants to evolve (test)");
                       } else if (argc > 1 && !strcmp(argv[1], "editor")) {
                           // a request through the vote (the hook), like the game's own editor entry
                           uintptr_t ctor = H->sym("EditorRequest_ctor"), submit = H->sym("Editors::EditorRequest::Submit");
                           void* av = game::avatar();
                           if (!ctor || !submit || !av) {
                               SAY("not possible here");
                               return 1;
                           }
                           void* req = skg::cdecl_call<void*>(H->rebase(0xF47240), (size_t)0x9C, "EditorRequest", 0, 0, 0, 0);
                           skg::thiscall<void*>(ctor, req);
                           skg::field<uint32_t>(req, 0xC) = argc > 2 ? (uint32_t)strtoul(argv[2], nullptr, 16) : 0x465C50BAu;
                           game::Key k = game::species_key(av);
                           skg::field<uint32_t>(req, 0x10) = k.instance;
                           skg::field<uint32_t>(req, 0x14) = k.type;
                           skg::field<uint32_t>(req, 0x18) = k.group;
                           bool r = skg::cdecl_call<bool>(submit, req);
                           SAY("editor request submitted -> %d\n", (int)r);
                       } else if (argc > 1 && !strcmp(argv[1], "go"))
                           finish_vote("the host started it");
                       SAY("vote %s: %s (%d s)\nown editor %d, part list waived %u", g_vote.active ? "active" : "none",
                           g_vote.text.c_str(), g_vote.left, (int)g_own_editor, g_part_list_waivers);
                       return 0;
                   },
                   nullptr, MT);
    tribe::Ctx trc;
    trc.sess = &g_sess;
    trc.feed = feed;
    trc.notice = [](const std::string& t) { notice(t, -1); };
    tribe::init(H, M, std::move(trc));
    notify::init(H, M, [] { return coop_ui::text_font(); });
    creations::Ctx crc;
    crc.sess = &g_sess;
    crc.feed = feed;
    crc.name_of = [](uint8_t id) { return g_sess.name_of(id); };
    crc.cell_rebuilt = [] { cellsync::join_partner(); };
    crc.appearance_changed = [](uint8_t id) { // rebuild that player's puppet with its own look
        auto it = g_remotes.find(id);
        if (it != g_remotes.end())
            despawn(it->second);
    };
    creations::init(H, M, std::move(crc));
    modes::g_species_for_player = [](uint8_t id) { return creations::player_species(id); };
    modes::g_model_for_player = [](uint8_t id, int kind, uint32_t key[3]) {
        creations::Key k = creations::player_key(id, (creations::Kind)kind);
        key[0] = k.instance; key[1] = k.type; key[2] = k.group;
        return k.instance != 0;
    };
    H->command_add(M, "coop.role", "coop.role [0-3] - civilization role: 0 none, 1 military, 2 economic, 3 religious",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       if (argc > 1)
                           set_role((uint8_t)(atoi(argv[1]) & 3));
                       for (auto& p : g_sess.players())
                           SAY("role %u %s %s\n", p.id, p.name.c_str(), role_name(g_roles[p.id]));
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.give", "coop.give [player id] - give the selected own tribe members / vehicles (or the opened city)",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       uint8_t to = argc > 1 ? (uint8_t)atoi(argv[1]) : give_target();
                       SAY("given: %d", give_selected_to(to));
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.creations", "coop.creations [list | next|prev <creature|cell|ufo> | choose <kind> <hex>]",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       SAY("%s", creations::debug(std::vector<std::string>(argv + 1, argv + argc)).c_str());
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.sync", "coop.sync - pose of every synchronized object (key x y z qx qy qz qw), tests",
                   [](void*, int, const char* const*, SkOut* out) {
                       std::vector<syncdbg::Entry> v;
                       auto player = [&](uint8_t id, const modes::Adapter* a, void* e) {
                           if (!a || !e)
                               return;
                           net::PlayerState st;
                           a->fill(e, st);
                           v.push_back({"p" + std::to_string(id), st.pos, st.rot});
                       };
                       if (g_sess.connected()) {
                           player(g_sess.my_id(), g_adapter, g_local);
                           for (auto& [id, r] : g_remotes)
                               if (!r.echo)
                                   player(id, r.puppet_adapter, r.puppet);
                       }
                       for (auto* f : {npc::sync_entries, tribe::sync_entries, civ::sync_entries,
                                       squadron::sync_entries, cellsync::sync_entries})
                           for (auto& e : f())
                               v.push_back(e);
                       for (auto& e : v) // one line per call: the console buffer of a call is 8 KB
                           SAY("%s", syncdbg::format({e}).c_str());
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.notify", "coop.notify <text> - show a co-op notification (game event log)",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       std::string t;
                       for (int i = 1; i < argc; i++)
                           t += (i > 1 ? " " : "") + std::string(argv[i]);
                       feed(t);
                       SAY("%s", notify::status().c_str());
                       return 0;
                   },
                   nullptr, MT);
    cellsync::Ctx cc;
    cc.sess = &g_sess;
    cc.partners = partners;
    cc.notice = [](const std::string& t) { notice(t, -1); };
    cc.my_name = [] { return g_name; };
    cc.puppet_owner = [](void* cell) -> int {
        for (auto& [id, r] : g_remotes)
            if (r.puppet == cell && !r.echo)
                return id;
        return -1;
    };
    cc.puppets = [] {
        std::vector<cellsync::Ctx::Puppet> v;
        if (g_mode != modes::kCell)
            return v;
        for (auto& [id, r] : g_remotes)
            if (r.puppet && !r.echo)
                v.push_back({id, r.name, r.puppet});
        return v;
    };
    cellsync::init(H, M, std::move(cc));
    H->command_add(M, "coop.pool", "coop.pool [bite | food <n> | hp <n> | hurt <player> <dmg>] - cell co-op",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       SAY("%s", cellsync::debug(std::vector<std::string>(argv + 1, argv + argc)).c_str());
                       return 0;
                   },
                   nullptr, MT);
    civ::Ctx cvc;
    cvc.sess = &g_sess;
    cvc.feed = feed;
    cvc.notice = [](const std::string& t) { notice(t, -1); };
    cvc.role_of = [](uint8_t id) { return g_roles.count(id) ? g_roles[id] : (uint8_t)0; };
    cvc.vote = [](const std::string& t, const std::string& d, std::function<void()> a) {
        return start_action_vote(t, d, std::move(a));
    };
    cvc.beacon = add_beacon;
    civ::init(H, M, std::move(cvc));
    squadron::Ctx sqc;
    sqc.sess = &g_sess;
    sqc.with_host = [] {
        auto it = g_remotes.find(0);
        return it != g_remotes.end() && !it->second.buf.empty() && same_place(it->second.buf.latest());
    };
    sqc.puppet_owner = [](void* u) -> int {
        for (auto& [id, r] : g_remotes)
            if (r.puppet == u && !r.echo)
                return id;
        return -1;
    };
    sqc.puppet_of = [](uint8_t id) -> void* {
        auto it = g_remotes.find(id);
        return it == g_remotes.end() ? nullptr : it->second.puppet;
    };
    squadron::init(H, M, std::move(sqc));
    spacecoop::Ctx spc;
    spc.sess = &g_sess;
    spc.notice = [](const std::string& t) { // every machine words its own messages (MSG_SPACE)
        feed(t);
        chat_log("* " + t);
    };
    spc.my_place = [] { return g_mode == modes::kSpace ? g_world : 0u; };
    spc.partners = [] {
        std::vector<spacecoop::Partner> v;
        for (auto& [id, r] : g_remotes) {
            if (r.echo || r.buf.empty())
                continue;
            const net::PlayerState& s = r.buf.latest();
            if (s.mode != modes::kSpace)
                continue;
            v.push_back({id, r.name, s.world, s.pos, r.puppet});
        }
        return v;
    };
    spc.beacon = add_beacon;
    spacecoop::init(H, M, std::move(spc));
    autoupdate::set_feed(feed);
    autoupdate::init(H, M, COOP_VERSION);
    H->command_add(M, "coop.update", "coop.update [check | apply] - SporeCoop updates from GitHub releases",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       SAY("%s", autoupdate::command(argc > 1 ? argv[1] : "").c_str());
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.space",
                   "coop.space [hit <dmg> | hp | jump [id] | give <n> | spice [add <n>] | terra <a> <w> <t> | planet]"
                   " - space co-op",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       SAY("%s", spacecoop::debug(std::vector<std::string>(argv + 1, argv + argc)).c_str());
                       return 0;
                   },
                   nullptr, SK_CMD_OUTSIDE_FRAME);
    H->command_add(M, "coop.fleet", "coop.fleet [list | shoot <id> <dmg> | hurt <ship id> <player>] - space co-op",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       SAY("%s", squadron::debug(std::vector<std::string>(argv + 1, argv + argc)).c_str());
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.civ",
                   "coop.civ [select <id> <0|1> | move <id> <x> <y> <z> | buy <city> <loco> | build <city> <kind>]",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       if (argc > 1) {
                           SAY("%s", civ::debug(std::vector<std::string>(argv + 1, argv + argc)).c_str());
                           return 0;
                       }
                       SAY("%s%s", civ::status().c_str(), civ::list().c_str());
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.tribe",
                   "coop.tribe [select <id> <0|1> | walk <x> <y> <z> | act <action> <target id>] - tribe co-op",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       if (argc > 1) {
                           SAY("%s", tribe::debug(std::vector<std::string>(argv + 1, argv + argc)).c_str());
                           return 0;
                       }
                       SAY("%s%s", tribe::status().c_str(), tribe::list().c_str());
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.team", "coop.team [on|off] - co-op mechanics (joint hunt, rescue, pack regen)", c_team,
                   nullptr, MT);
    init_ui();
    H->command_add(M, "coop.uistate", "coop.uistate [0|1] - co-op window state (open/close it)", c_uistate, nullptr,
                   SK_CMD_OUTSIDE_FRAME);
    H->cheat_register("coop", "coop.",
                      "coop host [port] | join <ip[:port]> | leave | status | name <nick> | say <text>");
    LOG("coop ready (name '%s')", g_name.c_str());
    return &g_info;
}
