---
id: BUG-0187
title: Civilization: the host's capture caption stops everybody's game
status: open
kind: bug
stage: Civ
reported: 2026-10-27
source: docs/known-issues.md (ticket backfilled 2026-10-27)
role: host
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

* A captured city plays a cinematic whose caption waits for the host's OK; until then the host's
  world (and so every guest's vehicles) stands still. Everybody is told now ("... watches a
  cinematic: the world waits for the OK"), but a host who is away stops the game for all.
  Reproduce: capture a city (`play_civ.py`), do not press Enter on the host. Fix idea: close the
  caption on the host after a few seconds when guests are connected (EndCinematic 0xADFF20).

## Root cause
See the report (observed vs inferred as marked there).

## Fix
Not fixed yet.

## Verification

## History
- 2026-10-27: first recorded in docs/known-issues.md.
- 2026-10-27: ticket backfilled from docs/known-issues.md (every entry carries its ticket id, CLAUDE.md 1).
