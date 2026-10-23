# Stability: the Creature stage, played

Two players (host Alice, guest Bob) played the Creature stage together the way people play, through
real input, in sessions of 10 minutes (`tools/tests/play_creature.py`, fixture `creature`:
Scalinus, the host's red species at its nest, the guest's own Kirgle; lane 3, 1440x1080, headless
display). This page: what the bot does, what was measured, what broke and was fixed, what is open.
Reverse-engineering notes from this work: [docs/re/creature.md](../re/creature.md).

## What the bot does

Each player runs in its own thread with seeded random choices (`--seed` repeats them):

| Action | Input | Notes |
|---|---|---|
| hunt | combat stance button, click on the NPC (walks to it), keys 1..4 | prey: any non-family NPC on the screen within 60 m (`coop.targets all`), until it dies, 35 s at most |
| hunt together | the partner joins the current prey (80 %) and turns the camera to find it | the pack bonus counts in `coop.team` (joint hits) |
| eat | click on the carcass after a kill (60 %) | |
| socialize | social stance button, click on the NPC, keys 1..4 | |
| walk | click on the ground, or hold W/A/S/D 1-2 s | |
| look around | right-drag | also used to find the partner or the prey |
| follow | the partner further than 30 m: turn to find it, click next to it | |
| pause menu | Esc, look, "Resume Game" (found with `coop.ui find`, tools/tests/uifind.py) | |
| co-op window | F9 (held), sometimes a chat line typed into it and sent, F9 again | the line must reach both players |
| zoom | mouse wheel | |
| death | "You were killed in battle!" caption: Enter (OK) | |
| exit to the main menu (guest, once) | Esc, "Exit to Main Menu", "Don't Save", "Don't Save" | the host's world must come back |

Console commands only measure (and set up the fixture): `coop.npc list` on the host (health, death,
species), `coop.sync` on both (pose of every player and NPC), `coop.dna` on both, `coop.team`,
`coop.avatar`, `coop.status`, `exc`, and the twoinst monitors (freeze > 10 s, crash).

Checks: NPC pose p90 within 1.5 m and 25 deg; players: standing p90 within 1.5 m, moving ones trail
by 0.75 s or less (median), p90 within 2.5 m, never more than 6 m; the shared DNA equal on both in
90 % of the samples and growing; both players hit NPCs; kills; joint hits; chat lines reach both;
the guest is back after the exit to the main menu; no fault, no freeze, no crash.

## Measurements

Five 10-minute sessions with every fix below (2026-10-23, seeds 21, 22, 23, 31, 32; replay:
`SPORE_LANE=3 python3 tools/tests/play_creature.py --minutes 10 --seed N`):

| Seed | Attacks host / guest | Kills (both in) | Guest kills paid | DNA (same on both) | Joint hits | Rescues | Regen rises | Deaths host / guest | NPC p90 / max (m) | Player p90 / max (m) | Standing p90 (m) | Moving delay median / p90 (s) | Exit and back | Faults |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 21 | 187 / 166 | 9 (6) | 5 | 0 -> 141 (143/143) | 1 | 0 | 21 | 0 / 0 | 0.62 / 8.1 | 1.40 / 4.5 | 0.48 | 0.46 / 1.29 | menu path failed (bot) | 0 |
| 22 | 171 / 186 | 6 (1) | 4 | 0 -> 64 (142/142) | 0 | 0 | 23 | 1 / 1 | 0.52 / 5.6 | 1.20 / 3.1 | 0.64 | 0.44 / 1.26 | 14 s | 0 |
| 23 | 235 / 211 | 8 (4) | 4 | 0 -> 98 (142/142) | 2 | 1 | 27 | 2 / 0 | 0.73 / 16.6 | 1.62 / 4.3 | 0.53 | 0.48 / 1.39 | 14 s | 0 |
| 31 | 188 / 246 | 8 (8) | 3 | 0 -> 112 (142/142) | 2 | 0 | 16 | 1 / 2 | 0.70 / 12.9 | 1.19 / 4.4 | 0.53 | 0.49 / 1.06 | 14 s | 0 |
| 32 | 123 / 123 | 9 (7) | 4 | 0 -> 117 (141/142) | 3 | 0 | 23 | 0 / 0 | 0.70 / 4.3 | 1.09 / 4.3 | 0.52 | 0.43 / 1.19 | 14 s | 0 |

* No freeze, no crash, no caught fault in any of them (before the fixes: one guest fault at
  0xD48634 in a 2-minute session).
* The shared DNA was the same on both machines in every sample but one (a sample taken while a
  total was on its way). Before fix 1 the guest's kills paid nothing.
* NPCs: p90 0.4-0.7 m between host and guest (coop_sync.py: 0.2-0.7 m); the rare 5-17 m samples
  are proxies in a fight (the proxy behind its own target, snapped at 8 m) and, before fix 2,
  corpses. Players: p90 1.1-1.6 m in play; standing p90 ~0.5 m; moving ones trail by ~0.45 s
  (plain walking: ~1 m at 2.7 m/s). Before fix 6: p90 2.5 m, max 7.8 m.
* Chat lines typed into the F9 window: 18 of 21 reached both players; the other 3 (seeds 21-23)
  never got into the chat line (empty after typing, twice). Idle, the same steps work every time;
  seeds 31 and 32 check whether the creature was dying at that moment (the death cinematic takes
  no UI input) and lost none (8 of 8). Watch `chat_not_typed` in the nightly runs.
* Progress stalls late in some sessions (seed 31: no kill after 4 min): the prey near the nest is
  eaten up, the bot hunts within 60 m on the screen and the bigger species (15 hp) flee or kill it.
  Hits keep landing (`hits_applied` grows), it is the bot, not the sync.
* Joint hits stay low (0-3 a session; seeds 8 and 11 earlier: 11 and 6): the bots drift apart
  (`follow_lost`), the joining player often arrives after the kill. Rescues happened 1-2 times in
  two of the eight 10-minute sessions (all of them "close" rescues, the message on both).
* Earlier 10-minute sessions (seeds 6, 8, 11) found fixes 6 and 7.

## Problems found in play, and fixed

Each with a regression check (commits on branch `play/creature`):

1. **A guest's kill paid nothing** (6ee2292). The game pays a kill's DNA, goal progress and the
   kill popup inside `TakeDamage` only when the attacker is the player's own avatar; a guest's hit
   is applied on the host with the guest's puppet as the attacker. The lethal guest hit now lands as
   the host avatar's. In play: about half the kills of a session are the guest's. Check:
   `coop_team.py`.
2. **Flying corpses, a guest's corpse 15-80 m away from the host's** (2af8267). The vector argument
   of `TakeDamage` is the hit's push; the mod passed the victim's position (length ~505): every
   forwarded guest hit pushed the NPC, a kill threw it ~80 m up, and the guest's local death replay
   (1e6 damage) left its corpse hanging far away (teleports do not move dead creatures). Synthetic
   hits push with (0,0,0) like a bite, the death replay is just lethal. Check: `coop_team.py`.
3. **A guest that exits to the main menu waited forever** ("Waiting for the host's world...")
   (d1541f8). The host resent its world only to guests in another stage. A guest that played the
   host's phase and sits in the main menu gets the world again after 5 s (back in ~14 s). Check:
   `coop_exit_menu.py` (full suite).
4. **4 clones of the guest's creature at the host's nest** (c5447cb). `CreateHerd` gives the herd
   of a puppet (and of the guest's NPC proxies) a target size of 5 and the game fills it: the clones
   were real NPCs, mirrored to the guest. Such herds get target 0 and are disabled. Check:
   `coop_team.py`.
5. **Guest fault at 0xD48634** (7e422f3): the creature stage's avatar-event handler reads
   `(+0xE84)->+0x32C` (archetype tuning) of creatures near a fight; our puppets and proxies have
   none. During the handler they borrow a blank one. Seen once in a 2-minute session, not since.
6. **A partner trailed a fight by 5-8 m** (26677ed). Lunges and charges move a creature faster
   than its puppet can walk, and attack animations undo some of the nudges. A partner's puppet
   snaps at 4 m (8 m before): max error 4.4 m instead of 7.8 m, p90 1.8 m instead of 2.5 m.
   `coop.status` shows the driver state per partner.
7. **The guests lost every NPC while the host's game stood still** (8ea0597): pause menu, the
   death caption waiting for OK, cinematics. No creature is updated then and the host forgot them
   after 2 s. A frame without creature updates counts as a pause. Check: `coop_exit_menu.py`
   (host paused 6 s: 21 -> 0 NPCs before, 21 -> 21 after).

Fixed after the sessions (branch `fix/creature-social`, 2026-10-23; docs/re/creature.md 8):

8. **A guest could not befriend anybody.** The social game is the NPC's behaviour tree, a mirror
   has none: the guest's keys did nothing. The guest's click on a mirror in the social stance
   engages it (the game's social stimulus, seen in a hook), a social ability key plays the guest's
   ability and the host applies the native win (the species' impress count, ally at its threshold,
   the creature's DNA for the pack, the impressed NPC's reaction on both). Check:
   `coop_creature_social.py`.
9. **"The Kook species is extinct!" on the guest's card.** The guest's game counted every replayed
   death of a mirror (again when a dead mirror came back into range) as a kill of the species: three
   made it extinct there. Relationship events on mirrors are dropped on guests and the host's
   species table (MSG_SPECIES) is the guests' table; the cards are refreshed when it changes, a
   card's ally / extinction DNA is paid once by the host, whichever machine's card saw it. Mirrors now live in their species' nest
   herd (with the archetype tuning). Check: `coop_creature_social.py`.
10. **Name tags over the pause menu and the co-op window**: hidden while either is up. Check:
    `coop_creature_social.py`.
11. **The host left to the main menu, the guests were told nothing** and stood among frozen
    mirrors. They get a notification, the mirrors go, their own game runs their NPCs, the DNA they
    earn alone goes to the pack when the host is back; a host that loads another planet of the stage
    sends it after 5 s. Check: `coop_exit_menu.py` part 3.

Test infrastructure found on the way: the subagents of one session share the scratchpad folder
(another agent's `up.py` with `start('cell')` ran in this lane); `uifind.py` for menu clicks.

## Open issues

In [docs/known-issues.md](../known-issues.md), with reproductions:

* guest: the social game is simplified (no meter, no prompts; any social ability is the answer);
* while the host is in the main menu the guest's planet fills up with its own NPCs only slowly;
* the mating call (Backspace) did not lead the bot to the editor (not explored further).

Seen and left alone: hunts that time out on prey that flees (fast animals), the guest's own NPCs
being removed all session (`local_removed`, new spawns of the game: expected, the host owns NPCs).
