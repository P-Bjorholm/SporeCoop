---
id: BUG-0051
title: the guest's food total can briefly drop below a level line, so it levels down and up again
status: open
kind: desync
stage: Cell
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#29
role: guest
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: #29
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/29 (full text, discussion).

The guest overwrites its food total with every total the host broadcasts. A broadcast the host sent before it received the guest's latest meal takes that meal's points away again, until the next broadcast gives them back. If the meal had crossed a 50-point level line, the guest's game levels down and then up again: two extra zooms, and the old-scale cells are removed twice. Severity: wrong behaviour (no data loss: the totals end up equal).

### Where
- Guest applies the host's total as a difference: [mods/coop/cellsync.cpp#L1322-L1334](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L1322-L1334)
- Host broadcasts its total every 0.5 s when it changed: [mods/coop/cellsync.cpp#L921-L932](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L921-L932)
- Guest applies its own meal locally first and sends it later: [mods/coop/cellsync.cpp#L635-L653](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L635-L653), [L376-L387](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L376-L387), [L619-L627](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L619-L627)
- Host adds the guest's points when they arrive: [mods/coop/cellsync.cpp#L1292-L1303](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L1292-L1303), [L1317-L1321](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L1317-L1321)

### How to reproduce
1. Host and guest in the Cell stage, same pool. Bring both to a total just below a multiple of 50 (`coop.pool food <n>` on the host; the guest follows).
2. In the same second, make the host eat (`coop.pool food 2` on the host, which goes through the hooked AddFoodPoints) and the guest eat across the line (`coop.pool food 10` on the guest).
3. On the guest, log the food total at every frame, or watch the screen: the level-up zoom plays, then a level-down, then a level-up again. `coop.status` on the guest shows the total going 55 then 47 then 57.
4. With added latency (for example 100 ms on the loopback), it happens on most tries.

### Expected
The guest's total never drops below what it already counted for its own meals, unless the host refused them (a cell somebody else ate first, the `eats_late` case). One level-up, no level-down.

## Root cause
**What happens**

Let both machines be at total T = 45 (level line at 50).

1. The host eats 2 points: its total is 47. Within 0.5 s, `host_tick` broadcasts `MSG_CELL_FOOD {47}`.
2. While that message is in flight, the guest eats 10 points. `h_AddFood` runs the original AddFoodPoints at once: the guest's total is 55 and its game levels up. The 10 points are sent to the host with the eaten cell (`MSG_CELL_EAT`) or at the end of the frame (`MSG_CELL_FOOD`).
3. The guest receives the host's 47:
   ```cpp
   int diff = food - F<int32_t>(s, 0x1C);   // 47 - 55 = -8
   o_AddFood(diff, false);                 // through the game: level ups happen as usual
   ```
   AddFoodPoints with a negative amount runs a level change too (docs/re/cell.md section 4: "on change runs level-up/down"). The guest goes back to the level below 50.
4. The host receives the guest's 10, reaches 57 and broadcasts it. The guest applies +2 and levels up a second time.

The message carries no sequence number or acknowledgement, so the guest cannot tell a total that already includes its points from an older one. The window is the round-trip time plus up to 0.5 s of the host's send interval, every time the host's total changes (the host eats, or a third player's points arrive) close to a guest meal.

The plant counter `S+0x20` is overwritten directly from the broadcast and has the same lag, but no level depends on it.

### Confidence
CONFIRMED by reading the full path (both send paths and the guest's difference). The level-down on a negative amount comes from the ✔ note in docs/re/cell.md section 4 and was not run here.

## Fix
Not fixed yet. Suggested in the review:

On the guest, keep the sum of points sent and not yet confirmed. Have the host include per guest the last meal sequence number it applied in `MSG_CELL_FOOD`. The guest then applies `host_total + unconfirmed - local` and drops a meal from the unconfirmed sum once its number is confirmed. A refused meal (`eats_late`) is confirmed without points, so it is still taken back as today. Older peers without the field keep the current behaviour. Keep the old behaviour behind `coop.fix foodseq off`.

## Verification
Not done yet. Planned test (must FAIL without the fix and PASS with it):

`tools/tests/coop_cell_food_race.py` on `twoinst.start`: both players near a level line, the host's eat and the guest's eat across the line within 100 ms, with an artificial delay on the host-to-guest link. Sample the guest's game state `S+0x1C` (food total) and the guest's level from the game (the level table index, or the avatar target size `+0xB4`) every frame for 2 s through a debug command. Assert the guest's level never goes down and its total never drops below the value it had right after its own meal. It fails today (a dip below the line) and passes with the fix.

### Related
- AddFoodPoints: [docs/re/cell.md#L173-L177](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/re/cell.md#L173-L177)
- The meal-with-the-eaten-cell protocol (fix/cell-meat): [docs/re/cell.md#L470-L480](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/re/cell.md#L470-L480)
- Each extra level change runs RemoveCellsOfOldScale on the guest: [docs/re/cell.md#L283](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/re/cell.md#L283)

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #29.
- 2026-10-27: ticket opened from the issue.
