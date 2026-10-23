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

## Test saves: no Civilization save with NPC cities yet

* The `civ` fixture (Agrauel) has only the player's city, so the joint-siege test hits the player
  city with a fake attacker. `new_stage_game.py civ` waits for the galaxy menu now, but the empty
  planet slot it clicks (`--slot 480,290`) is elsewhere on a fresh profile (the galaxy view differs):
  pass `--slot` from a screenshot, then `make_fixture.py` + an entry in tools/tests/fixtures.py.

## Space: no test save after the tutorial missions

* The `space` fixture starts at the first mission ("Fly to planet Dunoselka"): the ship has no
  interstellar drive (the galaxy view stays locked; tests add it with `coop.space tool add
  interstellardrive`), no terraforming tools, the colonies have no spice stored and no other empire
  has been met. So the game bot (`play_space.py`, docs/stability/space.md) can not collect or sell
  spice, terraform, take missions or meet raids. A fixture made after the tutorial (scan the wreck,
  meet a neighbour, a colony with spice) would cover them: play it once, then `make_fixture.py`.

## Not covered by tests yet

* Tribe: duet, healer bonus, the chief's aura and rescue in real fights (tests hit through the
  damage function only). Civilization: military / religious roles in real battles. Space: raids and
  missions (no live mission in the fixture), spice trading, terraforming with the tools, buying in
  the city planner (the bot opens and closes it only).
