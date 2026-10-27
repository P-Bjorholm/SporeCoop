---
id: BUG-0146
title: a member of an NPC tribe counts as a second player hitting the epic creature (80 food instead of 30)
status: open
kind: bug
stage: Tribe
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#125
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: #125
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/125 (full text, discussion).

The tribe co-op epic creature pays 80 food when "both players" (two or more owners) hit it, and 30 otherwise. The host tracks the members of *every* tribe, and NPC tribes' members get owner 0xFE ("nobody"). `note_animal_hit` adds the attacker's owner to the hitter set without filtering 0xFE. So a hit by an NPC tribe's member counts as a second player, and one player plus an NPC villager earns the "together" reward.

### Where
- Members of NPC tribes are tracked with owner 0xFE: [tribe.cpp#L414-L425](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/tribe.cpp#L414-L425)
- Every creature hit in the tribe stage reports its attacker: [npc.cpp#L776-L779](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/npc.cpp#L776-L779)
- `note_animal_hit` inserts any tracked owner, 0xFE included: [tribe.cpp#L1034-L1038](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/tribe.cpp#L1034-L1038)
- The reward counts the set's size: [tribe.cpp#L1014-L1031](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/tribe.cpp#L1014-L1031)

### How to reproduce
- Tribe stage, 2 or more players, an NPC tribe near the player village.
- On the host, `coop.tribe epic` (or wait for the epic). Only the host's members attack it, and an NPC tribe's member hits it at least once.
- Watch the host's food: +80 instead of +30.
- Whether NPC tribes attack an epic on their own is not verified; driving an NPC tribe member's attack with `coop.tribe act` makes it deterministic.

### Expected
Only the players' members (owner below 0xFE) count as hitters, so one player alone gets 30 food.

## Root cause
**What happens**

1. An epic creature comes to the village (host, `spawn_epic`).
2. Player A's members hit it, so `g_epic_hitters = {A}`.
3. A member of a nearby NPC tribe also hits it (the epic walks through its village, or the NPC tribe fights it). `g_hm[attacker].owner` is 0xFE, so `g_epic_hitters = {A, 0xFE}`.
4. The epic dies: `size() >= 2`, so the notice reads "epic creature down: +80 food" although only one player fought it.

The same set can never miss a real second player, so the bug only overpays. It is not specific to 3 or 4 players; it was found while checking how the owner ids of several players are counted.

### Confidence
- Code path CONFIRMED by reading. Verified by a second reviewer.
- The trigger is PLAUSIBLE: it depends on NPC tribe members attacking the epic. Low severity.

## Fix
Not fixed yet. Suggested in the review:

In `note_animal_hit`, insert the owner only when `owner < 0xFE`. Keep the old behaviour behind `coop.fix epic_players_only off` if the lab wants to check the test.

## Verification
Not done yet. Planned test (must FAIL without the fix and PASS with it):

On top of `twoinst.start` (`tribe` fixture):
- Spawn the epic, let only an NPC tribe member and the host's members hit it, then kill it.
- Assert the player tribe's food on both machines (the game's food bundles, `coop.res`) grew by 30, not 80.
- It must FAIL on main and PASS with the fix.

### Related
#45 (epic creature state survives a leave or a load). Not a duplicate.

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #125.
- 2026-10-27: ticket opened from the issue.
