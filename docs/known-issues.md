# Known issues

Bugs found and not fixed yet. Each entry: what happens, how to reproduce it, what is known.
Fixed entries move to the commit message (and a test).

## Guest: fault in the tribe setup with a null simulator object (seen once)

* `exception 0xC0000005 at 0x00B12D32` (data 0x2B4) on a guest of a 4-player tribe churn
  (`coop_multi.py`, 2026-10-23): the tribe setup 0xCD78E0, one step after a load, calls 0xB12CE0 on
  `[0x167FAAC]` (getter 0xB3D350), which was null (it is null in the galaxy menu). Not reproduced on
  2 players (`coop_rejoin_loop.py` plain, `--race`, `--reshare`, `--menu`: 0 of about 250 rounds).
  Since 2026-10-23 a load leaves a stage only when it has been playable for 3 s (`world.cpp`
  `kStageSettle`), which should avoid a quit during the setup; if it comes back, the fault log now
  shows the call chain (`exc`: "calls:"). docs/re/space.md 8.3.

## Creature: one NPC 4 m off on every guest for 30+ s (seen once)

* `out/join_desync.py`-style probe, 4 players, 2026-10-23 (after the menu-load fix): NPC n11 stayed
  4 m from the host's on Bob, Carol and Dave alike from 8 s to the end (36 s); the 19 others < 1 m.
  The same offset on every guest points at the host side or at a spot the proxy cannot stand on
  (pushed out by collision after each stuck-teleport of `drive::step`). Not seen again in 5 min of
  watching nor in 4 more sessions. Hunt: `coop.npc list` on host and guests for that id (err, want).

## Creature chaos: a guest crashed with a corrupted heap (seen once)

* `coop_multi.py --fixture creature --players 4 --minutes 6`, seed 1035603402, lanes 1+8, 2026-10-23
  (branch fix/npc-lag): Bob (lane A guest, never rejoined) got a first-chance AV at 0x009ABB5C
  (`test byte [eax],1` after `call 0x9AC1B0`, eax 0xC8124380) 0.1 s after "Carol pings a place",
  then an AV in `GeneralAllocator_FreeInternal`+0xAD 3 s later, and froze in `MallocInternal` (watchdog:
  no frames). Looks like heap corruption from earlier; the bot's last actions on Bob were clicks,
  wheel and `coop.targets`. Not seen in 4 other runs of the same test. Replay with the seed above.

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

## coop_civ_play: the guest's "I'm ready" click was lost twice

* Two of seven cold runs of `coop_civ_play.py` on lane 5 (2026-10-23, fixture `civ_npc`) failed at
  "it fires when everybody is ready": the vote stayed at "ready 1/2" although the test clicked the
  guest's visible "I'm ready" button (the lobby had just opened for the vote); a click by hand a few
  seconds later worked. The two failures came while the shared headless display was being
  restarted, the five runs after it passed, so the cause is not settled.

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
  damage function only). Civilization: military / religious roles in real battles, a siege by real
  vehicles of both players (coop_civ_play.py besieges an NPC city of `civ_npc` through the capture
  function). Space: raids and missions (no live mission in the fixture).
