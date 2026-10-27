---
id: BUG-0104
title: the co-op window keeps showing "Connecting to ..." for the whole session and hides the update line
status: open
kind: bug
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#83
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: #83
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/83 (full text, discussion).

The co-op window has one status string (`g_status`). It is set by the buttons (Join, Send my world, Leave) and is never cleared when the action finishes or fails. The old text then sticks around:

- In the lobby it is shown as the "world line" every time there is no transfer or editor hint. So a guest playing normally reads "Connecting to 1.2.3.4:47810..." under the player list for the entire session.
- On the connect page it wins over the update line. After any join attempt, "SporeCoop X is available", the download percentage and "Update failed: <why>" are no longer shown. The Update / Try again button stays visible next to the unrelated text.

### Where
- Status set, never cleared on success/failure: [coop_ui.cpp#L305](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop_ui.cpp#L305) ("Connecting to ..."), [#L495](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop_ui.cpp#L495) ("Sending the world to everyone..."), [#L499](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop_ui.cpp#L499) ("Left the session")
- Lobby world line falls back to the status: [coop_ui.cpp#L622-L623](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop_ui.cpp#L622-L623)
- Connect page: the status hides the update line, the button stays: [coop_ui.cpp#L589-L594](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop_ui.cpp#L589-L594)
- `ui_world_line` returns "" in normal play: [coop.cpp#L2248-L2276](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L2248-L2276)
- Disconnect callback (feed + chat log, not the status): [coop.cpp#L1490-L1493](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1490-L1493)

### How to reproduce
- Join any host (or one that does not answer). Play; open F9: the lobby says "Connecting to <address>...". `coop.uistate` prints `world 'Connecting to ...'`.
- With an update available (`[update] url` pointing at a newer test release), press Join once on an unreachable address and wait for the timeout: the connect page shows "Connecting to ..." and the Update button, but not the "SporeCoop X is available" line.

### Expected
The status describes the current state: cleared (or "Connected to X") once welcomed, replaced by the disconnect or reject reason when the session ends. The update line is never hidden by an old action message.

## Root cause
**What happens**

1. A guest types an address and presses Join. The status becomes "Connecting to 1.2.3.4:47810..." (yellow).
2. The join succeeds and the world loads. `ui_world_line()` is empty while nobody is in an editor, so the lobby shows "Connecting to 1.2.3.4:47810..." under the player list for as long as the session lasts.
3. If the join fails (reject, "no answer from host (timeout)") or the connection is lost later, the window falls back to the connect page and still says "Connecting to ...". The real reason is shown for a few seconds in a notification and goes into the chat log (coop.cpp:1492-1493). The window itself never shows it.
4. From now on the connect page's status line never shows the update line. A player who opens the window to update sees "Connecting to ..." next to an "Update" or "Try again" button and cannot read the download progress or why the update failed.

The host has the same problem: after "Send my world", "Sending the world to everyone..." stays on the lobby world line. After "Leave", "Left the session" is printed over a later update line.

### Confidence
CONFIRMED by reading, low severity. A second reviewer verified it, with one correction: the failure reason is not lost entirely, it reaches a notification and the chat log.

## Fix
Not fixed yet. Suggested in the review:

- Clear `g_status` from the connected callback.
- Set it to the reason in the disconnected callback (an Api hook such as `A.on_status(text, color)`, or let coop_ui read a "last session event" string from coop.cpp).
- Make "Sending the world..." and "Left the session" time out after a few seconds.
- Show the update line on its own row, not instead of the status.

## Verification
Not done yet. Planned test (must FAIL without the fix and PASS with it):

Extend `tools/tests/coop_ui.py`:
- After the guest has joined and loaded the world, assert that `coop.uistate`'s `world` line does not contain "Connecting".
- After the host stops hosting, assert that the guest's `status` contains the disconnect reason.

FAILS on main 563f72d (the world line is "Connecting to ..."), PASSES with the fix.

### Related
#8 (join timeout after a lost first packet: the player then sees this stale text), #11 (cleanup on leave).

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #83.
- 2026-10-27: ticket opened from the issue.
