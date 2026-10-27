---
id: BUG-0151
title: while the host is in the menu, an editor or a load, Tribe/Civ guests freeze and guest DNA is lost
status: open
kind: bug
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#130
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: #130
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/130 (full text, discussion).

"Host away" exists only for the creature NPCs, and only while the host is in the galaxy menu.
- While the host is in the menu, Tribe and Civ guests are told "you play on alone", but every citizen or vehicle on their screen stands still and their orders go nowhere.
- While the host is in an editor or a loading screen (no "away" at all), Creature proxies freeze, and DNA a guest earns is sent to a host that drops it.
- Goal-card DNA a guest earns is lost even in the menu case.

Severity: medium-low (no crash; progress lost and a frozen game for minutes).

### Where
- "Away" means only `mode == kMenu`: [coop.cpp#L352-L358](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L352-L358). The message: [coop.cpp#L369-L397](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L369-L397).
- Only npc reads `host_away`: [npc.cpp#L93-L106](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/npc.cpp#L93-L106), [npc.cpp#L584-L600](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/npc.cpp#L584-L600).
- Tribe:
  - every citizen's behaviour tree is skipped on a guest: [tribe.cpp#L201-L209](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/tribe.cpp#L201-L209);
  - orders go to the host: [tribe.cpp#L300-L350](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/tribe.cpp#L300-L350);
  - the host handles them only while it is in Tribe: [tribe.cpp#L1110-L1113](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/tribe.cpp#L1110-L1113).
- Civ:
  - vehicle behaviour trees are skipped: [civ.cpp#L200-L206](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/civ.cpp#L200-L206);
  - orders are forwarded: [civ.cpp#L274-L298](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/civ.cpp#L274-L298);
  - the host drops them outside Civ (`active()`): [civ.cpp#L1615-L1625](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/civ.cpp#L1615-L1625).
- DNA:
  - the guest sends what it earns to the host and does not apply it unless "away": [coop.cpp#L92-L108](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L92-L108);
  - the host applies `MSG_DNA_ADD` only with an avatar, and `game::avatar()` is null in the menu, editors and loads: [coop.cpp#L1553-L1560](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1553-L1560), [game.cpp#L338-L344](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/game.cpp#L338-L344).
- Goal cards: `social::goal_dna` runs before the away check, sends `K_GOAL` and drops the local reward: [social.cpp#L250-L272](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/social.cpp#L250-L272). The host pays it only with an avatar: [social.cpp#L574-L585](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/social.cpp#L574-L585).

### How to reproduce
Tribe (2 players, Tribe fixture):
1. Host and guest play together.
2. Host: pause menu, exit to the main menu.
3. Guest: after the "you play on alone" notice, order own members to gather or walk. Nothing moves; `coop.tribe` on the guest shows the behaviour-tree skip counter growing.

Civ: the same with vehicles.

Creature DNA (2 players):
1. Host opens the creature editor while the guest stays in the stage (guest not ready, the vote times out, or the guest's own editor did not open).
2. Guest: eat or kill to earn DNA; `coop.status` shows `dna_fwd` growing.
3. Host returns: the guest's DNA total equals the host's, and the earned DNA is missing.

The same for a goal card completed while the host is in the menu.

### Expected
- While the host is not playing the shared world (menu, editor, load, other world), every guest module either plays on locally or holds what the guest earns until the host is back. Tribe and Civ AI and orders run locally.
- DNA and goal rewards earned meanwhile reach the pack when the host returns, as `g_solo_dna` already does for the menu case.

## Root cause
**What happens**

1. Host in the galaxy menu for more than 1.5 s: `host_in_menu()` is true. The guest gets "<host> (host) left for the main menu: you play on alone until <host> is back", and npc stops removing the guest's own NPCs.
2. tribe and civ have no such state. On the guest, `h_BTTick` / `h_BT` keep returning 0 for every citizen or vehicle, because they are all mirrors. `h_DoActionForObjects`, `h_WalkSelected` and `h_SetOrder` keep sending `MSG_TRIBE_CMD` / `MSG_CIV_CMD`, and the host drops them because it is not in the stage. Nothing on the guest's planet moves, and the player cannot do anything until the host is back.
3. Host in an editor (an evolution or design change in any stage, or one the host opens while the guest is not in its own) or on a loading screen: not "away".
   - Creature: the guest's NPCs are still removed by the update filter, while the host sends no NPC states (its `npc_stage()` is false), so the proxies stand frozen.
   - Cell: the mirrors stay AI-locked.
4. DNA in that time (Creature): `on_dna` sends `MSG_DNA_ADD` and returns false, so the game does not add it. The host is in its editor or loading, so `game::avatar()` is null and the DNA is dropped.
5. Goal cards: `social::goal_dna` reports the reward as `K_GOAL` and drops it locally, also while the host is "away" (it runs before the away branch). The host drops it without an avatar. These rewards are lost in all three cases.

### Confidence
CONFIRMED by reading. A second reviewer verified it independently. Not run in a game: the frozen tribe and civ follow directly from the hooks; the DNA loss from `game::avatar()` being null outside Creature and Adventure.

## Fix
Not fixed yet. Suggested in the review:

- Make "host away" one state: the host is not in our place (`!host_here()` for more than about 1 s, mode and world), not only the menu. Pass it to tribe, civ and cellsync:
  - no behaviour-tree skip while away;
  - orders through the original function;
  - mirrors released (AI unlocked).
- Keep DNA (and `K_GOAL` rewards) earned while the host is not playing in the guest's `g_solo_dna` instead of sending it.
- On the host, queue `MSG_DNA_ADD` and `K_GOAL` that arrive without an avatar instead of dropping them.
- Keep the old behaviour behind `coop.fix host_away off`.

## Verification
Not done yet. Planned test (must FAIL without the fix and PASS with it):

`coop_host_away_stages.py` (two instances):
- Tribe fixture: the host goes to the galaxy menu. The guest orders a member to walk 20 m. Assert on the guest that the game's citizen position moved by more than 10 m.
- Creature fixture: the host opens its editor. The guest gains DNA through the game (a kill or `coop.res`-like debug add through the game function). The host returns. Assert the host's game DNA total (`GetEvolutionPoints`) includes the gain.

Both fail on main and with `coop.fix host_away off`, and pass with the fix.

### Related
#63 (the "host is back" message after a disconnect), #60 (creature pointers kept while the host is away), #27 (offline changes to shared resources), #21 (votes after the stage is gone).

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #130.
- 2026-10-27: ticket opened from the issue.
