// Native tests for the NPC id allocator (npc_ids.h, BUG-0023 / BUG-0044) and the joint-hunt hit
// table (hunt_hits.h, BUG-0099) (make test; built with AddressSanitizer).
#include <cstdio>
#include <set>
#include <unordered_map>

#include "hunt_hits.h"
#include "npc_ids.h"
#include "swap_ledger.h"

static int g_fail = 0;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);        \
            g_fail++;                                                    \
        }                                                                \
    } while (0)

// host_tick's pattern: NPCs come and go, a few live for good. Returns how many times an id was
// given while a living NPC had it.
static int churn(bool skip_used)
{
    std::unordered_map<uint16_t, int> live; // id -> npc
    uint16_t next = 1;
    int repeats = 0;
    for (int i = 0; i < 5; i++) // five NPCs that stay in the world (the nest next to the avatar)
        live[npc::alloc_npc_id(next, [&](uint16_t id) { return live.count(id) != 0; }, skip_used)] = i;
    for (int n = 0; n < 200000; n++) { // NPCs that load and unload at once while the players roam
        uint16_t id = npc::alloc_npc_id(next, [&](uint16_t id) { return live.count(id) != 0; }, skip_used);
        CHECK(id >= npc::kFirstNpcId && id <= npc::kLastNpcId);
        repeats += live.count(id) != 0;
    }
    return repeats;
}

static void test_ids()
{
    printf("npc ids\n");
    CHECK(churn(false) > 0); // the code before BUG-0023: an id in use is given again after the wrap
    CHECK(churn(true) == 0);
    { // never 0 or 0xFFFF, wraps to 1
        uint16_t next = 0xFFFE;
        CHECK(npc::alloc_npc_id(next, [](uint16_t) { return false; }) == 0xFFFE);
        CHECK(npc::alloc_npc_id(next, [](uint16_t) { return false; }) == 1);
        next = 0;
        CHECK(npc::alloc_npc_id(next, [](uint16_t) { return false; }) == 1);
    }
    { // every id in use: none
        uint16_t next = 7;
        CHECK(npc::alloc_npc_id(next, [](uint16_t) { return true; }) == 0);
    }
    { // skips a run of used ids
        std::set<uint16_t> used{5, 6, 7};
        uint16_t next = 5;
        CHECK(npc::alloc_npc_id(next, [&](uint16_t id) { return used.count(id) != 0; }) == 8);
        CHECK(next == 9);
    }
    // BUG-0044: an object that is another creature now
    CHECK(!npc::npc_object_reused(0x65CB8AB, false, 0x65CB8AB, false));
    CHECK(!npc::npc_object_reused(0x65CB8AB, false, 0x65CB8AB, true)); // it died
    CHECK(npc::npc_object_reused(0x65CB8AB, true, 0x65CB8AB, false));  // alive after it was dead
    CHECK(npc::npc_object_reused(0x65CB8AB, false, 0x65CBF12, false)); // another species
}

static void test_hits()
{
    printf("joint hunt hits\n");
    for (int legacy = 0; legacy < 2; legacy++) {
        teamplay::HitTable t(6.0);
        t.legacy = legacy;
        // 1000 creatures hit once each, one per second: only those of the last 6 s matter
        static char targets[1000];
        for (int i = 0; i < 1000; i++)
            t.hit(&targets[i], 1, i);
        if (legacy)
            CHECK(t.size() == 1000); // the code before BUG-0099: one entry per creature ever hit
        else
            CHECK(t.size() <= 7);
    }
    teamplay::HitTable t(6.0);
    char a, b;
    CHECK(!t.hit(&a, 0, 0));
    CHECK(t.hit(&a, 1, 2));  // another player within the window: joint
    CHECK(!t.hit(&b, 1, 2));
    CHECK(!t.hit(&a, 0, 20)); // the window is over
    t.forget(&a);              // the object was freed: a new creature at its address starts clean
    CHECK(!t.hit(&a, 1, 21));
    t.clear();
    CHECK(t.size() == 0);
}

// BUG-0110: changes around a call into the game that a fault leaves open are undone once per frame
static void set_u32(void* p, uintptr_t v) { *(uint32_t*)p = (uint32_t)v; }
static void test_ledger()
{
    printf("swap ledger\n");
    for (bool guard : {false, true}) {
        swapledger::Ledger<4> l;
        l.guard = guard;
        uint32_t av = 7, flag = 1;
        // a call that returns: nothing left
        size_t m = l.open(set_u32, &av, av, "av");
        av = 9;
        av = 7;
        l.close(m);
        CHECK(l.n == 0 && l.tick() == 0);
        // a call that faults (the close never runs): the frame's tick puts the value back
        m = l.open(set_u32, &av, av, "av");
        av = 9;
        l.fault = true;
        l.close(m);
        l.tick();
        if (guard)
            CHECK(av == 7 && l.restored == 1);
        else
            CHECK(av == 9); // the code before the fix: the partner stays the avatar
        av = 7;
        l.n = 0;
        // an inner call faulted inside an outer one that returned: the outer close undoes it
        size_t outer = l.open(set_u32, &av, av, "av");
        av = 9;
        l.open(set_u32, &flag, flag, "flag"); // inner
        flag = 0;
        av = 7;
        l.close(outer);
        CHECK(flag == 1 && l.n == 0);
        // several own entries of one caller are its own (not leftovers)
        uint32_t x = 1, y = 2;
        m = l.n;
        l.open(set_u32, &x, x, "x");
        l.open(set_u32, &y, y, "y");
        x = y = 5;
        l.close(m, l.n - m);
        CHECK(x == 5 && y == 5 && l.n == 0);
        // full: no entry, nothing breaks
        for (int i = 0; i < 6; i++)
            l.open(set_u32, &x, 1, "x");
        CHECK(l.n == 4);
        l.close(0, 4);
        CHECK(l.n == 0);
    }
}

int main()
{
    test_ids();
    test_hits();
    test_ledger();
    printf(g_fail ? "npc tests: %d FAILED\n" : "npc tests: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
