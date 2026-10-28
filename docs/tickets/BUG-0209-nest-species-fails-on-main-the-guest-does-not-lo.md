---
id: BUG-0209
title: nest_species fails on main: the guest does not load the host's world a second time after coop.share
status: open
kind: test
stage: Creature
reported: 2026-10-28
source: lab run on lane 6, fix/w4-registry and main 7874654
role: 
versions: main 7874654
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
`coop_nest_species.py` (suite `nest_species`) fails on lane 6 at "the guest loaded the host's world
again": after the host's `coop.save` and `coop.share` the test waits 120 s for one more
`world: loading ` line in the guest's `log 400` and for both games in the world. Every species
check before and after passes. Seen on fix/w4-registry (out/lab/20261028-031532) and on main
7874654 alone (lab run 20261028-033240 on lane 6). It passed last on fix/w2-creature
(20261027-232407), so waves 2/3 changed something (world sharing, the save box, or the log).

How to reproduce: `tools/lab.py run nest_species --lanes 1 --skip <all other lanes>` on main.

Not analysed yet. Suspects: the check counts lines in the last 400 log lines (a busy log scrolls
the old line out and the count does not grow); the share waits for something new since BUG-0112 /
BUG-0157 / BUG-0019 (one save for waiting shares, no load inside an editor, the evolution
cinematic).

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix (run dir), PASS with it (runs) -->

## History
- 2026-10-28: opened.
