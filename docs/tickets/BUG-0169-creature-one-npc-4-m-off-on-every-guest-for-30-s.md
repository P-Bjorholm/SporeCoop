---
id: BUG-0169
title: Creature: one NPC 4 m off on every guest for 30+ s (seen once)
status: open
kind: desync
stage: Creature
reported: 2026-10-23
source: docs/known-issues.md (ticket backfilled 2026-10-27)
role: guest
versions: 0.7.2.4 (main) and earlier
fixed_in:
branch: fix/w2-creature
commits:
tests:
signature:
docs:
related:
---

## Report
Copied from docs/known-issues.md, where the entry stays up to date:

* `out/join_desync.py`-style probe, 4 players, 2026-10-23 (after the menu-load fix): NPC n11 stayed
  4 m from the host's on Bob, Carol and Dave alike from 8 s to the end (36 s); the 19 others < 1 m.
  The same offset on every guest points at the host side or at a spot the proxy cannot stand on
  (pushed out by collision after each stuck-teleport of `drive::step`). Not seen again in 5 min of
  watching nor in 4 more sessions. Hunt: `coop.npc list` on host and guests for that id (err, want).

## Root cause
See the report (observed vs inferred as marked there).

## Fix
Not fixed. Candidate from code reading (inferred, not observed): `drive::step` (driver.h)
teleports only above 8 m, or after 1.5 s when the host's NPC reports less than 0.3 m/s. An NPC of
the host that pushes against something (velocity reported, position fixed) keeps the proxy walking
to a spot it cannot reach on any guest, for as long as it pushes: the same 4 m on every guest. A
possible change (a teleport when the error stays above ~2.5 m for some seconds whatever the speed)
was not made without a reproduction (it also moves walking puppets).

## Verification
Not reproduced (no runs of the 4-player probe on fix/w2-creature). Next time: on the host the NPC's
velocity (`coop.npc list`, the sync entries) and on the guests `err` / `want` of that id.

## History
- 2026-10-23: first recorded in docs/known-issues.md.
- 2026-10-27: ticket backfilled from docs/known-issues.md (every entry carries its ticket id, CLAUDE.md 1).
- 2026-10-28: fix/w2-creature: not reproduced; a candidate cause from driver.h written down (see Fix). BUG-0044 (reused objects) ruled out as the cause: it shows another species or a corpse, not an offset.
