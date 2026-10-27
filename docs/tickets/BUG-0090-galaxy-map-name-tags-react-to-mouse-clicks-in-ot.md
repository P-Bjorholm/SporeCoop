---
id: BUG-0090
title: galaxy map name tags react to mouse clicks in other windows
status: open
kind: bug
stage: Space
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#68
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: #68
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/68 (full text, discussion).

The partner and colony tags on the galaxy map are clicked through the global mouse state (`GetAsyncKeyState`, `GetCursorPos`), without checking that the game window has focus. A click in another window that lies over the game, on the spot where a tag was drawn, starts a trip in the game: a jump to the partner, or a paid flight to a partner's colony. Severity: minor (wrong behaviour in windowed mode).

### Where
- `click_tick`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/space_map.cpp#L831-L855
- What a click runs (`fly_to` with payment, or `go_to_partner`): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/space_map.cpp#L822-L829

### How to reproduce
1. Host and guest in Space, connected. The host is in the galaxy view in a window, a partner's tag visible.
2. Put another window (e.g. a text editor) over part of the game window, covering the tag, or alt-tab and leave a window over it.
3. Click in that other window where the tag is under it.
4. The game starts the trip (`coop.space map status` shows `clicks=` and `trips=` growing). For a colony tag the trip's price is taken from the money.

### Expected
Only a click that goes to the game window triggers a tag.

## Root cause
**What happens**

1. Every tick in Space, `click_tick` reads `GetAsyncKeyState(VK_LBUTTON)`. That is the physical button state, whichever window has the focus.
2. On a press edge it takes `GetCursorPos`, converts it with `ScreenToClient(game window)` and tests it against the tag boxes drawn last frame.
3. Nothing checks `GetForegroundWindow() == w`, or that the point is the game's (the window under the cursor). So a click in a browser, chat app or overlay over the game's client area counts as a click on a tag.
4. `g_tags` is filled whenever the overlay drew the map, which also happens while the game window is in the background but still rendering.
5. Spore keeps rendering in the background under Wine and in windowed mode on Windows, though the conditions are not verified.

### Confidence
CONFIRMED by reading (no focus or hit-window check in `click_tick`). That the game keeps drawing the overlay while unfocused depends on the setup and is not verified on every platform.

## Fix
Not fixed yet. Suggested in the review:

At the top of the click handling: `if (GetForegroundWindow() != w) { g_lmb = down; return; }`. Also check `WindowFromPoint(screen point)` is the game window (or its child) before `ScreenToClient`. The tests use SporeKit's virtual cursor and the game window is the foreground window there; if a lane runs headless without focus, SporeKit's hook can report the game window as foreground for tests.

## Verification
Not done yet. Planned test (must FAIL without the fix and PASS with it):

In `tools/tests/coop_space_map.py`, add a case:
1. The host is in the galaxy view with a partner's tag drawn (`coop.space map tags`).
2. Give focus to another window (a small helper window on the headless display, `tools/headless.sh`), positioned over the tag.
3. Click there with a real input event (xdotool on the headless display).
4. Assert on the host that the active star (the game's `GetActiveStarRecord`) and the money are unchanged.
5. Then click the tag in the game window and assert the trip happens.
6. Fails on main (the first click travels), passes with the fix.

### Related
- docs/re/space.md section 13 (galaxy map tags).

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #68.
- 2026-10-27: ticket opened from the issue.
