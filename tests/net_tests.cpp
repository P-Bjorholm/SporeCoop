// Native (Linux) tests for the portable coop netcode: make test
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include "net/dbpf.h"
#include <cmath>
#include <cstdio>
#include <random>
#include <chrono>
#include <thread>

#include "net/connection.h"
#include "net/discovery.h"
#include "net/interp.h"
#include "net/session.h"

using namespace net;

static int g_fail = 0;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);        \
            g_fail++;                                                    \
        }                                                                \
    } while (0)

static void test_buffer()
{
    printf("buffer roundtrip\n");
    PlayerState s;
    s.id = 3; s.time_ms = 123456; s.pos = {1, 2, 3}; s.rot = {0, 0, 0.7071f, 0.7071f};
    s.vel = {4, 5, 6}; s.flags = PF_IN_WORLD | PF_RUNNING; s.anim = 7; s.health = 50; s.max_health = 100;
    s.species = 0xDEADBEEF;
    Writer w;
    s.write(w);
    Reader r(w.data.data(), w.size());
    PlayerState t;
    CHECK(t.read(r));
    CHECK(t.id == 3 && t.time_ms == 123456 && t.pos.z == 3 && t.rot.w > 0.7f && t.flags == s.flags);
    CHECK(t.species == 0xDEADBEEF && t.max_health == 100);
    Reader bad(w.data.data(), 5);
    PlayerState u;
    CHECK(!u.read(bad));
    // v2: mode + world identify where the player is
    s.mode = 0x1654C05; s.world = 0x80B16021;
    Writer w2;
    s.write(w2);
    Reader r2(w2.data.data(), w2.size());
    PlayerState t2;
    CHECK(t2.read(r2) && t2.mode == 0x1654C05 && t2.world == 0x80B16021);
    // shared resource message payload (u8 id, f64)
    Writer w3;
    w3.u8(3);
    w3.f64(123456.75);
    Reader r3(w3.data.data(), w3.size());
    CHECK(r3.u8() == 3 && r3.f64() == 123456.75 && r3.ok() && r3.left() == 0);
}

// a snapshot in several big unreliable chunks: every chunk goes out (one per packet), none is lost
static void test_unreliable_chunks()
{
    printf("unreliable chunks larger than one packet\n");
    Connection a, b;
    for (int k = 0; k < 5; k++) {
        Writer w;
        w.u8((uint8_t)k);
        for (int i = 0; i < 1000; i++)
            w.u8(0x55);
        a.send_unreliable(9, w);
    }
    std::vector<uint8_t> pkt;
    std::vector<int> got;
    int packets = 0;
    while (a.build_packet(2000, pkt)) {
        packets++;
        b.on_packet(pkt.data(), pkt.size(), 2000, [&](uint8_t type, Reader& r) {
            if (type == 9) got.push_back(r.u8());
        });
    }
    CHECK(packets == 5);
    CHECK(got.size() == 5 && got[0] == 0 && got[4] == 4);
    CHECK(a.unreliable_queued() == 0);
}

static void test_dbpf()
{
    printf("dbpf write/read, EditorSaves of the test profile\n");
    std::vector<dbpf::Record> recs = {{{0x7C000001, 0x1A99B06B, 0x40626200}, {1, 2, 3, 4}},
                                      {{0x7C000001, 0x030BDEE3, 0x40626200}, std::vector<uint8_t>(300, 9)}};
    dbpf::Package pk;
    CHECK(pk.load(dbpf::write(recs)));
    CHECK(pk.entries().size() == 2);
    std::vector<uint8_t> out;
    const dbpf::Entry* e = pk.find(recs[1].key);
    CHECK(e && pk.read(*e, out) && out == recs[1].data);
    // the real thing (RefPack records, pollen metadata names), when the test profile exists
    const char* home = getenv("HOME");
    const char* user = getenv("USER");
    if (!home || !user)
        return;
    std::string path = std::string(home) + "/.var/app/com.usebottles.bottles/data/bottles/bottles/Games/drive_c/users/" +
                       user + "/AppData/Roaming/SporeKit-test/Spore/EditorSaves.package";
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        printf("  (no test profile EditorSaves.package)\n");
        return;
    }
    std::vector<uint8_t> file((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    dbpf::Package es;
    CHECK(es.load(file));
    int bems = 0, names = 0;
    for (auto& en : es.entries()) {
        std::vector<uint8_t> d;
        if (en.key.type == 0x1A99B06B && en.key.group == 0x40626200) {
            CHECK(es.read(en, d) && d.size() > 1000 && d[0] == 0xD3 && d[1] == 0x7E);
            bems++;
        }
        std::string a, n;
        if (en.key.type == 0x030BDEE3 && es.read(en, d) && dbpf::metadata_names(d, a, n) && !n.empty())
            names++;
    }
    printf("  %d creatures, %d names\n", bems, names);
    CHECK(bems > 0 && names >= bems);
}

static void test_reliability()
{
    printf("reliable channel under 30%% loss + reordering\n");
    Connection a, b;
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> U(0, 1);
    std::vector<std::vector<uint8_t>> a2b, b2a;
    std::vector<uint32_t> got;
    const int N = 400;
    uint32_t now = 1000;
    int sent = 0;
    for (int step = 0; step < 4000 && ((int)got.size() < N || a.pending_reliable()); step++) {
        now += 10;
        if (sent < N && step % 2 == 0) {
            Writer w;
            w.u32(sent++);
            a.send_reliable(42, w);
        }
        if (step % 3 == 0) {
            Writer w;
            w.u32(0xAAAA);
            a.send_unreliable(7, w);
        }
        std::vector<uint8_t> pkt;
        while (a.build_packet(now, pkt))
            if (U(rng) > 0.3f) a2b.push_back(pkt);
        while (b.build_packet(now, pkt))
            if (U(rng) > 0.3f) b2a.push_back(pkt);
        // deliver with random reordering
        std::shuffle(a2b.begin(), a2b.end(), rng);
        for (auto& p : a2b)
            b.on_packet(p.data(), p.size(), now, [&](uint8_t type, Reader& r) {
                if (type == 42) got.push_back(r.u32());
            });
        a2b.clear();
        for (auto& p : b2a)
            a.on_packet(p.data(), p.size(), now, [](uint8_t, Reader&) {});
        b2a.clear();
    }
    CHECK((int)got.size() == N);
    bool ordered = true;
    for (size_t i = 0; i < got.size(); i++)
        if (got[i] != i) ordered = false;
    CHECK(ordered);
    CHECK(a.pending_reliable() == 0);
    printf("  delivered %zu/%d, resends=%u rtt=%.0f\n", got.size(), N, a.stats.resends, a.rtt_ms);
}

static void pump(Session& h, Session& c, int ms)
{
    for (int i = 0; i < ms / 5; i++) {
        h.update();
        c.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

static void test_session()
{
    printf("session host/join/state/chat/leave over localhost\n");
    Session host, cli;
    int joins = 0, states = 0, chats = 0, leaves = 0;
    bool connected = false;
    host.build = cli.build = 0x922af7c7;
    host.cb.player_join = [&](const PlayerInfo& p) { joins++; CHECK(p.name == "guest-bob"); };
    host.cb.player_state = [&](const PlayerState& s) { states++; CHECK(s.id == 1 && s.pos.x == 10); };
    host.cb.chat = [&](uint8_t id, const std::string& t) { if (id == 1) { chats++; CHECK(t == "hi"); } };
    host.cb.player_leave = [&](uint8_t, uint8_t) { leaves++; };
    cli.cb.connected = [&] { connected = true; };
    std::string err;
    CHECK(host.host(0, "host-alice", err));
    uint16_t port = host.port();
    CHECK(cli.join("127.0.0.1:" + std::to_string(port), "guest-bob", err));
    pump(host, cli, 300);
    CHECK(connected);
    CHECK(joins == 1);
    CHECK(cli.my_id() == 1);
    CHECK(cli.name_of(0) == "host-alice");
    PlayerState s;
    s.pos = {10, 0, 0};
    for (int i = 0; i < 5; i++) {
        cli.send_state(s);
        pump(host, cli, 50);
    }
    CHECK(states >= 3);
    int cstates = 0;
    cli.cb.player_state = [&](const PlayerState& st) { if (st.id == 0) cstates++; };
    host.send_state(s);
    pump(host, cli, 100);
    CHECK(cstates == 1);
    cli.send_chat("hi");
    pump(host, cli, 100);
    CHECK(chats == 1);
    cli.leave("test");
    pump(host, cli, 200);
    CHECK(leaves == 1);
    printf("  %s", host.status().c_str());

    // build mismatch must be rejected
    Session bad;
    bad.build = 0x1234;
    std::string why;
    bad.cb.disconnected = [&](const std::string& w) { why = w; };
    CHECK(bad.join("127.0.0.1:" + std::to_string(port), "x", err));
    pump(host, bad, 300);
    CHECK(why.find("build") != std::string::npos);
}

static void test_interp()
{
    printf("snapshot interpolation\n");
    SnapshotBuffer b;
    b.delay = 0.1;
    for (int i = 0; i <= 10; i++) {
        PlayerState s;
        s.time_ms = 1000 + i * 50;
        s.pos = {(float)i, 0, 0};
        s.vel = {20, 0, 0};
        b.push(s, 5.0 + i * 0.05); // local clock offset 4s
    }
    PlayerState o;
    CHECK(b.sample(5.0 + 0.5, o)); // newest local recv time -> render 0.1 s behind -> x=8
    CHECK(std::fabs(o.pos.x - 8.0f) < 0.01f);
    CHECK(b.sample(5.0 + 0.6, o)); // extrapolate 0 s past newest: x=10
    CHECK(std::fabs(o.pos.x - 10.0f) < 0.01f);
    CHECK(b.sample(5.0 + 2.0, o)); // capped at 0.25 s -> x = 10 + 5
    CHECK(std::fabs(o.pos.x - 15.0f) < 0.01f);
}

static void test_discovery()
{
    Session host;
    std::string err;
    CHECK(host.host(kDefaultPort + kDiscoverPorts - 1, "LanHost", err));
    host.lobby_info = "Creature - Testworld";
    Discovery d;
    CHECK(d.start());
    d.targets = {0x0100007F}; // 127.0.0.1
    d.interval_ms = 100;
    std::vector<Discovery::Game> found;
    for (int i = 0; i < 200 && found.empty(); i++) {
        d.update(now_ms());
        host.update();
        d.update(now_ms());
        found = d.games(now_ms());
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(found.size() == 1);
    if (!found.empty()) {
        CHECK(found[0].info.name == "LanHost");
        CHECK(found[0].info.info == "Creature - Testworld");
        CHECK(found[0].info.players == 1);
        CHECK(found[0].host.port == kDefaultPort + kDiscoverPorts - 1);
    }
    printf("discovery ok\n");
}

static void test_ackbits()
{
    printf("128-packet ack window\n");
    AckBits b;
    b.set(0);
    b.set(63);
    b.set(64);
    b.set(127);
    CHECK(b.test(0) && b.test(63) && b.test(64) && b.test(127) && !b.test(1) && !b.test(128));
    b.shift(1);
    CHECK(b.test(1) && b.test(64) && b.test(65) && !b.test(0) && !b.test(127));
    b.shift(64);
    CHECK(b.test(65) && !b.test(1));
    b.shift(200);
    CHECK(!b.lo && !b.hi);
}

// a big reliable transfer (the world sent to a guest) in bursts of a whole ack window, with loss
static void test_bulk()
{
    printf("bulk transfer: 4 MB in bursts of %u packets, 5%% loss\n", kAckWindow);
    Connection a, b;
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> U(0, 1);
    const int N = 4000; // 1 KB messages
    uint32_t now = 1000;
    int queued = 0, got = 0, frames = 0;
    bool ordered = true;
    std::vector<uint8_t> pkt;
    a.rtt_ms = b.rtt_ms = 5;
    while (got < N && frames < 3000) {
        frames++;
        now += 33; // one game frame
        while (queued < N && a.pending_reliable() < 600) {
            Writer w;
            w.u32(queued++);
            std::vector<uint8_t> pad(1020, 0x55);
            w.raw(pad.data(), pad.size());
            a.send_reliable(42, w);
        }
        std::vector<std::vector<uint8_t>> a2b, b2a;
        for (unsigned i = 0; i < kAckWindow && a.build_packet(now, pkt); i++)
            if (U(rng) > 0.05f) a2b.push_back(pkt);
        for (auto& p : a2b)
            b.on_packet(p.data(), p.size(), now, [&](uint8_t type, Reader& r) {
                if (type == 42 && r.u32() != (uint32_t)got++) ordered = false;
            });
        while (b.build_packet(now + 1, pkt))
            if (U(rng) > 0.05f) b2a.push_back(pkt);
        for (auto& p : b2a)
            a.on_packet(p.data(), p.size(), now + 2, [](uint8_t, Reader&) {});
    }
    double mbps = got * 1024.0 / (frames * 0.033) / 1e6;
    printf("  %d/%d in %d frames (%.1f s at 30 fps: %.1f MB/s), resends=%u\n", got, N, frames, frames * 0.033, mbps,
           a.stats.resends);
    CHECK(got == N && ordered);
    CHECK(mbps > 2.5); // the old 32-packet window gave ~1.1 MB/s
}

int main()
{
    test_buffer();
    test_unreliable_chunks();
    test_dbpf();
    test_reliability();
    test_ackbits();
    test_bulk();
    test_session();
    test_interp();
    test_discovery();
    printf(g_fail ? "FAILED (%d)\n" : "ALL OK\n", g_fail);
    return g_fail ? 1 : 0;
}
