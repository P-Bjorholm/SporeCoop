---
id: BUG-0131
title: a disabled, knocked-out, dead or frozen partner counts as the partner who rescues you
status: open
kind: bug
stage: Space, Cell
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#110
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: #110
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/110 (full text, discussion).

The co-op rescue rules say a partner close by repairs a disabled ship (Space) or revives a knocked-out cell (Cell). The code that looks for that partner does not check whether the partner can help.
- **Space:** a partner whose own ship is disabled, who is dead, whose game hangs or loads, or who stopped sending states counts as "close by". The last state it sent is enough, for up to the 30 s session timeout.
- **Cell:** a knocked-out partner counts, because a knocked-out cell is kept at HP 1 and is never flagged dead.

So two downed players next to each other repair or revive each other, and a lethal hit becomes a knockout when the only "helper" cannot help. It is possible with 2 players, but with 3 or 4 players two players being downed at the same spot is common (the same pirate or predator).

### Where
Space:
- `spc.partners` only checks `echo`, an empty buffer and the mode. It lacks the age, `PF_IN_WORLD` and `PF_DEAD` checks that `partners()` has: [coop.cpp#L2908-L2919](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L2908-L2919) vs [coop.cpp#L2279-L2291](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L2279-L2291)
- `partners_here` uses it and ignores `g_partner_down`: [spacecoop.cpp#L111-L122](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/spacecoop.cpp#L111-L122)
- `filter_own_damage` disables the ship instead of letting it die when `partners_here()` is not empty: [spacecoop.cpp#L145-L172](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/spacecoop.cpp#L145-L172)
- `down_tick` repairs the ship when any partner from `partners_here()` is within the repair distance for 2 s: [spacecoop.cpp#L202-L224](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/spacecoop.cpp#L202-L224)
- `g_partner_down` is kept from K_DOWN but only used for the beacon: [spacecoop.cpp#L750-L762](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/spacecoop.cpp#L750-L762)

Cell:
- A knocked-out avatar is held at HP 1 (`go_down`, `downed_tick`): [cellsync.cpp#L270-L338](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L270-L338)
- The cell state sets `PF_DEAD` only at HP <= 0, so a downed cell is never dead to the others: [modes.cpp#L372-L375](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/modes.cpp#L372-L375)
- `nearest_partner` takes `partners()` and ignores `g_ko`: [cellsync.cpp#L144-L165](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L144-L165)

### How to reproduce
- Space: 3 players in one system. `coop.space hit <dmg>` (or a real pirate) on guest B until it is disabled, then the same on guest C next to B. Within 2 s both machines show "your ship is repaired". Variant: guest B stops its game (SIGSTOP the process, or a modal dialog); guest C takes a lethal hit next to B's last position and is disabled and then repaired.
- Cell: 3 players in one pool. `coop.pool hurt` (or a predator) knocks out B, and C right next to it. Both revive after 1.5 s.

### Expected
Only a partner who is playing can rescue or repair. That means a recent state (under 3 s old), in the world, not dead, not disabled (Space) and not knocked out (Cell). Two downed players wait together, and are repaired or revived only when a third, working partner comes. If nobody can help, the normal death happens.

## Root cause
**What happens**

Space, 3 players in one system:
1. A pirate disables B's ship. B is invincible at HP 1 and sends K_DOWN.
2. The pirate then hits C's ship lethally next to B. `filter_own_damage` finds B in `partners_here()`, so C is disabled instead of destroyed.
3. Both ships' `down_tick` see the other within the repair distance. After 2 s both are repaired ("your ship is repaired") without any working partner.

The same happens with a partner whose game froze or is loading: `spc.partners` keeps its last state (mode Space, old position) until the 30 s timeout. That partner can rescue and repair you while it is not even playing. A partner in its own death cinematic counts too.

Cell, 3 players in one pool:
1. A predator knocks out B and C side by side.
2. Each one's `downed_tick` finds the other as the nearest partner within `revive_dist`.
3. After 1.5 s both are revived ("C revived B", "B revived C").

A knocked-out partner also makes `go_down` knock you out instead of the normal death, and the respawn "next to the partner" can put you next to a knocked-out cell.

### Confidence
- CONFIRMED by reading. Verified by a second reviewer, who also found the missing age / `PF_IN_WORLD` / `PF_DEAD` checks in `spc.partners`.
- Severity: medium in Space (frozen or loading partners too), low in Cell.

## Fix
Not fixed yet. Suggested in the review:

- Give `spc.partners` the same filters as `partners()`: age under 3 s, `PF_IN_WORLD`, not `PF_DEAD`.
- Skip `g_partner_down` entries in `partners_here` when it looks for helpers (`filter_own_damage`, `down_tick`).
- In Cell, skip partners whose `g_ko[id].state == KO_OUT` in `nearest_partner` for `go_down`, `downed_tick` and the respawn spot. Alternatively, send `PF_DEAD` (or a new "down" flag) while knocked out.
- Keep the old behaviour behind `coop.fix rescue_needs_helper off`.

## Verification
Not done yet. Planned test (must FAIL without the fix and PASS with it):

Space: a 3-player test (`twoinst.start` plus `coop_multi.start_extra`, the `space` fixture).
- Disable B, then hit C lethally next to B.
- Assert the game's own state: C's ship is destroyed (its UFO state 2, death cinematic), and neither ship's HP goes back up while nobody else is near.
- Second case: freeze B's process, and C's lethal hit destroys C.

Cell: the same idea with the `cell` fixture. Knock out B and C side by side, and assert both stay translucent at HP 1 until a third cell arrives (or they respawn).

Both must FAIL on main (mutual repair or revive) and PASS with the fix.

### Related
#39 and #48 (knockout state not reset or stuck), #41 and #76 (other "ship disabled" problems). Not duplicates.

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #110.
- 2026-10-27: ticket opened from the issue.
