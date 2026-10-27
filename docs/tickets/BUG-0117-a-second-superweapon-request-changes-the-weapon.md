---
id: BUG-0117
title: a second superweapon request changes the weapon and target of the vote already running
status: open
kind: bug
stage: Civ
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#96
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: #96
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/96 (full text, discussion).

A superweapon is fired through a vote of all players. The weapon type and target of the vote are kept in static variables that are written *before* the code knows whether a new vote can start. A second request made while a vote is running fails to start a vote, but it has already overwritten the type and target. When the first vote passes, the game fires the second requester's weapon at the second requester's target. The notice still says the first player wants to fire it.

### Where
- Guest request on the host: `s_type` / `s_pos` / `s_civ` are assigned before `C.vote(...)`, and the vote's lambda reads them when it runs: [civ.cpp#L713-L731](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/civ.cpp#L713-L731)
- The host's own requests in `h_FireSW` do the same with their own statics: [civ.cpp#L684-L711](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/civ.cpp#L684-L711)
- `start_action_vote` returns false while any vote is active: [coop.cpp#L433-L449](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L433-L449)

### How to reproduce
- 3 players in the Civilization stage (host, guests B and C), with superweapons available.
- B fires a superweapon at an enemy city; the vote opens on all machines.
- Before anyone is ready, C fires a different superweapon at another target.
- Everybody presses "I'm ready" (or the 60 s run out).
- On every machine, the superweapon that fires is C's type at C's target, under B's vote text.

### Expected
The weapon and target that fire are the ones of the vote that passed. A request made during another vote is refused with a notice to its sender, and it changes nothing.

## Root cause
**What happens**

1. Guest B fires superweapon X at target P. The guest's `h_FireSW` sends CMD_SUPER to the host, and `host_superweapon_request` stores X / P and starts the vote "B wants to fire a superweapon".
2. While that vote runs (up to 60 s), guest C fires superweapon Y at target Q. `host_superweapon_request` stores Y / Q in the same statics, then `C.vote` returns false because a vote is active. Nothing tells C its request was dropped.
3. The vote passes ("everybody is ready" or "time is up"). The lambda calls `o_FireSW(s_civ, s_type, s_pos)`, which fires Y at Q.

The same happens on the host alone. If the host triggers the superweapon twice during its own vote, the second call overwrites `h_FireSW`'s statics and the first vote fires the second weapon and target. The two functions have separate statics, so a guest request does not change a host vote or the other way round.

### Confidence
CONFIRMED by reading. Verified by a second reviewer: low-medium severity. With 2 players it only happens when the same player requests twice during its own vote; with 3+ players two guests are enough.

## Fix
Not fixed yet. Suggested in the review:

Write the statics only after `C.vote(...)` returned true. Better, capture type, target and civ by value in the lambda (`[type, pos = std::array<float,3>{...}, pc]`) and drop the statics. Tell the sender when its request is refused (MSG_NOTICE to `from`). Keep the old behaviour behind `coop.fix sw_vote_capture off`.

## Verification
Not done yet. Planned test (must FAIL without the fix and PASS with it):

A 3-player test on top of `twoinst.start` plus `coop_multi.start_extra` with a civ fixture that has superweapons.
- B runs `coop.civ super <type A>`, then C runs `coop.civ super <type B>` during the vote; then `coop.vote go` on the host.
- Assert the game's own result on the guests' machines (the effect of the fired superweapon: the target city's state or the superweapon cooldown of type A, not a mod counter): type A fired at B's target, and C got a refusal notice.
- It must FAIL on main (type B fired) and PASS with the fix.

### Related
#21 (a vote still runs after its stage or session is gone): related vote lifetime problem, not a duplicate.

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #96.
- 2026-10-27: ticket opened from the issue.
