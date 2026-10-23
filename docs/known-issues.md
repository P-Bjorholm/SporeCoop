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
