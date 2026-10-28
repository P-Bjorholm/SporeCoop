---
id: BUG-0171
title: Tribe: the host froze once while the epic creature was being killed
status: not-reproduced
kind: freeze
stage: Tribe
reported: 2026-10-22
source: docs/known-issues.md (ticket backfilled 2026-10-27)
role: host
versions: 0.7.2.4 (main) and earlier
fixed_in:
branch: fix/w1-tribe-civ
commits: ba9dc46
tests: tribe_play, chaos_tribe
signature:
docs: docs/re/tribe.md#tribe-lifetime
related:
---

## Report
Copied from docs/known-issues.md, where the entry stays up to date:

* `coop_tribe_play.py` on branch feat/net-window, lane 2 (2026-10-22): the host stopped drawing
  during `coop.tribe epic hit` (monitor: FROZE); not reproduced in 14 more runs. The game logs of
  that run were lost (now kept by lab.py). Hunt: `lab.py run tribe_play,chaos_tribe --repeat 10`.

## Root cause
See the report (observed vs inferred as marked there).

## Fix
No cause found. Hardened on the way (fix/w1-tribe-civ, ba9dc46): `g_epic` (a raw pointer kept for
minutes) is checked against the game's object list before it is read, `coop.tribe epic hit` stops
at a dead victim (the test loop went on hitting a dying epic), and the epic state ends with the
session and the world.

## Verification
Hunt on main (88341cf), lane 5, GOG 2024: `lab.py run tribe_play,chaos_tribe --repeat 4`, lab run
20261027-183142: 8 of 8 passed, no freeze (the epic was killed together in every tribe_play run).
With the hardening: tribe_play in lab run 20261027-185915. 14 more runs before (2026-10-22).

## History
- 2026-10-22: first recorded in docs/known-issues.md.
- 2026-10-27: ticket backfilled from docs/known-issues.md (every entry carries its ticket id, CLAUDE.md 1).
- 2026-10-27: not reproduced in 8 runs on main (lab 20261027-183142); epic lifetime hardened on fix/w1-tribe-civ (ba9dc46).
