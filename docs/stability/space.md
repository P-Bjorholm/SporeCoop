# Space stage: stability by play (2026-10-23)

Two bots played the Space stage together on test lane 7 (headless, 1440x1080, host = instance
13, guest = 14, fixture `space`, Maraya in its home system), the way people play it, while a third
connection measured. Test: `tools/tests/play_space.py` (nightly suite, 12 minutes). Branch
`play/space`.

## What the bots do

Both players act at the same time, each in its own thread, mostly through real input (keys, clicks,
mouse wheel, the tool palette, the co-op window), choosing from what is on their own screen
(`coop.targets all`):

* **Fly**: W/A/S/D on a planet; clicks on planets in a solar system (the ship flies there, then the
  mouse wheel lands on it); zoom out to take off, to the system and to the galaxy view; clicks on
  neighbouring stars in the galaxy view (the ship jumps there), zoom in to enter the system.
* **Fight**: the combat palette, key 1 (the laser), clicks on the nearest NPC ship, flying closer with
  the keys; pirates are spawned by the host as setup when none came for 40 s (raiders and ambush
  pirates: raiders only attack around colonies).
* **Get disabled, repair the partner**: setup "battle damage" leaves the ship 8 HP, the pirates' real
  shots do the rest. Next to the partner the ship is disabled; the partner sees `partners_down` and
  flies to it with the keys until it is repaired. Alone it is destroyed: the death caption and the
  respawn caption are confirmed with Enter, the ship comes back at home.
* **Stay together**: the guest follows the host most of the time: to the host's planet in the same
  system (take off, click the planet, land) and, when the host is in another star system for 25 s,
  with the co-op window (F9, "Jump to Alice"). On the same planet it flies to the host with the keys
  (`far player` gives the direction when the host is off the screen).
* **Cities**: on an own colony the bot flies over a city, opens the city planner from the city's
  rollover, looks around and closes it.
* **Money**: the game's own cheat `moreMoney` on either machine (+1 000 000 to the shared money).
* **Captions**: every caption that waits for OK (deaths, respawns, tutorial texts, badges) is
  confirmed with Enter after a short "reading" pause; the pause menu, if it ever opens, is closed.

Setup by console only where the game gives no input for it: the interstellar drive (the fixture's
ship lacks it, the galaxy view is locked without it), pirates, battle damage.

## Measurements

Sessions with the fixed mod (fixes 1-4 below); the bot grew between them (seeds 10-13 without ambush
pirates, 14 on: the final bot). Earlier sessions (seeds 1-9) found bug 1 and shaped the bot.

| seed | min | together | ship sync p50 / p90 / max | money: checks (lasting diffs), +1 000 000 events | shots host / guest | guest hits applied by the host | F9 jumps | deaths / respawns | disabled / repaired by the partner | faults, freezes |
|---|---|---|---|---|---|---|---|---|---|---|
| 10 | 10 | 217 s | 0.4 / 3.6 / 5.9 m | 187 (0), 0 | 119 / 23 | 8 | 3 | 0 / 0 | 1 / 1 | 0 |
| 11 | 12 | 548 s | 0.1 / 2.0 / 7.3 m | 222 (0), 3 | 47 / 7 | 0 | 1 | 0 / 0 | 0 | 0 |
| 12 | 12 | 275 s | 0.1 / 1.2 / 2.9 m | 224 (0), 2 | 68 / 23 | 0 | 4 | 0 / 0 | 0 | 0 |
| 13 | 12 | 430 s | 0.2 / 2.3 / 4.0 m | 223 (0), 2 | 81 / 25 | 0 | 2 | 0 / 0 | 0 | 0 |
| 14 | 12 | 228 s | 0.9 / 8.3 / 23.3 m | 224 (0), 2 | 196 / 89 | 83 | 4 | 2 / 2 | 1 / 1 | 0 |
| 15 | 12 | 149 s | 0.4 / 2.6 / 7.2 m | 224 (0), 3 | 105 / 84 | 94 | 4 (of 25 clicks, see open issues) | 3 / 3 | 2 / 1 | 0 |
| 15 again | 12 | 602 s | 0.0 / 2.4 / 9.7 m | 222 (0), 0 | 708 / 606 | 23 | 0 | 3 / 3 | 2 / 1 | 0 |
| 16 | 12 | 267 s | 0.3 / 2.1 / 4.9 m | 224 (0), 3 | 25 / 6 | 0 | 1 | 0 / 0 | 0 | 0 |
| 17 | 12 | 432 s | 0.4 / 2.8 / 412 m (one sample) | 222 (0), 0 | 270 / 209 | 141 | 1 | 6 / 6 | 6 / 2 | 0 |
| 18 | 12 | 406 s | 0.2 / 2.2 / 4.7 m | 224 (0), 2 | 80 / 24 | 0 | 0 | 0 / 0 | 0 | 0 |

In all: ~2 hours of play, 0 faults, 0 freezes or crashes, 0 money differences, every death
respawned (1-6 s after OK on the death caption), every disabled ship either repaired by the partner
or lost after 20 s and respawned.

* "together": both in the same place (planet or system); NPC ship sync is measured only then
  (`coop.sync`, guest sample against the closer of two host samples, like `coop_sync.py`).
  Ships fly fast (up to ~100 m/s): p90 1-8 m is the expected interpolation lag.
* Guest hits: the guest's laser shots on the mirrors of the host's ships, sent to and applied by the
  host (`hits_sent` = `hits_in`); 0 in sessions where the pirates died before the guest was close
  (the host's ship and, before fix 4, the guest's puppet killed them).
* Money: host and guest `coop.res` every 3 s; a difference that lasts over two checks counts. None in
  any session, also right after +1 000 000 on either side.
* Respawn after OK on the death caption: 1-6 s, then the respawn caption plays (also confirmed).
* The 412 m sync maximum of seed 17 is a single sample (a ship that had just been spawned or had
  jumped); p90 stayed at 2.8 m.
* Disabled ships were often lost: the partner has 20 s to come within 60 m and the bots were often
  several hundred metres apart on a planet (their rescue flights take 10-30 s). That is the game
  design, not a fault.
* The game frees partner puppets and mirrors 5-20 times per session (`freed_by_game`), see bug 1.

## Problems found and fixed

1. **Host crash: a freed partner puppet driven every frame** (commit 078b783). Leaving a planet frees
   all NPC UFOs there, the Defender UFOs that show the partners too; the mod kept the pointer and
   moved the freed puppet (`modes::space_drive`, AV through an overwritten vtable). The host hung in
   the exception; the guest, in the same wineserver, hung in a cross-process user32 call (both
   "frozen"). Fix: the space adapter's alive check (the pointer must be in the UFO noun list and still
   a Defender), despawn only destroys listed UFOs; same check for the guest's mirrors and for hits.
   Regression: `coop_space_puppet.py` (both land on a planet, one takes off; `freed_by_game` must
   rise, nothing may fault or freeze).
2. **Rescue never happened in real fights for the guest** (commit f69d9e1). NPC hits reach the guest
   as MSG_UFO_DAMAGE; the guest applied them with the game's TakeDamage directly, past the rescue
   filter, so a guest next to its partner died instead of being disabled. Fix: through the hook.
3. **A ship with HP <= 1 died at the next hit next to its partner** (same commit): small hits can leave
   0.5 HP; now any lethal hit next to a partner disables the ship. Regression for 2 and 3:
   `coop_space_rescue.py` (fails on the old code: the guest is destroyed; with the fix it is
   disabled and the host repairs it by flying there with the keys).
4. **Partner ships fired on their own** (same commit): the puppets are Defender UFOs whose AI shot at
   every hostile ship in range (thousands of FireTool calls a minute on both machines while raiders
   were near), so pirates died on the host although the guest never fired. FireTool is dropped for
   puppets (`puppet_shots_blocked`); `coop_space_puppet.py` checks that a pirate nobody shoots keeps
   its health.
5. **"can't jump: ..." went to every player**: the co-op window's jump error was sent as a co-op
   notice, so the partner read why *we* could not jump. It stays local now (toast and chat log).
6. Test infrastructure: `twoinst.warm_reset` returned while the guest was still in its old copy of the
   world (it rejoins "together" at once and reloads the host's world seconds later): tests then lost
   the guest for ~10 s. It now waits for that reload. `coop.res` printed the leftover civilization
   money as a second "money" line in Space and `coop.res add money` changed that one.

## Open issues and what is not covered

* **Spice: pick-up and selling, terraforming tools**: not played. The fixture's colonies have no spice
  to collect and the ship has no terraforming tools (tool file names unknown, only FNV hashes in the
  inventory); selling needs the trade screens of other empires. The co-op parts (spice gift,
  terraform sharing) are covered by `coop_space_play.py` through console commands.
* **City planner purchases**: the bot opens and closes the planner but does not buy: the planner's
  ground camera is often blocked by trees and the free plots are not known to the bot.
* **Tutorial missions** (fly to Dunoselka, scan the wreck): the fixture is at their start; the bot
  skips them (the interstellar drive is given as setup). A fixture after the tutorial would let bots
  meet other empires, missions and raids (`docs/known-issues.md`).
* The guest's own ship is rarely shot by pirates (they choose the host), so the guest was never
  destroyed in play; its rescue path is covered by `coop_space_rescue.py`.
* **Jump clicks without a jump** (seed 15, first run): the guest clicked "Jump to Alice" 25 times and
  jumped 4 times. The reason was not recorded then (the "can't jump" message was broadcast as a
  notice, not kept); since then the bot records the refusal reason from the co-op chat log, and the
  next 4 sessions had no refusal. If it comes back: `jump_refused:<reason>` in the bot's counters.
* `planet_misclicks`: in the system view a moon next to a planet often takes the click; the bot
  retries a few pixels away (the game's behaviour, not a mod bug).
