---
id: BUG-0113
title: after a resolution change the co-op window stays at its old position, partly off-screen
status: open
kind: bug
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#92
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: #92
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/92 (full text, discussion).

The co-op window (760 x 600) is centred once, when it is built the first time F9 or the Co-op item opens it. The window is then kept for the whole run (`add_ref`), and nothing moves it again. If the player later picks a smaller resolution in the game's options, the window keeps the coordinates computed for the old screen size. Its bottom and right parts can end up outside the screen.

### Where
- The only place the window is positioned: [coop_ui.cpp#L395-L398](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop_ui.cpp#L395-L398), in `build_dialog`, which runs only while `D.root` is null ([coop_ui.cpp#L708-L709](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop_ui.cpp#L708-L709)).
- The window survives mode changes on purpose: [coop_ui.cpp#L373](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop_ui.cpp#L373).
- SporeKit already fires `SK_EV_DEVICE_RESET` and updates `screen_size` on every Reset: [d3d9hook.cpp#L314-L330](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/d3d9hook.cpp#L314-L330). coop_ui does not listen to it.

### How to reproduce
1. Start at 1920 x 1080 and open F9 once (close it again).
2. In the game's options, set the resolution to 1280 x 720.
3. Join or host and press F9: `coop.uistate` shows `leave` / `ready` / `report` with y beyond 720, and the buttons are not visible.

### Expected
The window is centred on the current screen every time it opens (or after every device reset).

## Root cause
**What happens**

Example: the window is opened at 1920 x 1080, so its area is x 580..1340, y 240..840. The player switches to 1280 x 720 in the options. The window stays at 580..1340 / 240..840:
- The lobby's Ready / Send my world / Leave row (window y 488..528) lands at screen y 728..768, below the screen.
- The Report a problem button and the version footer are also off-screen.
- The right 60 px of the window are cut.

The other direction (small to large) leaves the window off-centre.

Alt+Enter is not a trigger: it calls SetVideoMode with -1 for the size and keeps the resolution ([docs/re/graphics.md](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/re/graphics.md#L20-L28)).

### Confidence
PLAUSIBLE, low severity. Our code never repositions the window; a second reviewer verified that. Not verified at runtime: whether the game's own layout re-anchors the account-dialog layout (0x34A6339E) after a resolution change.

## Fix
Not fixed yet. Suggested in the review:

In `refresh()`, when `g_want_open && g_raise` (the window is being opened), recompute the area from `H->screen_size` and call `sui::set_area(D.root, ...)`. Also do this from an `SK_EV_DEVICE_RESET` subscriber, through `run_outside_frame`. Remember the size the area was computed for, so the work is only done when it changed.

## Verification
Not done yet. Planned test (must FAIL without the fix and PASS with it):

A step in `tools/tests/coop_ui.py`:
1. Open the window and record the `close` widget's rect from `coop.uistate`.
2. Change the resolution through the game's video-mode call: `Renderer_SetVideoMode` (docs/re/graphics.md section 2), with a smaller width and height.
3. Reopen the window and assert that every widget rect lies inside the new `screen_size`.

FAILS on main (the Leave row is below the screen), PASSES with the fix.

### Related
None of the existing issues.

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #92.
- 2026-10-27: ticket opened from the issue.
