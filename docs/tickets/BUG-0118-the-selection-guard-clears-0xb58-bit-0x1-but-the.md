---
id: BUG-0118
title: the selection guard clears +0xB58 bit 0x1, but the GOG 2024 selection code tests bit 0x2 (contradiction, possible crash)
status: open
kind: crash
stage: Creature
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#97
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: #97
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/97 (full text, discussion).

`h_SelectUI` guards `Simulator::CreatureSelectionUI` against the null `+0xE84` (archetype tuning) of puppets and NPC proxies. It does that by clearing `+0xB58 & 0x1` for the call, and the code calls that bit "pack member". The GOG 2024 disassembly note in the `.sym` file says the function tests **`al,2`** (mask 0x2) before it reads `(+0xE84)->+0x32C`. The docs give three different meanings for the bits involved. If the gate really is 0x2, the guard never covers the dangerous case, and selecting an NPC proxy after attacking it can still crash. CLAUDE.md "stop at a contradiction" applies: this needs a Ghidra check and a recorded CORRECTION before anything is built on it.

### Where
- The guard and its comment ("member of the player's pack (+0xB58 bit 1)", tests and clears mask 0x1): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L466-L489
- `data/symbols/overrides_dc04aee5.sym` L54 (GOG 2024): `Simulator::CreatureSelectionUI=0x00B6C970 ; verified port ...: start, plain ret; reads +0xB58 test al,2, then call 0xC0C300 (mov eax,[ecx+0xE84]) -> [eax+0x32C] at B6CA11 as described` https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/data/symbols/overrides_dc04aee5.sym#L54
- `data/symbols/overrides_922af7c7.sym` L113 (GOG 2016): "a pack member (+0xB58 bit 1) reads (+0xE84)->+0x32C". "bit 1" as a bit index is mask 0x2. https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/data/symbols/overrides_922af7c7.sym#L113
- docs/re/creature.md:
  - [L180](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/re/creature.md#L180) says 0x1 is "~ the alpha" and that the guard "treats the same bit as pack member".
  - [L354](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/re/creature.md#L354) says a pack member is `+0xB58 |= 0x100` (AddPosseMember 0xB23430).
  - [L66](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/re/creature.md#L66) says the stage event handler 0xD47FC0 needs `+0xB58 & 2` clear and sets it afterwards.
- `game::pack_member` uses 0x100: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/game.h#L110-L111
- The test checks only `& 1`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/tests/coop_select.py#L28-L31

The "pack member = 0x1" label is wrong in any case: the pack is 0x100, and 0x1 is the alpha by the docs.

### How to reproduce
**How to reproduce / check**

Ghidra checklist (GOG 2024 0xB6C970, GOG 2016 0xB6C930):
- [ ] Which immediate is tested on `[creature+0xB58]` in 0xB6C970, and which branch reaches `call 0xC0C300` / `[eax+0x32C]` (0xB6CA11)?
- [ ] In 0xD47FC0: where is the `or ..., 2` on +0xB58, and does it still run when `(+0xE84)->+0x32C == -1` (the blank object of `h_StageMsg`)?
- [ ] Writers of 0x1 (SocialPropose 0xDA43D0, "alpha") and of 0x100 (AddPosseMember 0xB23430): confirm the meanings.
- [ ] Runtime: on a guest, attack a proxy, read its `+0xB58` (`coop.npc` / `mem.read`), then click it.

### Expected
Selecting any puppet or proxy never reads a null `+0xE84`. The guard covers exactly the bit (or bits) the game tests, and the docs and `.sym` agree on what 0x1, 0x2 and 0x100 mean.

## Root cause
**What happens (if the gate is 0x2)**

1. The guest attacks an NPC proxy, which has `+0xE84 == null`. The attack posts 0xD335362C, and `CreatureStageEventHandler` 0xD47FC0 runs. `h_StageMsg` lends a blank `+0xE84` during the handler. The handler then sets `+0xB58 |= 2` on the creature (one event per creature). The blank pointer is taken away again afterwards.
2. The player clicks the proxy, which is the normal way to target it.
3. `CreatureSelectionUI` sees `+0xB58 & 2` and reads `(null)+0x32C`. `h_SelectUI` does nothing because `& 1` is clear. Result: an access violation at 0xB6CA11 (GOG 2024).

The same can happen on the host with a partner's puppet that the host attacked or socialised with.

### Confidence
The contradiction is CONFIRMED by reading the code, both `.sym` files and docs/re/creature.md. A second reviewer verified it. The crash path is PLAUSIBLE and not yet run. Medium severity. The guard's history is not recoverable (it is in the squashed root commit).

## Fix
Not fixed yet. Suggested in the review:

1. First record a CORRECTION in docs/re/creature.md (and the `.sym` comment) with the Ghidra evidence, per CLAUDE.md.
2. Then make the guard clear (and restore) the bit the game tests, probably 0x2, or both 0x1 and 0x2 if both paths reach the read.
3. A sturdier alternative: lend the blank `+0xE84` object during `CreatureSelectionUI`, as `h_StageMsg` does, instead of hiding a flag bit.
4. Rename the comment ("pack member") to what the bit really is.

## Verification
Not done yet. Planned test (must FAIL without the fix and PASS with it):

Extend `coop_select.py`: the guest first attacks an NPC proxy (or the host attacks or socialises with the partner's puppet), asserts in the game's memory that `+0xB58 & 2` is now set and `+0xE84` is null, then clicks it. It asserts that the game is still running and that `exc` reports no fault.
- FAILS on main if the gate is 0x2: a fault at 0xB6CA11.
- PASSES with the fix.

### Related
#44, #22 (proxies and recycled creature objects).

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #97.
- 2026-10-27: ticket opened from the issue.
