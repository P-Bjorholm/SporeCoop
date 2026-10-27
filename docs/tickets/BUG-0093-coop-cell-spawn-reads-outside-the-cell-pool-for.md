---
id: BUG-0093
title: coop.cell spawn reads outside the cell pool for a slot number that does not exist
status: open
kind: tooling
stage: All
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#71
role: 
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: #71
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/71 (full text, discussion).

The debug command `coop.cell spawn <slot>` takes the slot from `atoi` and indexes the cell pool with it, with no range or liveness check. A slot above the pool size, a negative number or a free slot makes it read a model pointer and a scale from outside the pool, or from a dead cell, and pass them to `CreateCellObject`. Severity: minor (a debug command, but it crashes the game it is typed into).

### Where
https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/debug.cpp#L83-L93

```cpp
uint32_t slot = (uint32_t)atoi(argv[2]);
uint8_t* src = data + slot * 0x398;          // no check against n or the slot's index
... *(void**)(src + 0x108), *(int*)(src + 0x358) ...
```

### How to reproduce
In the Cell stage: `coop.cell spawn 99999`, or `coop.cell spawn -1`. Expect an access violation (in SporeKit's fault handler, or a game crash inside CreateCellObject).

### Expected
"no such cell" for a slot that is out of range or not a live cell.

## Root cause
**What happens**

1. `list` above checks `i <= n && i < 4096` and that `(*(uint32_t*)c & 0xFFFF) == i` (a live cell).
2. `spawn` does neither, so `coop.cell spawn 99999` reads `data + 99999 * 0x398` (about 92 MB past the pool) and passes the read value as a resource pointer to `CreateCellObject`.

### Confidence
CONFIRMED by reading.

## Fix
Not fixed yet. Suggested in the review:

Before using `src`:
```cpp
if (slot == 0 || slot > n || slot >= 4096 || (*(uint32_t*)src & 0xFFFF) != slot) { SAY("no such cell"); return 1; }
```
Use `strtoul` with an end check instead of `atoi`.

## Verification
Not done yet. Planned test (must FAIL without the fix and PASS with it):

Add a step to `tools/tests/coop_cell_leave.py` (or any cell test): `coop.cell spawn 99999` must answer "no such cell", and the game must still run (no crash report). It fails (crash) on main.

### Related
- Other debug commands with unchecked input: `coop.npc goherd <hex>` checks the pointer, `coop.cell spawn` does not.

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #71.
- 2026-10-27: ticket opened from the issue.
