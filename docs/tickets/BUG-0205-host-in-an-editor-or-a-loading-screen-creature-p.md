---
id: BUG-0205
title: host in an editor or a loading screen: Creature proxies and Cell mirrors freeze, a goal reward reaching a host without a creature is lost
status: open
kind: bug
stage: All
reported: 2026-10-28
source: BUG-0151 rest (fix/w3-civ)
role: both
versions: main 5ae9744 and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0151, #130
---

## Report
The part of BUG-0151 (GitHub #130) that fix/w3-civ did not fix. "Host away" (the guest plays on
alone, its NPCs simulated by its own game) means only "the host is in the main menu". While the host
is in an editor (an evolution or design change the guest does not join, the vote timed out) or on a
loading screen:
- Creature: the guest's own NPCs are still removed by the update filter, while the host sends no NPC
  states (its `npc_stage()` is false), so the proxies stand frozen.
- Cell: the mirrors stay AI-locked.
- A goal card's reward (`social` K_GOAL) that reaches the host while it has no creature is dropped
  (`social.cpp` K_GOAL handler requires `game::avatar()`). Plain DNA gains are kept since
  BUG-0151 (`g_dna_owed`).

How to reproduce: creature fixture, the host opens its editor alone (`coop.vote test` path or the
guest not ready), watch the guest's NPC proxies (`coop.npc` positions do not change).

## Root cause
Inferred from reading (BUG-0151 review): `host_away_tick` (coop.cpp) only switches on
`host_in_menu()`; the editor and loading modes are not counted. Widening it touches npc, cellsync and
the DNA flow (Creature, Cell and core areas), so it was left to those areas.

## Fix
Not fixed yet. Idea: one "host away" state for "not in our place for more than ~1 s" (mode and
world, `!host_here()`), used by npc, cellsync, tribe and civ; on the host, keep K_GOAL rewards that
arrive without a creature like `g_dna_owed`.

## Verification
<!-- the test: FAIL without the fix (run dir), PASS with it (runs) -->

## History
- 2026-10-28: opened.
