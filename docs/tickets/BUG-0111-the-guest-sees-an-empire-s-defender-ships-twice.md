---
id: BUG-0111
title: the guest sees an empire's defender ships twice, because any UfoType 6 ship counts as "our puppet"
status: open
kind: bug
stage: Space
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#90
role: guest
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: #90
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/90 (full text, discussion).

The mod uses `mUFOType == 6` (Defender) to recognise the partners' puppets, which it spawns as Defender UFOs. The game spawns Defenders of its own too: an NPC empire's system has defenders (type 6), and a colony's own defenders are also type 6 (docs/re/space.md, sections 7.3 and 15.5). Two problems follow:

1. **Confirmed:** the guest never removes its own game's type-6 ships in the host's place, while the host mirrors its own type-6 ships to the guest. The guest sees every defender twice. Its local copies run their own AI and are never synced.
2. **Plausible:** `space_alive` accepts any listed type-6 UFO at a puppet's old address. docs/re/space.md 9.4 says freed UFO addresses get reused. When that happens, the mod adopts the game's new Defender as the partner's ship, then drives it, rewrites its political id, or destroys it.

### Where
- Host: mirrors every ship except its own and the puppets, type 6 included: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/squadron.cpp#L188-L192
- Guest: removes its local NPC ships in the host's place, but skips every type 6 ("the mod's player puppets"): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/squadron.cpp#L318-L326
- The puppet liveness check is listed + alive + type 6: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/modes.cpp#L209-L218
- Where it is used, in `drive_puppet`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L309-L331
- What then happens to the adopted ship: `space_despawn` (DestroyInstance) and `space_drive` (teleport, HP pin): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/modes.cpp#L179-L207
- Rivalry writes our political id (or the enemy id) into any listed type-6 "puppet": https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/space_rivalry.cpp#L625-L641
- Squadron trusts the raw puppet pointer. It drops the shots of a ship it takes for a puppet and forwards NPC hits on it to the guest: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L2890-L2900

### How to reproduce
Part 1:
1. Host and guest, `space` fixture, in an NPC empire's home system (or on a colony while a raid spawns its defenders).
2. On both machines, `coop.fleet list` shows the ships with types. On the guest, count the type-6 ships that are not in `g_mirror_of` (they have no `#id`). Compare with the host's type-6 count. The guest has the host's defenders as mirrors plus its own.

Part 2 (address reuse):
1. With a partner puppet shown, zoom from a planet of an NPC empire to the system several times.
2. Log in `drive_puppet` when `space_alive` passes for a UFO whose model key (+0x780) or political id differs from what `space_spawn` gave it.

### Expected
- The guest shows only the host's ships in the host's place: one defender per host defender.
- A puppet is recognised as ours by something only our puppets have, so a game ship is never driven, recoloured or destroyed by the mod.

## Root cause
**What happens**

1. Host and guest are in the same NPC empire system, or on a colony planet whose defenders have spawned. The host's defenders arrive on the guest as squadron mirrors. The guest's own game spawns its own defenders too, and `guest_state` keeps them because they are type 6. The guest sees twice the defenders. Its local ones fight on their own: they shoot at ships, take hits and die without the host knowing.
2. On a planet to system switch, the game frees every NPC UFO (DestroyNPCUFOs), the partner's Defender puppet included. If a new type-6 UFO is allocated at that address before the next `on_frame` (the system's or colony's defenders spawning, or a squadron type-6 mirror that `g_sess.update()` spawns in the same frame on the guest), `space_alive` returns true:
   - The partner is no longer in the same place: `despawn` calls DestroyInstance on the game's own ship.
   - The partner is in the same place: the game ship is teleported to the partner every frame and its HP pinned. `puppets_tick` gives it our political id, so an NPC empire's defender joins our side. Squadron drops its shots and sends NPC hits on it to the guest as MSG_UFO_DAMAGE.

### Confidence
- Part 1: CONFIRMED by reading the code above. The game spawning type-6 defenders is documented in docs/re/space.md.
- Part 2: PLAUSIBLE, because it depends on the allocator reusing the address within one frame (docs/re/space.md 9.4 notes such reuse).
- A second reviewer verified both readings.

## Fix
Not fixed yet. Suggested in the review:

- Squadron guest: skip only our real puppets (`C.puppet_owner(u) >= 0`) instead of every type 6, so the guest's own defenders in the host's place are removed like its other NPC ships.
- Puppet identity: remember at spawn what makes the puppet ours, and check all of it in `space_alive`:
  - the model key at +0x780;
  - `mbInvincible` = 1;
  - no mission at +0x800;
  - political id in {ours, the rivalry enemy id}.

  Better still: forget a puppet for good in the first frame it is missing from the UFO list, and check that every frame before any other module uses it.

## Verification
Not done yet. Planned test (must FAIL without the fix and PASS with it):

Two-instance test in the `space` fixture, in a system with NPC empire defenders. Assert the guest's type-6 count against the host's in the host's place (the game's own UFO list, not mod counters).

- FAILS without the fix: the guest has more type-6 ships than the host.
- PASSES with it: the counts are equal.

For part 2, a test that forces a context switch with a partner puppet. It checks that the host's NPC defender count and their political ids are unchanged after the switch, and that the partner's ship is respawned with its own model.

### Related
#15, #23, #32, #62 (other UFO lifetime and mirror issues in Space).

This came from an inventory of about 90 game-object pointers the mod keeps across frames. Six of them are guarded only by the UFO noun list, and three of those also rely on the type-6 check above.

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #90.
- 2026-10-27: ticket opened from the issue.
