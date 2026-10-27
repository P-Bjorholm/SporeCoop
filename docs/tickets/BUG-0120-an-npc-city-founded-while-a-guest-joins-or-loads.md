---
id: BUG-0120
title: an NPC city founded while a guest joins or loads never appears on that guest
status: open
kind: desync
stage: Civ
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#99
role: guest
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: #99
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/99 (full text, discussion).

The host tells its guests about a new NPC city with one reliable `MSG_CIV_BUILD` (`kNextCitySite`). A guest that is not in the Civ stage when it arrives (receiving and loading the host's world, a loading screen) drops it, and nothing sends it again. That guest never gets the city: its own game is blocked from founding cities, and the snapshot cannot create one. Severity: desync for the rest of the stage (low to medium).

### Where
- The host sends the founding once: [mods/coop/civ.cpp#L435-L455](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/civ.cpp#L435-L455)
- Guests block their own founding (same hook): [mods/coop/civ.cpp#L439-L444](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/civ.cpp#L439-L444)
- Every civ message is dropped outside the Civ stage: [mods/coop/civ.cpp#L1615-L1618](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/civ.cpp#L1615-L1618), with `active()` at [L52](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/civ.cpp#L52)
- The guest replays the founding: [mods/coop/civ.cpp#L1563-L1576](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/civ.cpp#L1563-L1576)
- A host city with no local city within 60 m maps to null and is skipped: [mods/coop/civ.cpp#L1366-L1374](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/civ.cpp#L1366-L1374), [L1263-L1276](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/civ.cpp#L1263-L1276)
- The host saves before sending the world, so cities founded after that save are not in the guest's copy: [mods/coop/coop.cpp#L231](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L231), [L866-L869](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L866-L869)
- How often cities appear (4-6 in 10 minutes): [docs/re/civ.md#L590-L600](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/re/civ.md#L590-L600)

### How to reproduce
1. Host in Civ on a planet where NPC cities appear (claim a spice geyser, about $8000; docs/re/civ.md 9.3). For a sure hit, call `AppearNextCity` from the console the way `new_stage_game.py civ --npc-cities N` does, while the guest loads.
2. Let a guest join, and found a city on the host while the guest's lobby shows "receiving world" or the game is loading.
3. After the guest is in the world: the host's `coop.civ` lists N cities, the guest's lists N-1, and the new city is missing on the guest's screen. The host's next new city appears on the guest at the earlier site.

### Expected
Every city of the host exists on every guest once the guest is in the Civ stage, whenever it was founded.

## Root cause
**What happens**

1. The host plays Civ. A guest joins, or the host enters Civ and the guests follow. `share_with` saves the host's game and starts the transfer.
2. During the transfer and the guest's load, the host's planet founds a new NPC city: `h_SpawnNextCity` succeeds on the host and broadcasts:
   ```cpp
   w.u8(kNextCitySite);
   w.u32(0);
   w.f32(0); w.f32(0); w.f32(0);
   C.sess->broadcast_reliable(net::MSG_CIV_BUILD, w);
   ```
3. On the guest, `civ::on_message` returns at once (`!active()`: the mode is not Civ yet), so the message is consumed and lost.
4. The guest loads the host's save, which was written before that city was founded. In Civ, `h_SpawnNextCity` on a guest always returns false, so the guest's own planet never founds it.
5. `MSG_CIV_STATE` lists the city, but `local_city_near` finds nothing and the entry stays null. Builds, captures, alarms, buys and gifts for that host city index are dropped on this guest. Vehicles going there drive to an empty site.
6. The next `kNextCitySite` makes the guest found "the free site with the lowest order", which is the one it missed, so it stays one city behind.

Not traced: the political id the replayed city gets on the guest (the docs say it is the same counter on both machines), and whether the shift then makes `guest_cities` call `ChangeOwner` on the wrong city. That is a possible further effect, unverified.

### Confidence
CONFIRMED by reading the full path; verified independently by a second reviewer (low to medium). The main window is between the host's share save and the end of the guest's load; a guest leaving Civ for a loading screen mid-stage is a second, rarer window. Not run in the game. The political id consequences are not traced (see above).

## Fix
Not fixed yet. Suggested in the review:

Make the founding state, not an event:
- the host adds a "cities founded so far" counter (or the city count) to `MSG_CIV_STATE`;
- a guest in Civ calls `o_SpawnNextCity` (with `g_applying`) until its own count matches, at most one per snapshot because the city appears a few seconds later.

A second option: the host resends `kNextCitySite` events to a guest once it reports the Civ mode.

## Verification
Not done yet. Planned test (must FAIL without the fix and PASS with it):

`coop_civ_*` on two lanes:
1. The host founds a city (`AppearNextCity`, instant) while the guest is loading the world.
2. Assert on the guest: the game's city list (`all_cities` count and positions) has a city within 60 m of every host city.

It FAILS on main (one city missing) and PASSES with the fix.

### Related
#37 (civ tables survive a load), #9 (civ ownership after a leave), #10 and #15 (same "lost once, never repaired" pattern for snapshots).

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #99.
- 2026-10-27: ticket opened from the issue.
