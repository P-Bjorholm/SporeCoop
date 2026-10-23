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

## Creature (guest): goal cards of species whose creatures are the host's say "extinct"

* The guest's game removes its own NPCs (the host simulates them, the guest shows mirrors), so its
  goal card for such a species reads "Hunt: The Kook species is extinct!" while the host's card
  says "Impress 3 Kook to ally" (play_creature.py, 2026-10-23; screenshot: walk the guest to the
  Kook nest near the start of the `creature` fixture). Progress itself is shared through the host
  (DNA, kills), only the guest's cards are wrong. Fix idea: count the proxies as the species'
  creatures (they are made with `Create` in a herd of the species but not registered where the
  extinction check looks), or hide the guest's species cards.

## Creature (guest): social actions on NPC mirrors do nothing

* The guest walks up to a mirrored Kook in the social stance and presses 1 (sing) again and again:
  no social mini-game, no impress progress, no DNA (the mirror has no AI, nothing reaches the host;
  docs/03-gameplay.md 3.6; play_creature.py 2026-10-23). The host's own socializing works (impress: +10 DNA, shared). Needs a
  `MSG_SOCIAL` like `MSG_DAMAGE_REQ`: the host runs the guest's social action on the real NPC.

## Creature: nickname labels are drawn over the pause menu and the co-op window

* The partner's name tag (overlay) stays on top of the "Options" pause menu and of the F9 window
  (cosmetic; screenshots in play_creature runs). Hide the tags while a modal menu is up.

## Creature: the mating call did not lead to the editor in the bot

* Backspace (Mating Call) at the nest with DNA shows "Mate: Locate your Mate!" and a thought bubble,
  but clicking the family members around did not open the editor within 30 s. Not a confirmed bug:
  how a player answers the call is not explored yet (docs/re/creature.md 5). The evolution vote is
  covered by `coop_vote.py` through `coop.vote editor`.

## Creature: the host leaves to the main menu, the guests are told nothing

* Host: Esc, "Exit to Main Menu", "Don't Save" twice. The guests stay on the planet with no NPCs
  (the host simulates them; the guest's own keep being removed) and no message; the overlay list
  shows "Alice [Galaxy menu]". When the host loads the same save again, the guests get NPCs and the
  host's creature back without a new world (the guest's copy is the older state). If the host loads
  another planet of the same stage, nothing resends the world: `follow_phase` only follows a stage
  change (not reproduced: one creature fixture). Fix idea: a notice on the host's mode change to the
  menu, and resend the world when a guest of a planet stage is in another `world` for 5 s.

## Not covered by tests yet

* Tribe: duet, healer bonus, the chief's aura and rescue in real fights (tests hit through the
  damage function only). Civilization: military / religious roles in real battles. Space: raids and
  missions (no live mission in the fixture).
