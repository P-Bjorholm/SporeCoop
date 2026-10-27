---
id: BUG-0145
title: with swapped mouse buttons, a click on a partner tag does nothing and a camera drag flies the ship
status: open
kind: bug
stage: Space
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#124
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: #124
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/124 (full text, discussion).

Two co-op features detect a left click by polling `GetAsyncKeyState(VK_LBUTTON)`:
- the partner and colony name tags on the galaxy map (click = fly there);
- the Co-op menu item during the galaxy-menu intro.

Windows documents that `GetAsyncKeyState` reports the **physical** mouse buttons, not the logical ones. With "Switch primary and secondary buttons" turned on (left-handed players), this goes wrong:
- The player's primary click (the physical right button) on a tag does nothing.
- The secondary button (the physical left button) triggers the flight. That is the button the game uses for its own secondary actions and camera drags.

### Where
- Galaxy map tag clicks: [space_map.cpp#L831-L855](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/space_map.cpp#L831-L855)
- Co-op item during the menu intro: [coop_ui.cpp#L802-L814](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop_ui.cpp#L802-L814)

### How to reproduce
1. Windows Settings, Mouse: "Primary button: Right".
2. Join a host in Space, open the galaxy map with a partner tag visible.
3. Primary-click the tag: nothing happens (`coop.space` status `tag_clicks` stays 0).
4. Press the other button over the tag: the ship flies to the partner.

### Expected
The tag reacts to the player's primary button, like every game button.

## Root cause
**What happens**

On a PC with swapped buttons:
- Clicking "Bob" on the galaxy map with the primary button does nothing.
- Starting a secondary-button action that begins over a tag starts `go_to_partner` / `fly_to` (a jump that costs energy, see #41).
- In the galaxy menu intro, clicking Co-op does not open the window. A secondary click on it presses Escape for the player and opens the window 3 s later.

### Confidence
CONFIRMED by reading, low severity. The physical-button behaviour is documented for Windows; not checked under Wine. A second reviewer verified it.

## Fix
Not fixed yet. Suggested in the review:

Poll `GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON`. Better: take the click from the game window's `WM_LBUTTONDOWN` (the window is already subclassed in sporekit/focus.cpp; messages are logical) and expose it to mods as an event. That also fixes #68 (clicks in other windows).

## Verification
Not done yet. Planned test (must FAIL without the fix and PASS with it):

A unit test is not possible for the API itself. Make the button choice a small helper `primary_button_vk(bool swapped)` and unit-test it. Alternatively, in a lab test:
- set `SPI_SETMOUSEBUTTONSWAP` on the lane's bottle;
- send a primary click (`input.click`) on a tag;
- assert that `tag_clicks` increments.

FAILS on main, PASSES with the fix.

### Related
#68 (same polling code: reacts to clicks in other windows), #41 (the flight it starts).

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #124.
- 2026-10-27: ticket opened from the issue.
