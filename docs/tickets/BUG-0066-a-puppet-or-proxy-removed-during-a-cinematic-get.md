---
id: BUG-0066
title: a puppet or proxy removed during a cinematic gets its AI back, and the host sends it to guests as an NPC
status: open
kind: bug
stage: Creature
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#44
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: #44
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/44 (full text, discussion).

`game::destroy_creature` takes the creature out of the puppet list before it decides to keep it until the running cinematic is over. While it waits, the game's own AI runs it again (NPCTickAI is no longer skipped). On the host, `npc::host_tick` then treats it as an NPC, gives it a net id and spawns it to the guests, so a guest can see an AI-driven copy of its own creature during the cinematic. Severity: wrong behaviour (minor, limited to the cinematic's length). It also contradicts the docs/known-issues.md entry that says these creatures "stand still".

### Where
- `destroy_creature` unregisters before deferring: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/game.cpp#L436-L451
- The AI skip depends on the puppet list: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/game.cpp#L243-L253
- The host tracks every updated creature that is not a puppet: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/npc.cpp#L231-L244
- Callers: puppet despawn https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/modes.cpp#L78 and proxy removal https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/npc.cpp#L365-L378
- The known-issues statement: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/known-issues.md#L29-L31

### How to reproduce
1. Two instances (host + guest), Creature stage.
2. Start a cinematic on the host (`call Simulator::cCinematicManager::PlayCinematic thiscall <cm> s:CRG_ClaimNest 1 1 0 0 0`, as in docs/re/creature.md 16), or evolve into a brain upgrade.
3. While it plays, make the guest change its creation (`coop.creations next creature`), so the host rebuilds the puppet. Or on the host run `coop.fix` / a test command that despawns the puppet.
4. On the host, `coop.npc list` shows a new `#id` for the old puppet's pointer; on the guest, `coop.npc list` shows a proxy of its own species, and it moves (position changes) while the cinematic plays.

### Expected
A creature that waits for its deferred destroy keeps the puppet treatment: no local AI, not tracked as a host NPC, not mirrored to guests.

## Root cause
**What happens**

1. A puppet (the host's copy of a guest's creature) or a proxy (the guest's copy of a host NPC) is removed: the guest leaves, its creation changes (puppet rebuilt), the host despawns an NPC.
2. `destroy_creature` runs:
   ```cpp
   puppet_unregister(c); // also used for UFOs and other cGameData puppets
   if (g_destroy_defer && noun_id(c) == 0x18EB45Eu && cinematic_running()) {
       ...
       g_destroy_later.push_back({c, world_token()});
       return;
   }
   ```
   It is no longer a puppet, but it stays in the world until `destroy_deferred_tick` runs after the cinematic.
3. `h_NPCTickAI` only skips registered puppets, so the creature's AI ticks again. A guest's puppet has no archetype tuning (+0xE84) and is in a herd of ours: the same kind of creature as the save strays of docs/re/creature.md 12. A proxy in a nest herd behaves like a normal NPC of that nest (it can wander or attack the local avatar; its hits are not filtered as proxy hits any more because it is out of `g_proxy_of`).
4. On the host, `host_tick` only skips `c == av || is_puppet(c) || is_destroyed(c)`. The deferred puppet gets a new id and `send_spawn` goes to every guest in range. The guest whose creature it was sees a second, AI-driven copy of itself until the cinematic ends and the despawn follows.
5. Guest-side NPC removal through the update filter is not affected: `destroy_pending` is checked there. `remove_saved_strays` skips it too.

### Confidence
CONFIRMED by reading the full path (`destroy_creature`, `h_NPCTickAI`, `host_tick`). Not verified at runtime: what the AI of an untuned puppet does during those seconds.

## Fix
Not fixed yet. Suggested in the review:

In `destroy_creature`, call `puppet_unregister(c)` only on the immediate path. In `destroy_deferred_tick`, unregister right before `DestroyInstance` (and also for entries that are dropped because the world changed). Callers that rely on `is_puppet(c)` being false at once (the proxy maps) already erase their own maps. Keep the old order behind `coop.npc cinedefer` or a new `coop.fix defer_keeps_puppet off`. Then update the known-issues entry: since the AI runs, "they stand still" holds only for creatures that are still puppets.

## Verification
Not done yet. Planned test (must FAIL without the fix and PASS with it):

`tools/tests/coop_cinematic_puppet.py`: two instances, Creature stage, host plays a cinematic and rebuilds the guest's puppet while it runs. Assert on the guest, using the game's state, that there is no creature of its own species other than its avatar within 50 m during the cinematic (`coop.npc creatures 0 <species>`). Assert on the host that the deferred puppet's position does not change (no AI movement). It fails on main and passes with the fix.

### Related
- docs/re/creature.md 16 (migration crash 0xC6AE60, why destroys are deferred), docs/re/creature.md 12 (untuned creatures with AI on).
- docs/known-issues.md "While a cinematic plays the creatures the mod removes on a guest ... stand still": needs a CORRECTION after this is confirmed.

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #44.
- 2026-10-27: ticket opened from the issue.
