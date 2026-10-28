---
id: BUG-0149
title: typing digits in the cheat console plays social abilities while a guest is socializing
status: open
kind: bug
stage: Creature
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#128
role: guest
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in:
branch: fix/w3-creature
commits: 4096536
tests:
signature:
docs:
related: #128
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/128 (full text, discussion).

When a guest is engaged in a social game with a mirrored NPC, the mod reads the keys 1-4 itself with `GetAsyncKeyState('1' + k)`, every frame. It does not use the game's input path. The only things that block it are the pause menu and the co-op window. The game's own text fields do not block it, for example the cheat console (Ctrl+Shift+C). Every 1, 2, 3 or 4 typed there plays that social ability on the avatar and sends a social round (K_ACT) to the host.

### Where
- Raw key poll: [social.cpp#L331-L344](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/social.cpp#L331-L344)
- The only block: [coop.cpp#L2682](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L2682)
- What a key does: [social.cpp#L315-L329](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/social.cpp#L315-L329)

### How to reproduce
1. Two instances in Creature stage; the guest walks to a mirrored NPC and starts a social game (`coop.social act <id>` once to engage, or a real social key).
2. Ctrl+Shift+C on the guest and type `1234`.
3. The guest's avatar plays four abilities; `coop.social log` on the host shows four acts from the guest.

### Expected
Typing in any text field does not play abilities. Only key presses that the game itself treats as ability keys count.

## Root cause
**What happens**

1. The guest is engaged with an NPC (social stance). The engagement lasts `kEngageTime` after the last act.
2. The guest opens the cheat console and types `coop host 47810` or `moneybags 1000`.
3. Each digit 1-4 in the text plays the matching social ability (sing, dance, charm, pose). Each counts as a round of the NPC's social game on the host and can finish it (impressed or refused) without the player meaning to.

The mask `0x8001` also uses bit 0 ("pressed since the last call"). Windows documents that bit as unreliable, because any program can consume it. A digit typed elsewhere (another window) can fire one ability on the first poll after the game is back in front.

### Confidence
CONFIRMED by reading, low severity (the cheat console case). The Alt+Tab case with bit 0 is plausible. A second reviewer verified it.

## Fix
Partly done on fix/w3-creature (4096536), not verified in the game:

* The cheat console: its window was seen once on a lane game (2026-10-28, `coop.ui tree` before and
  after Ctrl+Shift+C): container control id 0x014A4606 [0 400 800 600], its text edit 0x014A4608
  held the typed 'c'; the window exists only while the console is open. `menu_check_task`
  (coop.cpp, outside the frame every 0.2 s) looks it up with the pause menu and the social key poll
  is blocked while it is shown (`coop.social keys` prints text_up; `coop.social textfix off` = the
  old poll).
* Bit 0 of GetAsyncKeyState counts only while the game window was in front since the last poll.

Not done: other UTFWin text fields than the console; the game's own ability key path (a hook
there would be the clean fix).

## Verification
Not verified. Test `tools/tests/coop_social_console.py` (not in suite.py): it engages a Kook and
types 1/2/1 with the console open. The console could not be opened reliably on the lanes:
injected Ctrl+Shift+C opened it once and then never again in 6 tries (also on a fresh game), the
cheat manager's vt 0x44(1) did not open it either. `AppShortcuts_OnKeyDown` (0x814180) opens it
only when vk 0x43 comes with modifiers exactly 3 and `IWindowManager` (0x67CA60, global 0x15FCC4C)
vt 0x84 (returns +0x82C, a window: a full-screen window id 0xFFFFFFFF on the lane games) is null;
clearing +0x82C did not make it open either. The runs with the console closed (lane 5, 2026-10-28)
showed only the expected acts (1 per key press after cooldown).

Next: find what +0x82C is and how the console opens (or open it through its own layout), then
run the test without and with `coop.social textfix off`.

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #128.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fix/w3-creature: console window check and the bit-0 change made; the test cannot open the console on the lanes yet, so it stays open.
