---
id: BUG-0147
title: from the second time a ship is disabled on, its repair starts with a 0.5 s head start (25 %)
status: open
kind: bug
stage: Space
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#126
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: #126
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/126 (full text, discussion).

A disabled ship (the "ship disabled" rescue) is repaired when a partner stays within range for `kRepairHold` = 2.0 s. The time step in `down_tick` comes from a function-local `static double last`, which is updated only while the ship is disabled. When the ship is disabled again later (at least 60 s after the last time, because of the cooldown), the first step is `min(0.5, now - last)` = 0.5 s. A partner already close by gets 25 % of the repair in the first frame, so the repair takes 1.5 s instead of 2.0 s.

### Where
- `static double last = now;` and the clamped dt: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/spacecoop.cpp#L202-L226
- Disabling resets `g_repair_t = 0` but not `last`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/spacecoop.cpp#L145-L171
- `kDownTime = 20.0, kRepairHold = 2.0`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/spacecoop.cpp#L32

### How to reproduce
Two instances in Space, in the same system:
1. On the guest: `coop.space goto` (next to the partner), then `coop.space hit 100000`. The ship is disabled. Run `coop.space hp` every frame (lab: `sk wait.frames 1` loop); `g_repair_t` rises from 0 in frame steps and the repair completes after 2.0 s.
2. Wait 61 s, then `coop.space goto` and `coop.space hit 100000` again.
3. The first `coop.space hp` after the hit shows a repair time of 0.50, and the repair completes after about 1.5 s.

### Expected
Every repair needs the full 2.0 s of proximity.

## Root cause
**What happens**

- First episode: `last` is initialized on the first `down_tick` call, so dt = 0 and the repair takes the full 2.0 s.
- The repair ends with `end_down`, and `down_tick` returns early while `!g_down`, so `last` keeps the time of the end of the first episode.
- Second episode, at least 60 s later (`now - g_last_down < 60` blocks earlier ones): the first tick computes `dt = min(0.5, now - last)` = 0.5, so `g_repair_t` = 0.5 out of 2.0 while a partner is within `g_repair_dist` (60 m).
- The repair finishes after 1.5 s of proximity.

If no partner is close, the same 0.5 is subtracted instead (`max(0, g_repair_t - dt)`), which has no effect.

### Confidence
Confirmed by reading the code. A second reviewer verified it. Severity low (a balance and consistency bug, no crash).

## Fix
Not fixed yet. Suggested in the review:

Make `last` a file-level `g_down_last` and set it to `now` in `filter_own_damage` where `g_down` becomes true, and in `end_down`. Alternatively, use the frame dt that `cellsync`'s `symbiosis_tick` already computes, `min(now - g_last_tick, 0.5)`, which is updated every tick.

## Verification
Not done yet. Planned test (must FAIL without the fix and PASS with it):

`tools/tests/coop_space_repair.py` (two instances): disable the guest's ship twice, more than 60 s apart, with the host's ship next to it. Assert, on the guest's game state (`coop.space hp` ship state and HP), that each episode is repaired no earlier than 1.9 s after the disable. On main the second episode FAILS (about 1.5 s). With the fix both PASS (about 2.0 s).

### Related
#76 (ending the "ship disabled" state writes to the ship after Space was left, same feature), #41 (flying to a partner works while disabled), tracking issue #82.

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #126.
- 2026-10-27: ticket opened from the issue.
