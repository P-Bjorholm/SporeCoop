# Cell stage: two players playing (stability report)

Branch `play/cell`, 2026-10-23. Lane 2 (instances 3 = host Alice, 4 = guest Bob), fixture `cell`
(Karke, food 0), headless test display, 1440x1080.

## What the bot does

`tools/tests/play_cell.py [--minutes 10] [--seed N]` (nightly suite: `play_cell`). Both players are
driven by the same bot, each with its own seeded random generator, about 3 decisions per second:

* **Eyes**: `coop.pool see` lists what is on that player's screen: kind (plant, meat, creature,
  part token, rock, bubble, poison), size relative to the own cell, distance, screen position.
  `coop.targets` gives the partner on screen (or beyond its edge).
* **Hands** (real input only):
  * hold the left mouse button towards a place and steer by moving the mouse (`button x y down`,
    `move`, `button up`: a new SporeKit command, the way most people swim),
  * click a place (the cell swims there),
  * hold W/A/S/D towards it (`key 0x57 down` ... `up`).
* **Intents**, re-chosen every 2-8 s: eat the nearest food (a diet learned from tries), hunt a
  smaller creature, flee a bigger one (30 % of the time it does not notice), wander, swim to the
  partner, idle. A part token on screen is collected. A knocked out partner: swim right up to it
  (revive). Knocked out itself: wait. A cinematic (`cine=1`): watch; when it still waits after
  2.5 s (a caption), click the green check (or press Enter).
* **Measures** every 10 s with console commands: food on both machines (must be equal), every
  synced cell's position on the guest vs the host (`coop.sync`, the coop_sync.py method: the closer
  of two host samples around the guest sample), players' positions, the partner seen by both,
  knockouts, revives, respawns, each player's own eats (`own_eats`/`own_food`), damage taken,
  which player the host's NPCs chase, eats while hurt (does eating heal), freezes / crashes
  (twoinst monitors) and caught faults (`exc`).
* **Checks**: the games kept running; the pair made progress (food +20); both ate by themselves;
  food equal on both machines at every check and at the end; cells p90 error ≤ 0.6 m; players p90
  ≤ 1.5 m; each sees the partner; same unlocked parts; no fault. Regressions of the bugs below:
  no mirror flies off (largest cell error < 50 m), the guest reports only what it ate, the
  partner's cell is not replaced while playing.

## Sessions

Runs on the fixed display (after the coordinator's headless fixes 0f4339d and fc94dbd, all mod
fixes in). Earlier runs found the bugs below; their sync numbers are not comparable (the display
gave 1920x1080 or overlapping windows).

| run | seed | min | food | own food host / guest | KO host / guest | revives | respawns | cells p90 / max (m) | players p90 / max (m) | damage host / guest (hp) | result |
|---|---|---|---|---|---|---|---|---|---|---|---|
| play9 | 8 | 10 | 0 → 517 | 282 / 235 | 3 / 1 | 1 (guest) | 3 (host) | 0.070 / 2.72 | 0.38 / 1.47 | 51 / 27 | OK |
| play10 | 9 | 22 | 0 → 678 | 294 / 384 | 16 / 1 | 3 (host) | 12 (host) | 0.103 / 2.97 | 0.37 / 23.9 (respawn teleport) | 86 / 34 | OK (the old eats check compared with food eats only, see 1.) |
| play11 | 10 | 10 | 0 → 574 | 220 / 354 | 4 / 1 | 0 | 4 / 1 | 0.143 / 3.27 | 0.34 / 29.5 (respawn teleport) | 25 / 16 | OK |
| solo1 | 9 | 15 | 0 → 402 (host alone) | 402 | (vanilla) | | | | | 20 | baseline |

Bug-finding runs before the last fixes (same bot): play1-play6 (10 min each, seeds 2-7): food 324-658
per session, food equal at all 360 checks, no fault, no freeze; cells p90 0.047-0.20 m.

Throughout: **0 caught faults, 0 freezes, 0 crashes of the games** (two runs were killed from
outside: the shared X server `:2` went away / was restarted, `XIO: fatal IO error` in
`out/run.3.log`). Food progress was equal on both machines at every one of the ~500 checks.

## Problems found and fixed

1. **Guest made the host destroy cells nobody ate** (commit 5e2bec1). The guest reported every
   mirror its own game removed as "eaten" (`MSG_CELL_EAT`) and the host destroyed its cell: ~1300
   reports in 10 minutes for ~30 real eats. Causes (from `coop.pool why`, return addresses of
   DestroyCell): the level change removes all cells of the old scale (0xE7CD10, ~600 per level),
   poison clouds outside the guest's active area (0xE78C00), the area cleared around a hatching
   cell (0xE78B20), size 0, attached cells whose parent is not local (behaviour 0x100E,
   0xE7A0D0), and mirror NPCs swallowing / eating other mirrors in the guest's own collision step.
   Fix: the guest skips collision pairs without its avatar (`HandleCollisionPair` 0xE7F140 hook),
   and reports only mirrors its avatar touched in the last second; other removals keep the mirror
   until the host's state drops it. After: 26-192 reports per 10-22 minutes (3-9 a minute), all
   cells the avatar touched: eats with food points (`own_eats`) plus small cells it swallowed
   whole (0xE791A0 / 0xE7D880, no food points), e.g. play11: 102 reports = 77 + 22 swallows,
   2 + 1 others, 32 of them with food.
2. **Mirrors flew off to 1e18 m on the guest** (5e2bec1). Soft-body NPC cells (`+0x250` > 1
   particles) were moved by writing `+0x4C` only; the centre went back to its particles and the
   particle velocities grew exponentially (2e6 m, 2e11 m, 4e18 m within a minute). Fix: soft-body
   mirrors are moved with `modes::cell_move` (particles too, velocities zeroed) every tick. After:
   largest cell error 2.7-6 m (fast cells during a level change), p90 0.05-0.2 m.
3. **The partner blinked out and in** (5e2bec1): 13 times in 10 minutes the host showed
   "Bob appeared in the world": the game removed the partner's puppet cell (level change
   0xE7CD10, an NPC breaking / swallowing it 0xE7ADD0) and the mod made a new one. Fix: the game
   never removes a partner's cell and no collision pair with it runs (it is a picture of a cell
   simulated on its owner's machine). After: 0 replacements (`puppet_new`) except around
   knockouts.
4. **Progress stalled behind a caption** (bot): a part caption ("When you see the new part
   appear, swim over the token to collect it") waits for the green check; a player who does not
   press it sees the game stand still. Not a mod bug; the bot now presses it like a person.

## Open issues (docs/known-issues.md)

* **The host's NPCs hunt the host, rarely the guest**: summed over the bot's samples, NPCs chasing
  the host 1067 / 2680, chasing the guest's cell 36 / 71 (play9 / play10). Damage taken: host 51 / 86
  hp, guest 27 / 34 hp; knockouts host 3 / 16, guest 1 / 1. Host-side bites on the guest's puppet
  never arrive (the puppet is not damageable there: no AI, not the avatar; `hurt_fwd` = 0 in every
  run); the guest is only bitten when its cell bumps into a mirror (`hurt_local` 0-16). Playing the
  guest is safer than playing the host.
* **A small respawned cell can spiral**: knocked out far from the partner it respawns at half size
  next to the partner, is hunted again (above) and stays small (play10: host size 0.24 from 11 to
  19 minutes, 12 respawns). Partly the hunting asymmetry.
* Progress stalls of 3-6 minutes happen with this bot also when playing alone (solo1: 84 food from
  3.5 to 5 minutes) - food is sparse at some scales; not a co-op problem.

## Notes for the next person

* `coop.pool` now prints `own_eats`, `own_food`, `bites_taken`, `damage_taken`, `cine`, `parts`,
  `chasing_me` / `chasing_partner` and (guest) `pairs_skipped`, `kept`, `puppet_saves`,
  `puppet_new`, `soft_moves`, `wipes`; `coop.pool see [n]`, `coop.pool cell <key>`,
  `coop.pool why`.
* The Karke cell has no weapon: hunting never bites (`hits_sent` 0), so guest bites through the
  host (MSG_CELL_HIT) are not covered by real play with this fixture (coop_cell.py covers them).
* Food rate with this bot: ~250-300 points per player per 10 minutes, as alone (solo1: 402 in 15).
