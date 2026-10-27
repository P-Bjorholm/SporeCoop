---
id: BUG-0079
title: guests can see other players' civilization roles swapped after someone rejoins
status: open
kind: desync
stage: Civ
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#57
role: guest
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: #57
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/57 (full text, discussion).

The host writes the role table of the lobby message in the order of its player list, which is the order of joining. Guests read it in ascending player-id order. Once a player leaves and another takes the free lower id, the two orders differ and guests show the roles of other players swapped. Severity: minor (display only: the host, which applies the role bonuses, keeps the right table).

### Where
- Host writes ids, then roles in `players()` order: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L875-L891
- `players()` on the host is `0` plus `peers_` in connection order: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/net/session.cpp#L145-L163
- The guest puts the ids into a `std::map` (sorted) and reads roles in map order: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1625-L1648

```cpp
for (auto& [id, on] : ready)   // sorted by id
    g_roles[id] = r.u8();      // but written in join order
```

### How to reproduce
1. Host plus 3 instances (or 2 guests with a rejoin), `civ_npc` fixture.
2. B: `coop.role 1` (military). C: leave it at 0.
3. A leaves, C joins (id 1).
4. On B, `coop.role` lists C as military and itself as its own role (own role is kept). On the host the list is right.

### Expected
Every machine shows the same role for every player.

## Root cause
**What happens**

1. Guests A (id 1) and B (id 2) join; peers order is [A, B].
2. A leaves; C joins and gets id 1; peers order is [B(2), C(1)].
3. The host writes ids 0, 2, 1 and roles r0, r2, r1.
4. A guest reads ready/rtt per id correctly (each comes with its id), but roles in map order 0, 1, 2. So id 1 gets r2 and id 2 gets r1.

### Confidence
CONFIRMED by reading the full path (write order versus read order). Not run in game.

## Fix
Not fixed yet. Suggested in the review:

Write the roles in the same order the guest reads them, or better, as id and role pairs. Old guests stop reading at the end of the main table, so a new trailing section `u8 n, n x (u8 id, u8 role)` stays compatible. Simplest: sort `pl` by id in `send_lobby` before writing both tables.

## Verification
Not done yet. Planned test (must FAIL without the fix and PASS with it):

`tests/net_tests.cpp`: build the lobby message from a player list in join order [0, 2, 1] with roles {1:0, 2:1}, parse it with the guest code (factor the parse out of the callback into a function), and assert `roles[2] == 1`. It must fail on main. A two-instance variant in `tools/tests/coop_civ_roles.py` can assert the guest's co-op window text.

### Related
- The issue on civ roles sticking to reused player ids (same table).

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #57.
- 2026-10-27: ticket opened from the issue.
