---
id: BUG-0141
title: two players with the same name share one allies mirror, one set of rivals relations and one budget
status: open
kind: bug
stage: Space
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#120
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: #120
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/120 (full text, discussion).

Several Space tables are keyed by the player's name so that a rejoin (maybe under another id) finds its entry again. Names are not unique in a session, and the default name is "Player". With 3 or 4 players, two guests that never set a name are one player to these tables:
- one allies mirror shows only one of their colony lists;
- in rivals mode a war, alliance or proposal with one applies to both;
- a rejoin can hand one of them the other's budget.

### Where
- The host accepts any name, with no uniqueness check: [net/session.cpp#L319-L321](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/net/session.cpp#L319-L321); the default name: [coop.cpp#L2633-L2634](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L2633-L2634)
- Allies mirrors keyed by name (`g_m[name]`; `id` and `wanted` overwritten by whichever roster comes last): [space_allies.cpp#L802-L831](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/space_allies.cpp#L802-L831)
- Rivals relations by name pair, and the name-to-id lookup returns the first match: [space_rivalry.cpp#L177-L199](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/space_rivalry.cpp#L177-L199), [space_rivalry.cpp#L310-L320](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/space_rivalry.cpp#L310-L320)
- Budgets by name (K_MONEY stores, K_ASK hands back): [space_rivalry.cpp#L552-L570](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/space_rivalry.cpp#L552-L570)

### How to reproduce
- 3 players in Space, both guests with the default name (no `name=` in coop.ini, or `coop.name Player` on both), each guest with a colony of its own.
- On the host, `coop.space allies list`: one mirror "Player's empire" with one guest's colonies only.
- Switch to rivals mode. Guest B declares war on the host. On the host and on guest C, C is shown at war with the host as well, and the host's shots hurt C.

### Expected
Every player is a separate entry: two guests with the same name get separate mirrors, relations and budgets.

## Root cause
**What happens**

With the host "Alice" and guests B and C both named "Player":

- **Allies mirror** (on the host and on every other player): `sync_from_rosters` runs `g_m["Player"]` for both guests. The second one overwrites `id`, `color` and `wanted`, so only that guest's colonies are mirrored; the other's never appear. K_DEFENDED for a mirror colony goes to `m.id`, which may be the wrong guest. If one of them leaves, the mirror stays "online" because the other one still has the name.
- **Rivals relations:** `key("Alice", "Player")` is one entry. When B declares war on Alice, `host_rebuild` writes that relation for every pair whose names match, so C is at war with Alice too. C's puppet gets political id -1 on Alice's machine and Alice's weapons hit C. A relation between B and C is `key("Player", "Player")`. Proposals are stored by name and turned back into ids with `id_of`, which returns the first id with that name. A proposal from C can then show as coming from B, and "accept" may not match.
- **Budgets:** K_MONEY of B and of C both write `g_budget_name["Player"]`. When either rejoins and asks (K_ASK), it gets whichever budget was reported last.

With 2 players and equal names the proposals already resolve to the wrong id (`id_of("Player")` returns 0 for both ends). The mirror and relation mix-ups need 3 or more players.

### Confidence
CONFIRMED by reading. Verified by a second reviewer: low-medium (it needs equal names, and the default name makes that likely for new players).

## Fix
Not fixed yet. Suggested in the review:

Make names unique on the host at HELLO: if a name is taken by another accepted peer, append " 2", " 3" (`net/session.cpp`, before `WELCOME` and `PLAYER_JOIN`). The guest learns its effective name from the roster. This keeps the name-based rejoin working for the common case. Alternatively, key these tables by a per-install token sent in HELLO. Keep the old behaviour behind `coop.fix unique_names off`.

## Verification
Not done yet. Planned test (must FAIL without the fix and PASS with it):

Unit test (`mods/coop/net` builds natively): two HELLOs with the same name get two different names in the host's `players()`.

Game test with 3 players (`twoinst.start` plus `coop_multi.start_extra`, the `space` fixture), both guests named "Player":
- Assert on the host that `coop.space allies list` shows two mirrors with each guest's colonies.
- In rivals mode, guest B declares war on the host; assert on guest C's machine that its puppet of the host keeps its own political id (C is not at war).

Both must FAIL on main and PASS with the fix.

### Related
#54 and #42 (per-player state inherited by id; this is the same kind of mix-up keyed by name instead of id). #16 and #49 (other allies mirror problems).

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #120.
- 2026-10-27: ticket opened from the issue.
