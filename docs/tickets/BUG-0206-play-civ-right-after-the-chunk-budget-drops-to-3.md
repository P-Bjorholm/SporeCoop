---
id: BUG-0206
title: play_civ: right after the chunk budget drops to 300, four vehicles show owner 0 on the host and 1 on the guest for one audit
status: open
kind: desync
stage: Civ
reported: 2026-10-28
source: play_civ.py --seed 9, fix/w3-civ
role: guest
versions: fix/w3-civ (main 5ae9744 + wave 3 Civ fixes)
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0186
---

## Report
`SPORE_LANE=7 python3 tools/tests/play_civ.py --minutes 10 --seed 9` (fix/w3-civ, 2026-10-28): one
audit right after the test lowered the chunk budget to 300 (`chunk budget 300`) listed vehicles
#1, #6, #7, #8 with owner 0 on the host and owner 1 on the guest (after gifts from the guest to the
host); the next audits were clean. The run's RESULT was FAIL from these lines and from a spice
claim / move of the host's last seconds that the end audit did not see. The fix/w2-civ run of the
same seed (before wave 3) was OK. Not reproduced or analysed: possibly the owners of vehicles in the
second chunk arrive one cycle later after the split (stale for one audit), or a gift racing the
chunk change. Nothing in the wave 3 changes touches owners without a change of the player set.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix (run dir), PASS with it (runs) -->

## History
- 2026-10-28: opened.
