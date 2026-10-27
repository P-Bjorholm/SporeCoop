---
id: BUG-0173
title: Tribe: knocked-out members stand on the guest
status: open
kind: desync
stage: Tribe
reported: 2026-10-23
source: docs/known-issues.md (ticket backfilled 2026-10-27)
role: guest
versions: 0.7.2.4 (main) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Copied from docs/known-issues.md, where the entry stays up to date:

* The game's chieftain (and members of NPC tribes) lie knocked out with 0 health until they
  respawn; the host sends health 0, but `tribe.cpp guest_tick` applies only positive health to
  mirrors (a 0 might start the local death of a mirror), so on the guest they keep their last
  health (e.g. 0.4) and stand. Seen in play_tribe.py (enemy chief `#4 hp=0.0` on the host, `0.4`
  on the guest). The co-op chiefs use the host's health on every machine since 2026-10-23.

## Root cause
See the report (observed vs inferred as marked there).

## Fix
Not fixed yet.

## Verification

## History
- 2026-10-23: first recorded in docs/known-issues.md.
- 2026-10-27: ticket backfilled from docs/known-issues.md (every entry carries its ticket id, CLAUDE.md 1).
