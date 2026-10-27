---
id: BUG-0160
title: small numeric edge cases (non-unit quaternion from a zero matrix, negative cell hp sent as 255, NaN cast to int)
status: open
kind: bug
stage: Cell
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#139
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: #139
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/139 (full text, discussion).

Three small numeric edge cases found in the same review. None of them has been seen in a game. Each one has a one-line fix.

### Where
**Where and what happens**

1. **`mat_to_quat` of a zero or degenerate matrix returns a non-unit quaternion.**
   https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/modes.cpp#L320-L348
   - For an all-zero matrix (a cell whose rotation at +0x5C is not written yet), `tr = 0` and the first three branches are false. The last branch computes `s = sqrt(1) * 2 = 2` and gives q = (0, 0, 0.5, 0).
   - That quaternion goes over the wire (cell states, `cell_fill`) and through `quat_to_mat` on the other side: `m[0] = m[4] = 0.5`, `m[8] = 1`. The mirror or puppet gets a matrix scaled by 0.5 in x and y, so it is drawn squashed until the next good state.
   - `nlerp` normalizes during interpolation, but the cell mirror path (`guest_state` to `quat_to_mat`) uses the raw value.

2. **Cell hp is sent as `uint8_t`, so a negative hp arrives as 255.**
   Host: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L976
   Guest: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L1019 and https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L1041
   - `w.u8((uint8_t)F<int>(c, kHp))`: if the game's hp of a cell is below 0 (for example a big bite takes more than is left), -1 is sent as 255.
   - The guest writes `F<int>(c, kHp) = 255` into its mirror. The mirror then looks fully healthy to the guest's game (health ring, aggression checks) until the dead flag arrives. Values above 255 also wrap (256 arrives as 0).

3. **The off-screen arrow casts a NaN or huge float to int.**
   https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1256-L1264 (same pattern in `cellsync.cpp` `draw_ko`, https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L1390-L1395, and `space_map.cpp:731-735`)
   - If `world_to_screen` returns NaN (a NaN position from the game, a camera not set up yet), `fabs(dx) > 1` is false for both axes and `k` = 1e9.
   - `(int)(cx + dx * k)` is then a float-to-int conversion out of range, which is undefined behaviour; x86 gives 0x80000000.
   - The label is drawn at x = INT_MIN minus half the text width (another float-to-int conversion). This is harmless today but undefined.

### How to reproduce
1. Call `modes::mat_to_quat` on `float m[9] = {0}` and print q: (0, 0, 0.5, 0), norm 0.5.
2. Host: `coop.cell` debug, set a host cell's hp to -1 (memory write at +0x244). The guest's mirror shows hp 255 in `coop.cell` status.
3. Feed a NaN position to `beacon` / `coop.ping`. The arrow is drawn at x = -2147483648.

### Expected
1. A unit quaternion: identity for a degenerate matrix.
2. A negative hp arrives as 0.
3. No out-of-range conversion: a non-finite screen position is skipped.

## Root cause

### Confidence
Confirmed by reading the code. A second reviewer verified it. Severity low (cosmetic or short-lived, no crash seen).

## Fix
Not fixed yet. Suggested in the review:

1. At the end of `mat_to_quat`, normalize q, and return identity when the norm is below 1e-6. Or normalize in `quat_to_mat` callers on the receiving side.
2. Host: `w.u8((uint8_t)std::clamp(F<int>(c, kHp), 0, 255))`.
3. `if (!std::isfinite(sc[0]) || !std::isfinite(sc[1])) continue;` before the arrow math, and clamp x and y to the screen before the cast.

## Verification
Not done yet. Planned test (must FAIL without the fix and PASS with it):

- `tests/net_tests.cpp`: move `mat_to_quat` / `quat_to_mat` to a portable header, then `CHECK(|q| == 1)` for the zero matrix and for random rotation matrices. It FAILS on main for the zero matrix (norm 0.5) and PASSES with the normalization.
- The hp clamp is covered by a cell test that kills a cell with an overkill bite and asserts the guest's mirror hp is 0 or the mirror is dead, never 255.

### Related
#10 (cell state packets), #19 (cell size after a respawn), tracking issue #82.

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #139.
- 2026-10-27: ticket opened from the issue.
