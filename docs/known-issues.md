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
* 2026-10-23: it came back once more (4-player tribe, `coop_multi.py`). Since then `gamefixes.cpp`
  skips `SimCamera_LookAt` and `SimCamera_Update` while the camera object is null (`coop.status`:
  "tribe camera skipped N"); the stage setup goes on without pointing that camera.

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

## Tribe: host fault and freeze when a member is added during the game-over caption

* When the player tribe dies out the game shows "Alas! Your population has fallen below the
  minimum needed to survive ... Bounce back in time" and waits for OK (the bounce back reloads the
  last save; with co-op the guest follows, checked 3x in play_tribe work, 2026-10-23). If a member
  is added to the player tribe while that caption is up, pressing OK faults at **0x00C0DAE3**
  (in 0xC0DAD0, a creature method reading `this+0xB54` and the species `this+0xB20`, both null for
  the new member; callers 0xC09CAE in cCreatureAnimal::Create, 0xC0AB99, 0xC0E74D, 0xC1CEAB,
  0xD410F8) and the host stops drawing for good (2 of 2 tries).
* Reproduce (lane N): host kills its own members through the damage path
  (`coop.tribe pincer <own id> <enemy id> 300`, a few times each, until only the knocked-out
  chieftain is left and the caption shows), then `coop.tribe spawn 2`, then `key enter` on the
  host. Without the spawn the bounce back works (frames go on, both machines have the same 3
  members afterwards).
* Only the test command adds members at that moment today; a guest adds mirrors (SpawnMember)
  whenever the host has members it lacks, so a guest that shows its own game-over caption while
  the host already bounced back could hit the same path (not seen: the guest showed no caption in
  4 tries, once it did while the host had none left alive).

## Tribe: knocked-out members stand on the guest

* The game's chieftain (and members of NPC tribes) lie knocked out with 0 health until they
  respawn; the host sends health 0, but `tribe.cpp guest_tick` applies only positive health to
  mirrors (a 0 might start the local death of a mirror), so on the guest they keep their last
  health (e.g. 0.4) and stand. Seen in play_tribe.py (enemy chief `#4 hp=0.0` on the host, `0.4`
  on the guest). The co-op chiefs use the host's health on every machine since 2026-10-23.

## Cell: a guest far from the host swims in an empty pool

* The host simulates and sends only the cells around its own cell (its active tiles, docs/re/cell.md
  5; `host_tick` sends cells within 120 m of the host's avatar), the guest spawns none of its own. A
  guest more than ~100 m away meets no NPCs and no food until it comes back (play_cell seed 9,
  2026-10-23: partner 211 m away at the end, the guest's 538 local cells were all around the host;
  the pair was over 60 m apart half the session). Not unfair in itself (no NPC there to hunt either
  player), but the guest eats less there. A fix needs the host to populate and keep tiles around
  each partner (PopulateTiles 0xE7D370 around `G+0x4128` is the host's view only).
* Reproduce: `SPORE_LANE=N python3 tools/tests/play_cell.py --minutes 10 --seed 9` and watch
  `partner` distances and the guest's `local_gameplay_cells` vs where its cell is.

## coop_sync.py cell: the players' facing check rests on 2 samples

* Lane 2, 2026-10-23 (branch fix/cell-npc-prey): `coop_sync.py cell` failed once on "cell/players:
  90% facing within 15 deg" with `n=2`, p90 19.9, max 23.1 deg (positions fine, NPC cells fine), and
  passed on the next run (p90 11.2, max 20.6). With two samples one sample taken mid-turn decides
  the result. Probably the test, not the sync: take more player samples in the cell phase.

## coop_civ_play: the guest's "I'm ready" click was lost twice

* Two of seven cold runs of `coop_civ_play.py` on lane 5 (2026-10-23, fixture `civ_npc`) failed at
  "it fires when everybody is ready": the vote stayed at "ready 1/2" although the test clicked the
  guest's visible "I'm ready" button (the lobby had just opened for the vote); a click by hand a few
  seconds later worked. The two failures came while the shared headless display was being
  restarted, the five runs after it passed, so the cause is not settled.

## Creature: the mating call did not lead to the editor in the bot

* Backspace (Mating Call) at the nest with DNA shows "Mate: Locate your Mate!" and a thought bubble,
  but clicking the family members around did not open the editor within 30 s. Not a confirmed bug:
  how a player answers the call is not explored yet (docs/re/creature.md 5). The evolution vote is
  covered by `coop_vote.py` through `coop.vote editor`.

## Creature (guest): the social game is simplified

* Since 2026-10-23 a guest befriends a mirrored NPC (click in the social stance, a social ability
  key; the host applies the native win, docs/re/creature.md 8.3), but the guest does not see the
  native social meter and the NPC's ability prompts (they belong to the NPC's behaviour tree, which
  runs only on the host): any social ability counts as the answer, one press per round. A click on
  the ability bar instead of a key is not counted (the mod polls the keys 1-4). The host's own
  game is native. The success message 0x60B4123 (CreatureStageEventHandler) is not posted for a
  guest's win. Reproduce: `tools/tests/coop_creature_social.py`.

## Creature: while the host is in the main menu the guest's planet fills up slowly

* The host leaves to the main menu: the guests are told and play on alone (their own game runs
  their NPCs again, `coop.npc` host_away=1), but the game respawns the removed NPCs only a few at a
  time (`local_alive` 0-1 after 30 s in `coop_exit_menu.py`). Making the mirrors local NPCs
  instead failed (a mirror with AI leaves its herd and is destroyed, docs/re/creature.md 8.3).

## Space: no test save after the tutorial missions

* The `space` fixture starts at the first mission ("Fly to planet Vulpica", the wreck): the ship has
  no interstellar drive (tests add it with `coop.space tool add interstellardrive`), no
  terraforming tools, no colony besides home and no other empire has been met. `space_empire`
  (docs/06-testing.md 6.15) adds the drive, money, two colonies with spice and two contacts, made
  with test commands; it is still inside the tutorial (travel to stars that are not own stars is
  refused there), and has no raids, missions or trade. A save played through the tutorial would
  still be better.

## Space: `coop.space jumpto` into another system arrives without planets

* `coop.space jumpto 0x00460038` (and `0x0046102D`) from a planet and from the galaxy view put the
  ship into that star's system, but the system had no planets: only the sun was drawn, and
  `coop.targets all` still listed the planets of the system left behind (the planet list of
  `cStarManager` +0x50 was not reloaded). Zooming out to the galaxy and back in did not load them
  either. The star records had their planets (star +0x84, 3 and 8 records). Seen 2026-10-23,
  lane 8, single player, inside the tutorial. The co-op "jump to partner" uses the same
  `travel_to` (TravelToStarSystem 0xFDD380, docs/re/space.md 7.3): check whether a jump to a
  partner in another (not yet visited) system shows its planets. Not saved into any fixture.

## Civilization: single vehicle sync outliers of 20-27 m

* `play_civ.py` sessions: p90 of the vehicle position error 0.9-1.5 m, but the maximum of ~2500
  samples was 22-27 m in three sessions (one sample each). Not traced yet: candidates are a guest
  mirror bound to a nearby local vehicle of the same kind right after a purchase (the 25 m bind
  radius in guest_state) or the velocity estimate around a capture caption pause. Reproduce:
  `SPORE_LANE=N python3 tools/tests/play_civ.py --minutes 10 --seed 9` (it prints the outliers):
  `v13 22.7 m host (-497, 27, 66) guest (-493, 45, 79)` three samples in a row, an NPC vehicle on
  the far side of the planet; not seen on player vehicles.

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

## coop_sync.py space: the ships' p90 sits near its 10 m limit

* 2026-10-23, 4 runs: ships p90 7.4-10.6 m (one run over the limit), single samples up to 690 m.
  Ships fly fast and the test reads host and guest one after the other, so most of it is the time
  between the two reads plus the interpolation delay; the 690 m sample is the first sample after the start:
  the host's new ship still stands at its default spot (0, 0, 699) for that moment (play_space.py
  saw one 412 m sample in 2 h). The p90 of real play is
  1.2-8.3 m (docs/stability/space.md). Since then the test compares the guest with the host's path between
  its two reads (the guest is read in between); if it still fails, look at the samples, not at the limit.

## Space contracts: what is not done (space_contracts.cpp, 2026-10-23)

* The armada attacks a colony of the host only: its waves are the game's raids, and a raid needs a
  planet of the raided empire in the host's save; the guests' colonies (feature 1's roster,
  `space_map.h`) are not in it. The host's NPC ships are the shared ones (squadron.cpp), so with three
  or four players the host has to be one of the two ships on the colony.
* The colony's turrets fight the raid too (on the homeworld they shot down 20 plain raid ships in
  ~25 s); the armada's ships have double HP, but a well-defended colony may still win a wave nearly
  alone. A raid the pirates win fails the contract; a raid still running at a timeout goes on as an
  ordinary raid.
* "Jump there" to the armada's star lands a guest next to a planet the travel picks (an own colony
  first): the host's colony is not the guest's, so the guest may have to fly to it in the system
  view before "Land" takes it to the right planet.
* SpawnUFO pirates do not fight (docs/re/space.md 17.2): an armada anywhere else than on an own colony
  (a neutral star, a partner's colony) would need the UFO AI's target selection, not found.
* While the game's badge panel ("Reward ... 2 badge points" + OK, after a defended raid) is up, the
  co-op window's buttons (all F9 pages) get no clicks; Enter or its OK closes the panel. The panel is
  modal (only it gets the mouse): bringing the co-op window to the front every refresh did not help
  (tried 2026-10-23, lane 3) and would have covered the panel's OK button, so it was reverted.
* A partner who leaves the session fails a running contract when the crew gets smaller than two
  (no grace time for a rejoin); a rejoining player sees the current contract within 5 s and can
  accept a running one again. Not covered by the test: a partner leaving, a rejoin.
* Joint terraforming contracts (a planet to a T-score together) are not done: the fixture has no
  terraforming tools to test them with.
* The reward tool is chosen from the host's cargo (the first one of the list it lacks); a guest who
  has it already gets only the money.

## Space rivals mode: limits (space_rivalry.cpp, 2026-10-23)

* PvP damage is the raw damage of the shot: the shooter's damage-boost tool does not count (the base
  TakeDamage of the shooter's game is not called for the puppet, docs/re/space.md 10.2).
* Only hits of the own ship count: NPC ships and turrets of a player's game hit an enemy puppet only
  on the host (squadron relays NPC hits on puppets to their owner), not on a guest's game.
* A player who joins for the first time while rivals mode is on keeps the money of the host's save
  it loaded (it had no budget yet); a rejoining player gets its own budget back from the host.
* A guest that is not in space when the host switches the mode gets its share or merge when it is
  back in space; the merge counts its last reported budget.
* Spice gifts, tool gifts and the jump work between rivals as before (deliberate acts); mission
  rewards are shared between allies only; raid alerts reach everybody.

## Space tests on the new `space` fixture (Madas): two flaky runs seen (lane 6, 2026-10-23)

* `coop_space_rescue.py` round 1 failed once: the host had flown ~550 m off with 2.5 s of W and came
  back too late (repair 1.0 of 2.0 s when the 20 s ran out); round 2 then started 1330 m apart. Passed
  in the next run and on the base branch. The ship of the new fixture is faster: a shorter flight off
  would make it stable.
* `coop_space_death.py` failed once in the forced-jump step: after the OK the ship stayed dead for 47 s
  and the guard "respawn caption without a planet" did not fire (0), so the respawn caption had ended
  some other way; passed in the next run and on the base branch.

## Space diplomacy: what is not shared or may drift (space_diplomacy.cpp)

* **Names and species of NPC empires differ between the copies**: each copy picks them at the first
  contact (from that player's creations). The relation, contact and war/alliance state are shared
  by political id; notices name both ("Kirgle Empire (Alice: Judge Empire)"). Sharing names would
  need the species too (docs/re/space.md 10.3).
* Events the game raises on every copy by itself are not sent (personality / archetype at contact,
  the empire AI's be nice / be nasty, embassies, super power, avoided contact). Any other event
  between the player and an NPC empire is sent, also when the local AI caused it (for example an
  NPC destroying the player's buildings): if both copies' AI do the same, the host counts it twice.
  Guests converge on the host's table within 20 s; the host's own copy is the reference.
* An empire created after the join (uplift of a civilization, ...) gets a political id on each copy
  by itself (`NextPoliticalID`), the ids may differ: its events are dropped as "unknown empire"
  (`coop.space diplomacy` counter `unknown`). Not seen in tests.
* The comm screen is not driven by clicks in the test: `coop_space_diplomacy.py` calls the same
  game functions its buttons do (ApplyRelationship with the gift event, DeclareAlliance,
  DeclareWar, DeclarePeace); the money of a gift goes through the shared budget. Attacks on an
  empire's ships, missions and trade use the same hook but are not played in the test.
* Rivals mode (feature 10) turns sharing off; it is not tested here (the switch belongs to
  space_rivalry.cpp).

## Space map: a guest's colonies last only until it rejoins

* Every player flies in a copy of the host's save: a colony the guest founds exists only in the
  guest's running game. When the guest leaves and joins again it loads the host's world and the
  colony is gone (`coop_space_map.py` checks exactly this). The host's own colonies survive (they
  are in the save the guests get). Shared colony ownership is not done (docs/re/space.md 5.3).
* The names of stars nobody named differ between the copies (docs/re/space.md 10.1): the roster
  sends the owner's names, the map shows mine for stars without a roster name.
* The test colony is founded with `coop.space map colonize` (the tool's effect without the aiming
  check): the real aiming refused every planet of the `space` fixture but the home planet ("Can't use
  in this system", "Colonies must be placed near spice"). A fixture with a planet with spice
  geysers would let the test aim like a player.
* The map is overlay text (a ring of dots, a name tag), not the game's own star marks; tags are not
  hidden behind the game's own galaxy panels (the star info card), only under the pause menu, the
  co-op window, comm screens and cinematics.

## Space trade: open ends (space_trade.cpp, 2026-10-23)

* **Rivals mode is tested only in a local merge**: space_rivalry.cpp (feature 10) is not in this
  branch; coop_space_trade.py passed its rivals part (offer, accept by a real click, both own
  budgets, an open offer declined when the host ends rivals mode) on a throwaway merge with
  feat/space-rivalry 2515362 (lane 10, GOG 2024, 2026-10-23). Without space_rivalry the test
  prints "rivals mode: UNTESTED".
* **The guest's UI clicks do not arrive on the new space fixture (GOG 2024, lane 10)**: after the
  join the guest's game sits in a HUD-less movie state (`coop.space hp` shows `movie=1`, Enter
  does not end it), the co-op window opens with F9 but a click on "Space >" changes nothing. The
  same state makes coop_space_page / coop_space_death / coop_space_rescue fail on lane 10, also on
  the plain feat/space-map build (not caused by space_trade). coop_space_trade.py therefore lets
  the host do every real click and presses the guest's offer row through `coop.space press`.
* **A gift or sale on its way when the own session ends comes back** (the sender cannot know
  whether the partner already got it): leaving right after "Give" can duplicate the charges.
  A partner who leaves before answering is handled (the sender gets it back).
* **Demand cap is per seller game**: the 30 units / 10 minutes of a partner colony are counted by
  the seller and forgotten on a restart; two sellers each get their own 30.
* **The F9 Space page has 9 rows for every feature together** (coop_ui.cpp kSpaceRows): the trade
  rows (gift, next tool, other partner, sale, offers) come after the features registered before
  it and can fall off the page when many features show rows at once.
* Spice values are the game's own (`spacetrading~` 0x34F1A4A x the difficulty multiplier, about
  5 600 to 24 000 per unit on normal), so one sale of 10 is worth 110 000 to 490 000 at 2x: big
  next to the fixture's $100 000. NPC stores pay similar money for the spices they want.

## Not covered by tests yet

* Tribe: duet and healer bonus in real play (play_tribe.py plays real raids since 2026-10-23:
  pincer, the chief's aura and rescues happen there; the fixture's tribe has no instruments and
  no healer tool, so a social click on another tribe's hut is a food gift, CitizenAction 9, and
  nobody can heal: a fixture of a later tribe save is needed). Civilization: the religious role
  and trade in real play (play_civ.py plays military vehicles: the military role and joint sieges
  by real vehicles are covered). Space: raids and missions (no live mission in
  the fixture), spice trading with NPC stores (selling to a partner colony is covered by coop_space_trade.py), terraforming with the tools, buying in the city planner (the bot
  opens and closes it only).
