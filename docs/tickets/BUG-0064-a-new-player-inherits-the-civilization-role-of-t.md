---
id: BUG-0064
title: a new player inherits the civilization role of the player who had their id before
status: open
kind: bug
stage: Civ
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#42
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: #42
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/42 (full text, discussion).

The host keeps every player's civilization role in `g_roles`, keyed by player id. Nothing removes an entry when a player leaves or a session ends. Ids are reused (the lowest free one), so a newcomer silently plays with the previous player's role on the host (military +25 % damage, economic trade bonus, religious faster conversion), while their own window says "My role: none - pick one". Severity: wrong behaviour.

### Where
- `g_roles`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L129-L130
- `player_leave` erases `g_ready` but not `g_roles`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1707-L1717
- `reset_lobby` (on host and join) does not clear it: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1734-L1739
- Host-side effects through `role_of`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L2878 and https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/civ.cpp#L523-L540 (military), https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/civ.cpp#L576-L592 (religious), https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/civ.cpp#L865-L885 (economic)
- Id reuse: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/net/session.cpp#L116-L127

### How to reproduce
1. Three instances or two with a rejoin, `civ_npc` fixture. Guest A: `coop.role 1`.
2. A: `coop.leave`. Guest B (or A with another name) joins and gets id 1.
3. Host: `coop.role` lists `role 1 <B> military`. B: `coop.role` lists itself as `none`.
4. In a fight, `coop.civ` on the host shows `military_hits` growing for B's vehicles.

### Expected
A new player starts with role "none" everywhere, and the host and the player agree on it.

## Root cause
**What happens**

1. Guest A joins as id 1 and sets "military" (`MSG_ROLE`), so the host stores `g_roles[1] = 1`.
2. A leaves (or times out). `player_leave` erases `g_ready[1]` and the `Remote`, but `g_roles[1]` stays 1.
3. Guest B joins and `alloc_id()` gives id 1 again. B's own `g_roles[my_id]` is 0, and on a guest the lobby message never overwrites the own role (`g_roles[g_sess.my_id()] = my_role`), so B's window shows "none".
4. The host's `b_damage` / `h_Convert` / `trade_tick` call `role_of(1)`, which returns 1: B's vehicles hit 25 % harder although B never chose military.

The same happens across sessions: the host stops hosting and hosts again, or a former guest hosts with entries left from the session it was in.

### Confidence
CONFIRMED by reading the full path. Not run in game.

## Fix
Not fixed yet. Suggested in the review:

- `g_roles.erase(id)` in `cb.player_leave`.
- `g_roles.clear()` in `reset_lobby()` (and in the session-end helper of #11).
- On the host, `player_join` could also send the current table at once (`g_lobby_dirty = true` is already set).

## Verification
Not done yet. Planned test (must FAIL without the fix and PASS with it):

`tools/tests/coop_civ_roles.py` (host plus one guest that rejoins, civ fixture):
1. Guest `coop.role 1`, guest `coop.leave`, guest joins again (id 1 again).
2. Assert on the host that the game's damage of the guest's vehicle to a building is unmodified: drive one `pincer`/hit and compare the building's health loss with a known unit damage, not a mod counter.
3. Assert `coop.role` agrees on both machines.

It must fail on main.

### Related
- The issue on lobby roles read in a different order on guests (same table).
- The creations issue on reused player ids (same pattern).
- Umbrella #11.

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #42.
- 2026-10-27: ticket opened from the issue.
