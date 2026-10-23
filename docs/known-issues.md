# Known issues

Bugs found and not fixed yet. Each entry: what happens, how to reproduce it, what is known.
Fixed entries move to the commit message (and a test).

## Loading a save of another stage while in the Space stage crashes

* **What:** the game crashes in `cStar::GetSolarSystem` (null star) when a save of the Cell stage
  is loaded in game while the Space stage is running.
* **Reproduce:** `tools/tests/warm_check.py` (tribe → civ → space → cell warm switches); seen
  2026-10-22 in lane 2: `exception 0xC0000005 at 0x00C8B8F3 Simulator::cStar::GetSolarSystem+0x3`.
* **Players:** a guest in Space whose host then loads an earlier-stage save receives that world and
  loads it in game → the same crash.
* **Idea:** leave to the galaxy menu first when the received world is of another stage (world.cpp);
  tribe → civ → space switches in game work.

## Cell: the partner's cell is sometimes missing after the join

* `coop_cell.py`: "sync samples taken" fails — the host has no puppet of the guest cell
  (`samples 0`), the rest of the test works. Seen 2 of 2 runs on lane 3 while 3 lanes ran
  (2026-10-22), rarely on lane 1 alone: timing-dependent (CPU load).
* Hunt: `tools/lab.py run cell_coop --repeat 8 --retry 0`; look at the puppet creation in
  modes.cpp (cell adapter spawn) vs the first cell states.

## Tribe: the host froze once while the epic creature was being killed

* `coop_tribe_play.py` on branch feat/net-window, lane 2 (2026-10-22): the host stopped drawing
  during `coop.tribe epic hit` (monitor: FROZE); not reproduced in 14 more runs. The game logs of
  that run were lost (now kept by lab.py). Hunt: `lab.py run tribe_play,chaos_tribe --repeat 10`.

## Guest: "done" in its own editor does nothing

* Cancel works; not related to DNA. Found 2026-10-22 while making own creations per player.

## Space: after a lone death the respawn did not come within 60 s in a test

* `coop_space_play.py` (last step): the native death cinematic starts (state 2, +0x7F0 set) but the
  ship was not respawned 60 s later. The mod does not change that path; check in a real game
  whether the cinematic waits for input. The rescue timeout uses the same native path.

## Not covered by tests yet

* Tribe: duet, healer bonus, the chief's aura and rescue in real fights (tests hit through the
  damage function only). Civilization: military / religious roles in real battles. Space: raids and
  missions (no live mission in the fixture).
