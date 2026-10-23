# Stability: the Civilization stage played by two bots

Test: `tools/tests/play_civ.py` (nightly suite: `play_civ`, 10 minutes). Lane 6 (instances 11 =
host, 12 = guest), fixture `civ_npc` (Taldon: the player capital on an island and three cities of
other nations, the nearest ~400 m away; new nations' cities appear over time). Branch `play/civ`,
2026-10-23.

## What the bot does

Two threads, one per player, act at the same time through real input on 1440x1080 windows; an
action every 1-3 s, chosen at random with weights like normal play:

* **Camera**: W/A/S/D pans until the point of interest is inside a box on screen (projection from
  the `coop.cam` view rows, the planet sphere hides points behind it), the wheel zooms but the
  altitude is kept at 150-420 m; the bot waits until the camera stops gliding before it reads
  screen positions.
* **Buy** (city menu): look at an own city (or the shared capital), click its city hall, click an
  enabled land / sea / air button (`coop.ui where 062D6902 ...`). Checked: the host made a vehicle
  owned by this player, money went down, the player sees the vehicle.
* **Move**: box-select own vehicles (`drag` around their `coop.targets` positions), right-click the
  ground 30-70 m away. Checked: the host has an order for them / they moved; foreign vehicles never
  end up selected.
* **Spice**: select a land vehicle, right-click the nearest free spice node. Checked: an order on
  the host, then the node owned by the player civ on host and guest.
* **Attack**: select own military vehicles, click the NPC target city, click the crosshair in its
  menu (window 0x037E95B9). Both players use the same target, so sieges are often joint. Checked:
  order 2 on the host, the vehicles drive there; the capture meter, ruins and the capture itself
  on both machines.
* **Co-op window (F9)**: switch the role with the arrows (checked on the host's `coop.role`), give
  selected vehicles to the partner (checked: owner on host and giver, giver's selection dropped).
* **City planner**: open it from the city menu, look, close it with the green check.
* **Captions**: a capture caption (host world stopped, `movie=1`) is answered with Enter after 2-5 s.
* **Alarm**: when the host counts an alarm, both players send vehicles to the attacked city.
* Noise: pause menu (Esc twice), camera moves.
* **Setup only** (console): 30000 money at the start, 10000 more when below 4000; halfway through
  `coop.civ chunk 300` (small snapshot chunks, several per cycle, like a big planet).

Measured every 8 s (own console connections, not the bots'): money on both; every city's political
id, owner, buildings and ruins; vehicle sets and owners; `coop.sync` position error of every
vehicle (the closer of two host samples, like coop_sync.py); spice node owners; the largest
snapshot chunk; the pending checks of the bots' actions; freezes, crashes, faults (twoinst
monitors, `exc`). At the end: guest commands sent = executed + rejected on the host, and the
share of clicks that gave no order on the host, guest vs host (the same clicks: a land vehicle
sent onto water gets no order on either).

## Sessions

| # | seed | code | min | actions (host/guest) | result | notes |
|---|---|---|---|---|---|---|
| 1 | 1 | first bot | 0.2 | 12 | bot bugs | threads shared a console (mixed replies) |
| 2 | 2 | first bot | 4 | ~170 | FAIL | camera panning loop; NPC city growth on the guest found |
| 3 | 3 | + growth fix | 4.9 | 173 | FAIL | right click on an NPC city gives no order; given vehicles stayed selected; spice nodes, new NPC cities differ |
| 4 | 4 | + nodes, attack via menu | 7.3 | 139 | FAIL | capture caption stops the host's world (vehicles "stuck"), buildings after capture differ |
| 5 | 5 | + meter, cities, selection | 8.1 | 218 | FAIL | ruins kept on the guest, a purchase refused silently |
| 6 | 6 | + chunks, reconcile | (lost) | | | lane 6 wiped by update_check.py of another run |
| 6b | 6 | same | 11.3 | 213 (98/115) | FAIL (bot) | 2 captures, joint siege; only a counting mistake of the bot (host's own gifts) |
| 7 | 7 | + ruins | 10.9 | 413 (268/145) | **OK** | 1 capture, 4 ruins mirrored, 169 guest commands all executed |
| 8 | 8 | same | 10.1 | 319 (168/151) | FAIL (bot) | a new city 8 s late on the guest, a bought vehicle given away at once: both checks too strict |
| 9 | 9 | final | 10.9 | 266 (137/129) | **OK** | 1 capture by a joint siege (43 joint hits), 6 ruins mirrored, 93 guest commands all executed |
| 10 | 10 | final | | | | |

Typical numbers of the passing code (sessions 6b-9): vehicle position error p90 0.9-1.4 m
(2500-2900 samples, median 0.0), max 22-27 m (one moment per session, open: in session 9 an NPC vehicle on the far side of the
planet, 22.7 m for three samples in a row); guest commands 60-169, all
executed, 0 rejected; 5-6 new NPC cities founded on the host and replayed on the guest (60-210
local foundings blocked); 1-2 cities captured (capture meter on the guest up to 88-99 %), joint
siege hits 0-30, military role hits 100-132; 4-11 capture captions answered; money equal at every
audit; 0 faults, no freeze, largest snapshot chunk 1142-1152 bytes (limit 1152).

## Problems found and fixed

1. **NPC cities grew on their own on the guest** (more buildings than the host after a minute):
   the guest ran the other nations' AI growth and also got the host's buildings. Fixed: guests skip
   `CreateBuilding` from the AI growth paths. Commit aac14a5.
2. **Hundreds of rejected commands per minute**: the guest's AI ordered the mirrors of other
   nations' vehicles and the hook forwarded them to the host. Fixed: only own vehicles' orders are
   sent (aac14a5).
3. **Spice derricks only on the host**: a claimed node stayed free on the guest (and every NPC
   claim). Fixed: node owners in the snapshot, `cCommodityNode::Claim / Unclaim` on the guest
   (4e2f467).
4. **No siege meter on the guest**: the besieged city's capture progress stayed 0 there. Fixed:
   capture % and attacker in the snapshot, `SetCaptureProgress` (0f5d5c6).
5. **New NPC cities differed**: the guest's planet founded a city the host never had, and the
   host's new cities came without their first buildings. Fixed: `SpawnNextCity` blocked on guests,
   replayed when the host founds; the building broadcast refreshes the city list (0f5d5c6).
6. **A given vehicle stayed selected by the giver** (and the host could still order it). Fixed:
   only the owner keeps a vehicle selected (0f5d5c6).
7. **The host's capture caption froze everybody without a word**: now everybody is told; the
   caption itself still waits (open issue). `coop.civ` shows `movie=1` (0f5d5c6).
8. **Snapshot chunk 0 over the packet limit** (reported by the tribe bot for tribe.cpp; the civ
   chunk held every city, 24 vehicles, the nodes and a city's buildings: ~1100-1250 bytes on a
   grown planet, dropped for good by the connection). Fixed: chunks packed by a byte budget, the
   guest removes vanished vehicles only after a complete cycle; checked every audit and with small
   chunks in the second half of each session (4115454).
9. **Buildings removed on the host stayed on the guest** (after a capture the host clears the
   ruins: 1 building there, 3 on the guest). Fixed: a building list per city in the snapshot, the
   guest builds / removes the difference (4115454).
10. **Ruins whole on the guest** during a siege. Fixed: the reconcile damages the matching building
    (generic combatant damage, hp 0, state 2) (9297100).
11. **A refused guest purchase did nothing** (its button can be enabled while the host has no
    vehicle slot left). Now the guest is told why (4115454).
12. **Editing a building design said "evolve"**: the vote said "wants to evolve (Civilization)" and
    leaving the editor "evolved the species: everybody gets the new body". Texts fixed (b554d45).
13. Test tooling: `coop.targets` lists every city and vehicle with ids; `coop.ui where / at`;
    `coop.civ slots / nouns / buildings / chunk`; `order=` per vehicle, `capture= ruins= hp=` per
    city; `spore.sh deploy` re-installs a lane's loader (update_check.py had removed it).

## Open issues (docs/known-issues.md)

* The host's capture caption stops the world for everybody until the host presses OK.
* Single vehicle sync outliers of 20-27 m (one sample per session).
* The NPC AI still spawns vehicles on guests (killed by the next snapshot).
* No building placement through the city planner (no designs in the fixture; a tile click opens
  the building editor).
* Not played yet: religious and economic vehicles (the capital builds military ones), trade routes
  between players' cities by real input, superweapons by real input (the vote is tested in
  coop_civ_play.py).
