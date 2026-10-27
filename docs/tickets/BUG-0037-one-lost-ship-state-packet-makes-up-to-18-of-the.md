---
id: BUG-0037
title: one lost ship-state packet makes up to 18 of the host's ships explode and respawn on the guest
status: open
kind: desync
stage: Space
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#15
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: #15
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/15 (full text, discussion).

With more than 18 NPC ships in the host's place (a raid wave, a colony with turrets and defenders), the host sends the ship state in several unreliable chunks. If one chunk is lost or arrives late, the guest treats every ship of that chunk as gone. It blows them up with the game's own death (explosion, loot) and spawns them again 0.1 s later. Severity: wrong behaviour, visible on any real internet link during fights.

### Where
- Host sends the chunks unreliably: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/squadron.cpp#L208-L234
- Guest collects the ids of one cycle and runs the removal pass on the last chunk: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/squadron.cpp#L263-L318
- `destroy_mirror(u, true)` uses the game's death path: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/squadron.cpp#L249-L261

### How to reproduce
1. Host and one guest in Space, both on the host's colony planet (same place).
2. Get more than 18 NPC ships listed: a raid wave from `space_contracts` (2 crew: 2 bombers + 8 raiders, then 3 + 12) plus the colony's turrets and defenders, or `coop.fleet spawn 2` 20 times on the host.
3. Drop some UDP packets from host to guest (netem on the lane's loopback, or a test switch, see Test).
4. On the guest, `coop.fleet list` shows `removed=` and `created=` climbing while the host's ships did not die. On screen, ships explode and reappear.

### Expected
A lost or late chunk changes nothing. A mirror is only removed when the host no longer lists the ship.

## Root cause
**What happens**

1. `host_tick` (10 Hz) splits the ships into chunks of `per = 18`. Each chunk carries `cycle`, `chunk`, `chunks` and is sent with `send_unreliable_to(pl.id, net::MSG_UFO_STATE, w)`.
2. `guest_state` clears `g_cycle_ids` whenever a packet with a new `cycle` arrives. It then inserts the ids of that packet only.
3. When `chunk + 1 == chunks` (the last chunk), it walks `g_mirrors`. Every mirror whose id is not in `g_cycle_ids` is removed:
   ```cpp
   if (!g_cycle_ids.count(it->first)) {
       if (it->second.u) {
           g_mirror_of.erase(it->second.u);
           destroy_mirror(it->second.u, true);   // TakeDamage 1e6: explosion, loot
   ```
4. Nothing checks that all chunks of the cycle arrived. If chunk 0 of a 2-chunk cycle is lost, only the 1..18 ids of chunk 1 are in the set. All ships of chunk 0 are exploded with `o_TakeDamage(..., 1e6f, ...)`, the game's NPC death (explosion, loot drop 0x102BA40, docs/re/space.md 2.1 step 10).
5. The next cycle (0.1 s later) carries those ids again. `g_mirrors[id]` has no `u`, so `SpawnUFO` creates new ships and teleports them in.
6. The same happens if a stale chunk of an older cycle arrives after a newer one (it resets `g_cycle_ids` for its own cycle number).

With 1 % loss at 10 Hz and two chunks, this happens about once every 10 s during a big fight.

### Confidence
CONFIRMED by reading the full path (`host_tick` -> unreliable send -> `guest_state` removal pass). Not reproduced at runtime: the lab runs on loopback without loss, which is why the tests pass. Whether the guest can pick up the loot of the fake deaths is not verified.

## Fix
Not fixed yet. Suggested in the review:

- Track which chunks of the current cycle arrived (a bitmask, `chunks` is at most 15 since `all_ufos` caps at 256). Run the removal pass only when all `chunks` of the cycle are in. Ignore packets of a cycle older than `g_rx_cycle` (16-bit serial compare).
- Additionally (or instead), remove a mirror only when it has been missing for a while, e.g. `now - m.seen > 0.5`. Remove it without the explosion when it was not marked dead by the host.
- Keep the old behaviour behind `coop.fix fleetchunks off` so the lab can check the test still catches it.

## Verification
Not done yet. Planned test (must FAIL without the fix and PASS with it):

`tools/tests/coop_space_fleet_loss.py` on `twoinst.start('space')`:
1. Host and guest land on the same planet. The host spawns 25 pirates (`coop.fleet spawn 2` x 25) so the state needs 2 chunks.
2. Add a host debug switch `coop.fleet drop <chunk> <n>`: skip sending the next n packets of that chunk index. Use `drop 0 5`.
3. Read the guest's UFO noun list (the game's own list, `GetAllUFOs`, through `coop.fleet list` pointers and `mID` +0x24). Assert that the set of ship objects is the same before and after the drop: same pointers / mID, none destroyed or newly created.
4. Without the fix the guest's list changes (new objects for the chunk-0 ships). With the fix it stays the same. Also check that no game wrote a crash report.

### Related
- The same chunk pattern exists in `mods/coop/cellsync.cpp` (Cell stage mirrors). That one is filed separately by the Cell review.
- docs/re/space.md section 1 (mirror recipe), section 2.1 (TakeDamage death path), section 9.4 (the game frees NPC UFOs).

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #15.
- 2026-10-27: ticket opened from the issue.
