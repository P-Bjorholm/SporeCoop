---
id: BUG-0032
title: one lost state packet makes the guest delete and recreate up to 14 of the host's cells
status: open
kind: desync
stage: Cell
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#10
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: #10
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/10 (full text, discussion).

The host sends its Cell stage pool to the guest as several unreliable packets per round, 14 cells each. When the last packet of a round arrives, the guest deletes every mirror cell not listed in that round, so the cells of any packet that was lost or reordered are deleted. They come back 0.1 s later as new cells: they blink, lose their facing, and a dying creature plays its death again. Severity: wrong behaviour, frequent on any link with packet loss.

### Where
- Removal pass: [mods/coop/cellsync.cpp#L1052-L1065](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L1052-L1065)
- Round bookkeeping: [mods/coop/cellsync.cpp#L1000-L1007](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L1000-L1007)
- Host side, chunks sent unreliably: [mods/coop/cellsync.cpp#L950-L982](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L950-L982)
- Unreliable data also dropped on purpose under load: [mods/coop/net/session.cpp#L481-L493](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/net/session.cpp#L481-L493)

### How to reproduce
1. Two instances (host and guest) on one lane, both in the Cell stage in the same pool, guest next to the host. Wait until `coop.pool` on the guest reports 30 or more mirrors.
2. Drop about 5 % of the host-to-guest UDP packets (for example a loss filter in the lab's network layer or `tc netem loss 5%` on the loopback for the game ports).
3. Watch `coop.status` on the guest: `created` and `destroyed` in `cell(guest):` rise steadily, although the host's pool does not change. On screen, cells near the guest blink and re-hatch.

### Expected
A lost or reordered state packet leaves the mirrors it would have updated where they are. A mirror is removed only when the host really no longer has the cell.

## Root cause
**What happens**

1. `host_tick` collects the cells within 120 m of the host, bumps `g_cycle` and sends `ceil(n / 14)` `MSG_CELL_STATE` messages with `send_unreliable_to`, each with `{cycle, chunk, chunks, count}`.
2. `guest_state` clears `g_cycle_seen` whenever a message with a new cycle arrives, then inserts the host index of every cell in the message.
3. When the message with `chunk + 1 == chunks` arrives, it runs the removal pass:
   ```cpp
   if (chunk + 1 < chunks)
       return;
   for (auto it = g_mirrors.begin(); it != g_mirrors.end();) {
       if (!g_cycle_seen.count(it->first)) {
           ... destroy_local(it->second.local); ...
           it = g_mirrors.erase(it);
   ```
4. Nothing checks that chunks `0 .. chunks-2` of this cycle arrived. If chunk 0 was lost, all 14 cells in it are "not seen" and are destroyed on the guest (`DestroyCell`).
5. The next round (0.1 s later) does not find them, so `cell_create` makes new local cells with new pool indices. The new mirror starts with a fresh soft body. If the host shows the creature dying, `KillCell` is replayed on it a second time. Anything the guest's game held by the old local index is lost: a chase target, a pending bite event, the touch record in `g_touched`.
6. Reordering gives the same result. If the first chunk of cycle N+1 overtakes the last chunk of cycle N, the late last chunk resets the seen set to only its own cells, so nearly every mirror is purged.
7. `Session::flush` sends at most `burst + 64` packets per peer and then calls `drop_unreliable()`. When a reliable bulk transfer (world, creations) shares the window, cell state chunks can be dropped even on a LAN.

With 40 cells in range (3 chunks at 10 Hz, 30 packets/s), a 1 % loss rate means 14 cells blink about every 3 seconds.

### Confidence
CONFIRMED by reading the full path (host send, `guest_state`, `destroy_local`, the next round's `cell_create`). Not run in the game: the visual effect of the recreation (re-hatch effect or not) is inferred.

## Fix
Not fixed yet. Suggested in the review:

Count the chunks received per cycle and run the removal pass only when all of them arrived. Or, more robust, remove a mirror only when it has not been seen for a time (`m.seen` older than about 0.5 s, 5 rounds). Ignore messages from an older cycle (compare with wrap-around) instead of resetting the seen set. Keep the old behaviour behind `coop.fix cellpurge off` so the lab can show that the test catches it.

## Verification
Not done yet. Planned test (must FAIL without the fix and PASS with it):

`tools/tests/coop_cell_loss.py` on `twoinst.start`: host and guest in the same pool, 30+ cells in range. Turn on loss of host-to-guest unreliable packets (a debug switch in the session that drops every Nth unreliable message, for example 1 in 10). For 20 s, sample the guest's mirrors with `coop.sync` once a second and record the guest's local pool index of each host key. Assert that no host cell that still exists on the host (checked on the host with `coop.sync`) changes its local index on the guest. Without the fix the indices change within seconds; with it they stay the same. Also assert no crash report on either game.

### Related
- The same pattern exists in mods/coop/squadron.cpp for Space (`g_cycle_ids` removal pass around L265-L312); it is filed separately by another reviewer.
- docs/re/cell.md section 7, "Implementation recipe (host-authoritative tide pool)": [docs/re/cell.md#L241](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/re/cell.md#L241)

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #10.
- 2026-10-27: ticket opened from the issue.
