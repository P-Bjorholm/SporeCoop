---
id: BUG-0044
title: a recycled NPC object keeps the old net id, so guests see the wrong species or a walking corpse
status: open
kind: desync
stage: Creature
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#22
role: guest
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: #22
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/22 (full text, discussion).

The host gives NPC net ids by creature object pointer and keeps the mapping for 2 s after the creature last updated. Creature objects are pooled. When the game frees an NPC and builds a new one in the same object within that time, the new NPC inherits the old id and no new spawn goes to the guests. Guests keep showing the old species, and if the old NPC was dead, a corpse dragged along the path of a living creature. Severity: wrong behaviour (desync of what the players see).

### Where
- Id by pointer, reused while `last_seen` is younger than 2 s: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/npc.cpp#L231-L257
- Guest: `px.killed` is only reset by a new MSG_NPC_SPAWN or a world change: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/npc.cpp#L461-L482
- The species is sent only in the spawn: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/npc.cpp#L179-L200

### How to reproduce
1. Two instances, Creature stage, NPC sync on (default).
2. Host and guest stand near a nest. The host kills an NPC and eats the corpse until it is gone, then waits for the nest to refill. Or the host runs far away and back to make the game unload and recreate NPCs.
3. On the host, `coop.npc list` shows a pointer with the same `#id` but another species; on the guest, `coop.npc list` shows the proxy with the old species and ` DEAD`.
4. Faster: a test command that destroys a host NPC (`DestroyInstance`) and spawns another species next to it in the same frame, then checks whether the new object got the freed pointer.

### Expected
A new creature always gets a new net id (or at least a fresh spawn), and the guest's proxy shows the species and the alive state of the host's creature.

## Root cause
**What happens**

1. `host_tick` loops over the creatures that ran `Update` this frame. For a pointer already in `g_host_npcs` it only refreshes `last_seen`. A pointer is forgotten (despawn to guests) only after 2 s without an update.
2. Creature objects are pooled: `cCreatureAnimal::Create(pos, species, age, herd, isAvatar, reuse)` has a reuse flag, and `npc::creatures()` lists "creature objects without a species: not in the world (pool)".
3. If NPC X is freed (eaten corpse, unloaded when the avatar runs off, herd cleanup) and the game creates NPC Y in X's object within 2 s, `g_host_npcs.find(Y)` hits X's entry: same id, no `send_spawn`, so the species key and `max_health` are never sent again.
4. On the guest, the proxy for that id keeps X's species. If X died, `px.killed` stays true forever, because the state path never clears it:
   ```cpp
   if (!px.killed)
       drive::step(...);
   else { // a corpse lies where the host has it
       ...
       game::teleport(px.c, s.pos, s.rot);
   }
   ```
   So the guest sees X's corpse teleported along Y's living path, with no health or impressed updates.

### Confidence
PLAUSIBLE. The code path is CONFIRMED by reading. Not verified: how often the game hands out a freed creature object again within 2 s (pool order, timing).

## Fix
Not fixed yet. Suggested in the review:

Store the species key (and the dead flag) in `HostNpc`. In `host_tick`, when a known pointer now has another species key, or went from dead to alive, despawn the old id for every guest and give it a new id with a new spawn. On the guest, also clear `px.killed` when a state says the NPC is alive (no `NF_DEAD`). Keep the old behaviour behind `coop.fix npc_reuse off`.

## Verification
Not done yet. Planned test (must FAIL without the fix and PASS with it):

`tools/tests/coop_npc_reuse.py` on two instances (host + guest, Creature stage): on the host, destroy an NPC and create a creature of another species in the same frame until the pointer is reused (debug command). Assert on the guest the game's own state of the proxy at that id: its species key (`+0xB28`) equals the host creature's, and its dead flag (`+0xB5E`) is 0 when the host's creature is alive. It fails on main and passes with the fix.

### Related
- BUG-0023 (net ids repeat after about 65k allocations): same allocator, a different cause.
- docs/re/creature.md 11 (NPC ids across worlds), 14.1 (how a creature dies).

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #22.
- 2026-10-27: ticket opened from the issue.
