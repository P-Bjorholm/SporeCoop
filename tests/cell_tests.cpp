// Native tests for cell_logic.h (make test): the decisions of the Cell stage sync. Each case runs
// with the fix on (must hold) and off (must show the bug), so the test is known to catch it.
#include <cmath>
#include <cstdio>

#include "cell_logic.h"

using namespace cell_logic;

static int g_fail = 0;
#define CHECK(c)                                                  \
    do {                                                          \
        if (!(c)) {                                               \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            g_fail++;                                             \
        }                                                         \
    } while (0)

// BUG-0083: slot == highWater is not a live slot, and nothing past the capacity is read
static void slots()
{
    CHECK(slot_in_pool(0, 10, 16, true));
    CHECK(slot_in_pool(9, 10, 16, true));
    CHECK(!slot_in_pool(10, 10, 16, true));
    CHECK(!slot_in_pool(16, 16, 16, true)); // full pool: one object past the allocation
    CHECK(!slot_in_pool(12, 20, 12, true)); // a corrupt highWater above the capacity
    CHECK(slot_in_pool(10, 10, 16, false)); // the old bound let it through
    CHECK(slot_in_pool(16, 16, 16, false));
}

// BUG-0032: a lost or late chunk must not let the removal pass run on half a round
static void rounds()
{
    for (bool fix : {true, false}) {
        Rounds r;
        // round 1: 3 chunks, chunk 0 lost
        CHECK(r.on_chunk(1, 1, fix) == Rounds::NEW_ROUND);
        CHECK(r.on_chunk(1, 2, fix) == Rounds::SAME_ROUND);
        CHECK(r.complete(3, fix) == !fix); // old: removal ran, the 14 cells of chunk 0 were deleted
        // round 2 complete
        CHECK(r.on_chunk(2, 0, fix) == Rounds::NEW_ROUND);
        r.on_chunk(2, 1, fix);
        r.on_chunk(2, 2, fix);
        CHECK(r.complete(3, true) || !fix);
        // round 3 starts, then the last chunk of round 2 comes late (reordered)
        CHECK(r.on_chunk(3, 0, fix) == Rounds::NEW_ROUND);
        Rounds::Verdict late = r.on_chunk(2, 2, fix);
        CHECK(fix ? late == Rounds::OLD_CHUNK : late == Rounds::NEW_ROUND); // old: round 2 "again", seen = 1 chunk
        if (fix) { // round 3 goes on undisturbed
            CHECK(r.on_chunk(3, 1, fix) == Rounds::SAME_ROUND);
            CHECK(r.on_chunk(3, 2, fix) == Rounds::SAME_ROUND);
            CHECK(r.complete(3, fix));
        }
    }
    // wrap-around of the 16-bit round number, and a host that starts counting again
    Rounds r;
    r.on_chunk(0xFFFF, 0, true);
    CHECK(r.on_chunk(0, 0, true) == Rounds::NEW_ROUND);
    CHECK(r.on_chunk(0xFFFF, 0, true) == Rounds::OLD_CHUNK);
    r.on_chunk(5000, 0, true);
    CHECK(r.on_chunk(1, 0, true) == Rounds::NEW_ROUND); // far behind: a new session
    CHECK(r.on_chunk(1, 0, true) == Rounds::SAME_ROUND); // a duplicate
}

// BUG-0051: T = 45. The host eats 2 (47, sent), the guest eats 10 (55 here, level 1 at 50).
// The host's 47 arrives before the host counted the guest's meal.
static void meals()
{
    for (bool fix : {true, false}) {
        Meals m;
        int local = 55;
        uint32_t meal = m.sent(10);
        int d = m.diff(47, true, 0, 0, local, fix); // the total without our meal (ack 0)
        local += d;
        CHECK(fix ? local == 57 : local == 47); // old: below the level line, a level down
        d = m.diff(57, true, meal, 0, local, fix); // the host counted it
        local += d;
        CHECK(local == 57);
    }
    // a refused meal (eaten first by somebody else, eats_late): acknowledged without points
    Meals m;
    uint32_t meal = m.sent(10);
    int local = 55;
    local += m.diff(45, true, meal, 0, local, true);
    CHECK(local == 45);
    // not sent yet (this frame's meal) counts as on the way, an older host without the ack does not
    Meals p;
    CHECK(p.diff(45, true, 0, 4, 49, true) == 0);
    CHECK(p.diff(45, false, 0, 4, 49, true) == -4);
    // meals with no points (a kill) get numbers but are not waited for
    Meals k;
    k.sent(0);
    CHECK(k.unacked.empty());
}

// BUG-0041: a small cell (0.5 of size 2 -> 1) eats 41 points over a level line; the game resizes
// it to the new level's size 3 inside AddFoodPoints
static void body()
{
    Body b = regrow(1.f, 3.f, 0.5f, 41, true);
    CHECK(b.factor == 1.f && std::fabs(b.target - 3.f) < 1e-4f);
    Body old = regrow(1.f, 3.f, 0.5f, 41, false);
    CHECK(old.target > 5.f); // 3 / 0.5 * 0.91 = 5.46: almost twice the level's size
    // a meal within the level: grows by 0.01 per point
    Body g = regrow(1.f, 1.f, 0.5f, 10, true);
    CHECK(std::fabs(g.factor - 0.6f) < 1e-4f && std::fabs(g.target - 1.2f) < 1e-4f);
}

static void ko()
{
    CHECK(!ko_stale(10.0, 9.0, true));
    CHECK(ko_stale(10.0, 7.5, true));
    CHECK(!ko_stale(100.0, 7.5, false)); // old: never
}

int main()
{
    slots();
    rounds();
    meals();
    body();
    ko();
    if (g_fail) {
        printf("cell_tests: %d FAILED\n", g_fail);
        return 1;
    }
    printf("cell_tests: all passed\n");
    return 0;
}
