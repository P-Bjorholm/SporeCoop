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

RESULTS

## Bugs found and fixed

FIXES

## Open issues

OPEN
