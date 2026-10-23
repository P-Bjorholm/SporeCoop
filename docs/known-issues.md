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

## Very bad network: a guest rejoin did not complete within 90 s

* Nightly 2026-10-22, `SPORE_NET=awful` (250 ± 120 ms, 10 % loss, 2 % duplicates) chaos on tribe:
  the first join took 28 s (60 s in total), after a leave + join the guest was not back in the
  host's world 90 s later. `mobile` (120 ± 60 ms, 3 % loss) passes everything.
* Replay: `SPORE_LANE=1 SPORE_NET=awful python3 tools/tests/coop_chaos.py --fixture tribe --minutes 5
  --seed 264515564`; hunt: `tools/lab.py run chaos_tribe_awful --repeat 4 --retry 0`. Look at the
  world transfer under loss (reliable window 160 for long round trips, RTO, the join handshake).

## Guest: "done" in its own editor does nothing

* Cancel works; not related to DNA. Found 2026-10-22 while making own creations per player.

## Space: after a lone death the respawn did not come within 60 s in a test

* `coop_space_play.py` (last step): the native death cinematic starts (state 2, +0x7F0 set) but the
  ship was not respawned 60 s later. The mod does not change that path; check in a real game
  whether the cinematic waits for input. The rescue timeout uses the same native path.

## Test saves: no Civilization save with NPC cities yet

* The `civ` fixture (Agrauel) has only the player's city, so the joint-siege test hits the player
  city with a fake attacker. `new_stage_game.py civ` waits for the galaxy menu now, but the empty
  planet slot it clicks (`--slot 480,290`) is elsewhere on a fresh profile (the galaxy view differs):
  pass `--slot` from a screenshot, then `make_fixture.py` + an entry in tools/tests/fixtures.py.

## Not covered by tests yet

* Tribe: duet, healer bonus, the chief's aura and rescue in real fights (tests hit through the
  damage function only). Civilization: military / religious roles in real battles. Space: raids and
  missions (no live mission in the fixture).
