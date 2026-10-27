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
branch:
commits:
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
Not fixed yet. Suggested in the review:

Take the keys from the game window's `WM_KEYDOWN` (not repeated: bit 30 of lParam clear), and only when no UTFWin text edit has keyboard focus. Even better, hook the game's ability key handler, so only key presses the game accepts count. Drop bit 0 of `GetAsyncKeyState`.

## Verification
Not done yet. Planned test (must FAIL without the fix and PASS with it):

Extend `tools/tests/coop_creature_social` (or the social test):
- after engaging, open the cheat console on the guest and type "1234" with `text`;
- assert on the host that the guest's act count did not change.

FAILS on main 563f72d, PASSES with the fix.

### Related
#68 (the same kind of raw polling for mouse clicks).

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #128.
- 2026-10-27: ticket opened from the issue.
