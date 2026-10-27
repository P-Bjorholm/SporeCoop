---
id: BUG-0163
title: a click right after the galaxy menu closes can make a virtual call on the freed "Co-op" menu button
status: open
kind: crash
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#142
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: #142
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/142 (full text, discussion).

`coop_ui::tick` checks every new left click against the galaxy menu's "Co-op" item. It calls `sui::visible(g_menu_btn)`, a virtual call on the window, before it checks that the menu (and its intro) is still there. `g_menu_btn` is cleared only by the outside-frame refresh, every 1 s while the co-op window is closed. So for up to 1 s after the galaxy menu is gone, a click calls through the vtable of a possibly freed window. Severity: low (short window; a crash if the memory was reused).

### Where
- The check, with `sui::visible` before `world::menu_intro_playing()`: [coop_ui.cpp#L802-L807](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop_ui.cpp#L802-L807).
- `sui::visible` -> `flags(w)` -> `vcall(w, 0x28/4)`: [sui.cpp#L133](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/sui.cpp#L133), [sui.cpp#L150](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/sui.cpp#L150).
- `g_menu_btn` is cleared only in `ensure_menu_button`, run from `refresh` (period 1 s while the window is closed): [coop_ui.cpp#L117-L124](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop_ui.cpp#L117-L124), [coop_ui.cpp#L832-L839](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop_ui.cpp#L832-L839).

### How to reproduce
2 players not needed:
1. Galaxy menu with the "Co-op" item visible and the co-op window closed.
2. Double-click a saved planet's Play (or click Play and click again at once).
3. With page heap or a debug allocator (or Application Verifier under Windows), the second click shows a read of freed memory in `sui::visible` from `coop_ui::tick`. Without one it usually reads stale but intact memory, so it rarely crashes.

### Expected
No access to a menu window after the game left the menu: the item pointer is dropped as soon as the mode is not GalaxyGameEntry, or only used after `menu_intro_playing()` (which checks the mode) is true.

## Root cause
**What happens**

1. In the galaxy menu, `ensure_menu_button` adds the item to the menu window and keeps `g_menu_btn` (a child of that window). The mod builds a new item for every galaxy menu, because the menu window is a new one each time (the comment about the fresh drawable).
2. The player starts a planet (or a co-op load starts it). The game leaves GalaxyGameEntry, and the menu window and its children are released.
3. Until the next `refresh` (up to 1 s, 0.25 s if the co-op window is open), `g_menu_btn` still points at the old item.
4. Any new left click in that time evaluates `lb && !g_lb_down && gw && g_menu_btn && sui::visible(g_menu_btn) && ...`. `visible` reads the object's vtable and calls slot 0x28. If the heap has given that memory to something else, this is a call through garbage. Only the last term, `world::menu_intro_playing()`, would have stopped it, because it is false outside the menu.

### Confidence
PLAUSIBLE. Confirmed by reading; a second reviewer agreed. Not checked: exactly when the game frees the menu window after the mode switch.

## Fix
Not fixed yet. Suggested in the review:

- Reorder the condition so `world::menu_intro_playing()` (cheap, mode-checked) comes before any `sui` call on `g_menu_btn`.
- Also set `g_menu_btn = g_menu_parent = nullptr` in `tick()` as soon as `A.mode() != modes::kMenu`.

No behaviour change for players, so a fix switch is not needed.

## Verification
Not done yet. Planned test (must FAIL without the fix and PASS with it):

`coop_ui_menu_leave.py` (one instance is enough):
- The menu is up and the item exists (`coop.uistate` shows `menu_button=yes`).
- Start a load from the menu, then send 10 left clicks in the next second (`sk click`).
- Assert no crash report and no fault in the log.
- Then assert, from the debug state after the mode change, that `menu_button=no` within one frame.

The last check fails on main (it stays "yes" up to 1 s) and passes with the fix.

### Related
#68 (galaxy map name tags react to clicks in other windows), docs/08-ui.md.

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #142.
- 2026-10-27: ticket opened from the issue.
