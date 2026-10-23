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

## Cell: the host's NPCs hunt the host, rarely the guest

* In `play_cell.py` sessions the host's NPCs chased the host 620-2680 bot samples and the guest's
  cell 36-117; damage taken host 25-86 hp, guest 16-34 hp; knockouts host 3-16, guest 1 per session
  (docs/stability/cell.md). The NPC AI picks the avatar (`G+0x411C`) as its prey; the guest's
  puppet on the host is not damageable there (no AI, not the avatar: IsDamageable 0xE52950), so a
  host NPC's bite on it never reaches the guest (`hurt_fwd` = 0 in every run). The guest is only
  bitten when its own cell bumps into a mirror on its machine (`hurt_local`). Playing the guest is
  safer; a hunted host respawned small can be hunted down again and again (play10: 12 respawns).
* Reproduce: `SPORE_LANE=N python3 tools/tests/play_cell.py --minutes 10 --seed 8` and compare
  "damage taken" and "host NPCs chasing" at the end.
* A fix needs the NPC target choice to consider the partner's cell (the chase target `+0x22C` is
  written in 0xE57692/0xE576CC/0xE70EC6/0xE71EC0 ...) and the host to forward such a bite to its
  owner (`MSG_CELL_HIT`, already handled by the guest).

## Test saves: no Civilization save with NPC cities yet

* The `civ` fixture (Agrauel) has only the player's city, so the joint-siege test hits the player
  city with a fake attacker. `new_stage_game.py civ` waits for the galaxy menu now, but the empty
  planet slot it clicks (`--slot 480,290`) is elsewhere on a fresh profile (the galaxy view differs):
  pass `--slot` from a screenshot, then `make_fixture.py` + an entry in tools/tests/fixtures.py.

## Not covered by tests yet

* Tribe: duet, healer bonus, the chief's aura and rescue in real fights (tests hit through the
  damage function only). Civilization: military / religious roles in real battles. Space: raids and
  missions (no live mission in the fixture).
