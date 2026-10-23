# Stability: the Tribe stage played by two bots

Test: `tools/tests/play_tribe.py` (nightly suite: `play_tribe`, 10 minutes). Lane 4 (instances 7 =
host, 8 = guest), fixture `tribe` (Cohrach: the player tribe with 3 members, one NPC tribe "Pink
Village" with 4, animals, fruit trees). Branch `play/tribe`, 2026-10-23.

## What the bot does

Two players (the host and the guest) play together through real input on 1440x1080 windows, like
two friends: they agree on a plan for 70-120 s at a time (gather, hunt, raid the other tribe
together, socialize with it together, kill the epic creature together when one comes) and do
something of their own one action in four (gather, hunt, click the partner's members, move the
camera, give a member, walk).

* **Looking around**: the minimap (calibrated at the start: 3 clicks, `coop.avatar` reads the
  planet point under the mouse), the mouse wheel (bounded zoom), the arrow keys, right-drag to
  turn the view (then the minimap is calibrated again).
* **Own members**: a member that is not busy (a gatherer is left alone 40 s, a hunter 25 s) is
  looked at and box-selected (`drag`); raids and the epic take a box around several members.
  Hurt members (below 60 % of their best health) are sent to eat at the food mat (heal).
* **Orders**: hover the target ~0.4 s, right-click it; a click that becomes a walk (the pick
  missed the model) is repeated 10 / 20 / 35 px higher, then another target is tried. Targets:
  fruit trees with fruit left near the village (gather), the animals nearest to the village
  (hunt), the other tribe's members and hut in the attack mode (raid), its hut in the social
  mode (socialize: the click becomes CitizenAction 9, a food gift: the instruments are locked in
  the fixture), the epic creature.
* **The partner**: left-clicks, boxes and right-click orders on the partner's members; gifts of
  one member through the co-op window (F9) and `coop.give`.
* **Setup only**: `coop.tribe spawn 3` gives each player 3 members.
* **Game over**: when the player tribe dies out the host presses OK on the "population below the
  minimum" caption (the game bounces back to the last save) and play goes on.

Measured every 10 s and per order: the order a click became (host `tribe last local order`, guest
`tribe last order`), whether the host executed it (guest orders: not rejected) and the ordered
members moved or the target lost health 8 s later; the shared food on both machines; the player
tribe's members, owners and chiefs on both machines; selected partner members; `coop.sync`
position / facing errors of citizens (t*) and animals (n*); the co-op counters; freezes, crashes,
faults (twoinst monitors, `exc`).

## Sessions

Final bot (all fixes below), lane 4, 11 minutes each. Orders: given by a real click and
followed (members moved or the target lost health on the host) / lost (the host rejected a
guest order) / idle (arrived, nobody moved) / clicks that gave no order or only walks.

| seed | result | host orders ok/lost/idle/none | guest orders | food start -> end (rises) | citizens at the end | t* p90 / max | n* p90 | co-op counters |
|---|---|---|---|---|---|---|---|---|
| 21 | FAIL (the guest gave only 6 orders: after the bounce back its selections failed; host-side outliers of creatures the host stopped tracking, now ignored) | 25/0/0/4 | 6/0/0/2 | 20 -> 78 (18), 1 game over | 7 | 1.28 / 2.5 m | 0.80 m | aura 22, rescue 1 |
| 22 | FAIL (1 guest order rejected, cause not logged) | 23/0/0/2 | 11/0/2/1 | 20 -> 157 (20) | 32 (5 tribes) | 0.77 / 3.2 m | 0.76 m | pincer 12, aura 5 |
| 23 | OK | 28/0/0/0 | 17/0/3/5 | 20 -> 216 (21) | 31 | 0.75 / 3.2 m | 0.74 m | aura 43, **epic killed together (+80)** |
| 31 | OK | 13/0/1/1 | 24/0/2/2 | 20 -> 105 (17) | 29 | 0.75 / 3.0 m | 0.73 m | pincer 20, aura 4, rescues 2 |
| 32 | OK | 25/0/2/0 | 21/0/1/6 | 20 -> 133 (19) | 32 | 0.72 / 3.2 m | 0.73 m | - |
| 33, `SPORE_NET=mobile` | OK | 19/0/3/1 | 19/0/1/6 | 20 -> 159 (17) | 35 | 1.42 / 5.2 m | 0.99 m | pincer 51, aura 35, rescues 2, **epic killed together** |
| 41 | OK | 25/0/1/4 | 15/0/0/8 | 20 -> 122 (17) | 31 | 0.76 / 3.1 m | 0.72 m | pincer 26, aura 18 |

Bot limitations: the own food mat sits at the bottom edge of the default view and the bot rarely
finds it (heal orders: 4 in s7, then mostly "nothing"); raids are not coordinated in time (both
players raided the same tribe within 10 s only 0-1 times per session, the pincer still happened
through overlapping fights).
In every session: no freeze, crash or fault on either game, the shared food was the same on both
machines in every sample (~50 per session), the player tribe's members, owners and chiefs were
the same on both (after the fixes), nobody ever had a partner's member selected although each
player clicked, box-selected and right-clicked the partner's members 10-25 times per session (the
freeze once seen when selecting the partner's units did not come back), every gift (1-4 per
session) arrived on both machines. Facing p90 of citizens 2-10 degrees.

What happens in a typical session: 3 -> 6 members (setup), gathering brings +5 food per trip
(food 20 -> 100-220 in 11 minutes, healing at the food mat costs some), raids on the Pink
Village bring the pincer and the chief's aura into play (and kill members on both sides), new NPC
tribes form after ~5 minutes (2 -> 5 tribes, ~30 citizens), the epic creature comes after 8
minutes together and was killed together twice (+80 food); one session ended in a game over
("population below the minimum") and bounced back to the save on both machines.

Before the final bot: 11 sessions (seeds 1-11, 2-10 minutes) while the bot learned to play
(selection timing, the minimap, hovering before right-clicks, fruit trees) found bugs 1-5 below,
the game-over crash and the guest's Spore Guide (`H`: test pitfall, docs/06-testing.md 6.10).

## Bugs found and fixed

Each fix has a regression check (`tools/tests/coop_tribe.py`, smoke suite, unless noted).

1. **Guest gather orders on fruit trees were dropped** (6486341). Fruit trees are `cTreeData`
   (noun 0x6ABEE7E), not cGameData: the guest sent "noun 0x6ABEE7E at (0,0,0)" and the host
   rejected the order. The guest now sends trees by position, the host finds its own tree in the
   planet flora (`[0x167FB14]+0x1E5F0`, docs/re/tribe.md "Trees"). Check: a guest right-click
   on a fruit tree by real input becomes a gather order the host executes.
2. **A gifted member stayed selected by the giver** (2b97df5), and the host executes its own
   orders locally, so it could keep commanding a member it gave away. Every machine drops members
   of others from its selection. Check: after `coop.give` the giver has nothing selected.
3. **A knocked-out member stayed a player's co-op chief** (e0731bb): the game's chieftain lies
   with 0 health until it respawns, the chief's aura and rescue came from a body. Chiefs are
   picked among members with health, by the host's health on every machine (a guest's mirror
   never shows 0, which made the machines pick different chiefs: seen in play_tribe.py).
4. **On a crowded planet the guest lost the player tribe** (d5b1645). The host sent 24 citizens
   per snapshot chunk; 24 entries + the tribe table are ~1250 bytes, more than `kMaxPacket`
   (1200), and the connection drops such a message for good. New NPC tribes form after a few
   minutes (2 -> 5 tribes, 33 citizens): from 24 citizens on, the first chunk (with the player
   tribe) never arrived and the guest removed those mirrors every cycle; its tribe emptied while
   the host had 4 members (seed 11). Chunks are sized from the byte budget; a guest only cleans up
   after a cycle whose chunks all arrived. Check: 27 citizens (old code: 27 on the host, 3 on the
   guest).
5. **A dead animal's body shot off into the sky on the guest** (63f146b). A proxy killed locally
   leaves the planet at ~2.5 km/s; driven like a living proxy it hung ~80 m above the ground.
   Every hunted animal (12 at once in seed 11). Killed proxies are pinned to the host's corpse.
   Check: the hunted animal's body (old code: 77.7 m off); play_tribe.py fails on anything more
   than 50 m off.

Test tooling made on the way (875f5a2, 384b0bd, 8c09855): `coop.targets objs` (everything on
screen: fruit trees, huts, tribe centres), member ids in `coop.targets`, `tribe last order` /
`tribe last local order` / `tribe ui mode` in `coop.status`, `coop.tribe objs|trees|spawn`,
`coop.memfind` (who points at an object: how the tree container was found), `coop.npc list`
with every tracked creature.

## Open issues

* **Host fault + freeze when a member is added during the game-over caption** (fault 0x00C0DAE3,
  repro in docs/known-issues.md). Only the test command `coop.tribe spawn` does that today.
* **Knocked-out members stand on the guest** (a mirror never gets 0 health): docs/known-issues.md.
* **Duet and healer bonus never happen in play**: the fixture's tribe has no instruments and no
  healer tool (the palette says "Conquer or ally with other tribes to progress"), a social click
  is a food gift (CitizenAction 9). Needs a later tribe save as a fixture.
* **The tribe palette is locked** in the fixture: nothing could be bought (tools, babies).
* **A creature the host stops tracking** (a reload, a bounce back, an unloaded animal) stays in the
  host's NPC table for up to 2 s with a destroyed object (position 0,0,0); its id keeps the guest
  proxy alive meanwhile. If the game allocates a new creature at the same address within those
  2 s it would inherit the old id and species on the guest (not seen).
* Seed 21: after the bounce back (game over) the guest's box selections failed 38 times and it gave
  no more orders. The view resets on a reload and the bot now calibrates the minimap again after
  a bounce back; a forced bounce back (kill the tribe, OK on the host) then worked for the guest
  (selection and orders), so the stale minimap is the likely cause, not the mod. Not seen again.
* Seed 22: the host rejected one guest order (cause not logged then; the bot logs the host's
  reason since 2026-10-23).
* Civilization: the vehicle snapshot can have the same too-big-chunk problem as tribe had
  (docs/known-issues.md), not checked in play.
