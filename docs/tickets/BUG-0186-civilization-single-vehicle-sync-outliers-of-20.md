---
id: BUG-0186
title: Civilization: single vehicle sync outliers of 20-27 m
status: open
kind: desync
stage: Civ
reported: 2026-10-27
source: docs/known-issues.md (ticket backfilled 2026-10-27)
role: guest
versions: 0.7.2.4 (main) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0122, BUG-0188
---

## Report
Copied from docs/known-issues.md, where the entry stays up to date:

* `play_civ.py` sessions: p90 of the vehicle position error 0.9-1.5 m, but the maximum of ~2500
  samples was 22-27 m in three sessions (one sample each). Not traced yet: candidates are a guest
  mirror bound to a nearby local vehicle of the same kind right after a purchase (the 25 m bind
  radius in guest_state) or the velocity estimate around a capture caption pause. Reproduce:
  `SPORE_LANE=N python3 tools/tests/play_civ.py --minutes 10 --seed 9` (it prints the outliers):
  `v13 22.7 m host (-497, 27, 66) guest (-493, 45, 79)` three samples in a row, an NPC vehicle on
  the far side of the planet; not seen on player vehicles.

## Root cause
See the report (observed vs inferred as marked there).

## Fix
Not fixed yet.

## Verification

## History
- 2026-10-27: first recorded in docs/known-issues.md.
- 2026-10-27: ticket backfilled from docs/known-issues.md (every entry carries its ticket id, CLAUDE.md 1).
- 2026-10-28 (fix/w2-civ): not reproduced at 20-27 m: `play_civ.py --minutes 10 --seed 9` on lane 7 (GOG 2024, before the BUG-0188 fix and without the BUG-0122 fix) gave n=2430, p90 0.34 m, max 17.4 m (one sample, vehicle v2). Both candidates of the report are being removed by other fixes: the receive-time velocity of BUG-0122 (fix/w2-net: a late or bunched snapshot made the mirror overshoot by 22-40 units in the harness, which fits a single 20-27 m sample of a fast vehicle) and the guest's own NPC AI vehicles that could be bound as a mirror within 25 m (BUG-0188, fixed here; seen bound once in `coop_civ_ai_spawn.py --nofix`). Left open: re-run play_civ seed 9 after both branches are merged; close if the max stays under ~10 m. Most likely the same cause as BUG-0122 (inferred, not proven).
