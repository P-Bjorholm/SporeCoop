---
id: BUG-0175
title: coop_sync.py: mirrors trail more at low frame rates, the GOG 2024 guest pauses after a load (the test failures were fixed 2026-10-24, fix/gog2024-sync)
status: open
kind: bug
stage: All
reported: 2026-10-24
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

* The full lab run 20261023-225702 (feat/space-07, lane 10, 9 lanes busy) failed `coop_sync` twice:
  cell/players facing p90 15.6°, tribe/citizens 2.39 / 2.13 m, civ/vehicles 4.4 / 6.0 m and 33.6°,
  space/ships 33 m (first run). It had never run on GOG 2024, and ran on the new fixtures.
* Cause 1 (GOG 2024, test timing): the 2024 guest keeps its game time paused (cGameTimeManager
  +0x48 = 3, black screen, ~23 fps) for ~7 s after its world load in the tribe stage, exactly the
  window the test measured in. Citizens do not get `Update` then (`coop.tribe` on a guest prints
  `ai_skipped=0/0`), so mirrors do not walk and follow the host by drive.h's per-frame 15 % nudge
  only: the error is speed x frame time / 0.15 (2.4 m at 5.5 m/s and 15 fps; the lab's load lowered
  the frame rate). Forcing 15 fps on both builds (`frame.limit 66`) reproduced it on 2024 (citizens
  2.7 m) and not on 2016 (0.6 m). No wrong symbol: once the game time runs the 2024 mirrors walk.
  Players do not see it (the screen is black then).
* Cause 2 (test, both builds): the facing checks counted the guest's designed lag. The cell host
  turns every 4th round and the civ fixture has a single vehicle that turns in place, so 1 of 8
  samples sat at 15-30° (raw p90 2016: 12-21.5°, 2024: 2.6-18°).
* Vehicles at a low frame rate trail by ~2 m on both builds (15 fps: 4.2 m on 2016 and 2024), which
  is where the lab's 4-6 m came from.
* Fix: coop_sync.py waits until neither game is paused and compares within a 0.15 s lag window
  (docs/06-testing.md 6.5 "Sync accuracy"). After: 3 of 3 passes on lane 10, 2 of 2 on lane 3.
* Not fixed: drive.h's nudge is per frame, so a mirror that cannot walk trails more at low frame
  rates; the 2024 guest's paused seconds after a load are not explained (planet still loading?).

## Root cause
See the report (observed vs inferred as marked there).

## Fix
Not fixed yet.

## Verification

## History
- 2026-10-24: first recorded in docs/known-issues.md.
- 2026-10-27: ticket backfilled from docs/known-issues.md (every entry carries its ticket id, CLAUDE.md 1).
