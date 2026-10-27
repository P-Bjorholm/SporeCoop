---
id: BUG-0184
title: Galaxy menu: a host that stopped answering right after "Co-op" during the intro (once)
status: open
kind: freeze
stage: Menu
reported: 2026-10-24
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

* `coop_ui.py` on lane 9 (GOG 2024, branch fix/gog2024-tests, 2026-10-24): once in 12 runs of the
  final code the host's console timed out right after its click on "Co-op" during the menu intro
  (the mod posts Escape to skip the intro, docs/re/space.md 7.9); the game log was overwritten by
  the next test before it was read. The other 11 runs (8 of them in a row) passed. An earlier version
  that ended the intro with EndCinematic in the menu broke the menu twice (an access violation in
  `UTFWin::Window::SetFlag`, a game that stopped reacting): that one is gone. If it comes back, keep
  `out/lab/<run>/ui.host.game.log` and look for what the main thread did after "Co-op clicked
  during the menu intro".

## Root cause
See the report (observed vs inferred as marked there).

## Fix
Not fixed yet.

## Verification

## History
- 2026-10-24: first recorded in docs/known-issues.md.
- 2026-10-27: ticket backfilled from docs/known-issues.md (every entry carries its ticket id, CLAUDE.md 1).
