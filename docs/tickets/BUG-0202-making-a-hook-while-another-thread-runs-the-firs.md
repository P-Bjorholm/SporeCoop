---
id: BUG-0202
title: making a hook while another thread runs the first bytes of the hooked function can crash that thread (hot reload re-hooks RandomInt)
status: open
kind: crash
stage: All
reported: 2026-10-27
source: found by kit_freeze.py hookchurn, fix/w1-sporekit
role: both
versions: 0.7.2.4 and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0132
---

## Report
Seen in the first version of `kit.test.hookchurn` (tools/tests/kit_freeze.py, BUG-0132), which made
and removed a hook 200 times while 3 threads called the hooked function: with the BUG-0132 fix on
the game still crashed, `0xC0000005 at sporekit.dll+0x22102, write of address 0x794B2100` (lane 3,
lab run out/lab/20261027-184432, crash-20261027-194506-i5.txt): eip two bytes into the hooked
function, executing the rel32 bytes of the new `jmp` as instructions.

In play: hot reload of coop.dll (`mod.reload coop`, `tools/lab.py reload`) hooks
`Math::RandomNumberGenerator::RandomInt` / `RandomFloat` again while game worker threads may call
them (docs/re/space.md 7.3). At the first load the hooks are made before worker threads draw from
the RNG (likely), so it matters mostly for the reload.

How to reproduce: make `hooks_churn_test` create the hook while the worker threads run (remove the
gate `g_churn_go` in sporekit/hooks.cpp) and run `kit.test.hookchurn 200 3`.

## Root cause
Observed: MinHook writes the 5-byte `jmp` over the function's first instructions without stopping
the other threads (freezing is off under Wine, it deadlocked). A thread that has executed the first
original instruction (here 2 bytes) continues at offset 2 and decodes the middle of the `jmp`.
MinHook's own answer (Freeze + ProcessThreadIPs moves suspended threads' eip) is what is switched
off. Removal does not have this problem: while hooked, no thread can be inside the first 5 bytes.

## Fix
Not fixed. Options: suspend only the game's worker threads that can run the target, and only for
hooks on such functions (a per-hook flag), or keep the RandomInt / RandomFloat hooks of coop alive
across a reload (a stub owned by sporekit.dll that calls into the current coop.dll, as uiproc.cpp
does for UI handlers), so they are made once per process.

## Verification
<!-- the test: FAIL without the fix (run dir), PASS with it (runs) -->

## History
- 2026-10-27: opened (found by the BUG-0132 test on fix/w1-sporekit).
