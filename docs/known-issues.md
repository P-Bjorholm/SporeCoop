# Known issues

Bugs found and not fixed yet. Each entry: what happens, how to reproduce it, what is known.
Fixed entries move to the commit message (and a test).

## Guest: rare first-chance fault in a planet render while a world loads

* `exception 0xC0000005 at 0x00FB89D0 cTerrainStateMgr_GetTemperatureScore` (data 0x30, caller
  0xF9E5EC: a planet render gets a null terrain state), guest only, while it loaded a received world
  from the galaxy menu (2 of ~30 switches in `coop_stage_switch.py`, 2026-10-22). The game survives
  it. Not seen in 14 switches since `world.cpp` starts the load outside the frame; if it comes back:
  docs/re/space.md 8.2, and compare with the host (never seen there).

## Tribe: the host froze once while the epic creature was being killed

* `coop_tribe_play.py` on branch feat/net-window, lane 2 (2026-10-22): the host stopped drawing
  during `coop.tribe epic hit` (monitor: FROZE); not reproduced in 14 more runs. The game logs of
  that run were lost (now kept by lab.py). Hunt: `lab.py run tribe_play,chaos_tribe --repeat 10`.

## Civilization: ruins are whole buildings on the guest during a siege

* The host's besieged city loses buildings (state 2, ruins: `coop.civ` city line `ruins=`); the
  guest keeps them whole until the capture, when the host clears the ruins and the guest's
  building reconcile removes them too. The siege meter is right on both (synced). Reproduce:
  `play_civ.py` (attack phase), compare `coop.civ` `ruins=` / `hp=` of the target city on host and
  guest (`civ(guest)` status: `ruins_differ`). Fix idea: the generic combatant damage 0xBFCEF0
  (docs/re/civ.md 9.4) on the guest's building with the host's hp; check its message handlers with
  a null attacker first.

## Civilization: the host's capture caption stops everybody's game

* A captured city plays a cinematic whose caption waits for the host's OK; until then the host's
  world (and so every guest's vehicles) stands still. Everybody is told now ("... watches a
  cinematic: the world waits for the OK"), but a host who is away stops the game for all.
  Reproduce: capture a city (`play_civ.py`), do not press Enter on the host. Fix idea: close the
  caption on the host after a few seconds when guests are connected (EndCinematic 0xADFF20).

## Civilization: the NPC AI still spawns vehicles on guests

* The guest's civ AI spawns vehicles for the other nations; the next snapshot kills them
  (`civ(guest)` `removed=` grows by a few per minute). `SpawnVehicle` callers 0xBE7643 / 0xBFBCF7
  use the result without a null check, so blocking needs a hook at the AI decision instead.

## Civilization: no building placement through the city planner in tests

* In `civ_npc` (a new game) the planner's house / factory / entertainment have no design yet; a
  click on a tile opens the building editor (co-op vote). The bots only open and close the planner;
  guest buildings are tested through `coop.civ build` (coop_civ.py). A fixture with designs (or a
  bot that makes one in the editor) is needed.

## Not covered by tests yet

* Tribe: duet, healer bonus, the chief's aura and rescue in real fights (tests hit through the
  damage function only). Civilization: the religious role and trade in real play (play_civ.py has
  military vehicles only; the military role and joint sieges are covered). Space: raids and
  missions (no live mission in the fixture).
