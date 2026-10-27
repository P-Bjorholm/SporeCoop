---
id: BUG-0042
title: six suite tests (smoke creature included) pass with faults and check only mod bookkeeping
status: open
kind: test
stage: All
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#20
role: 
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: #20
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/20 (full text, discussion).

Six suite entries print the games' fault counters but never fail on them, run without the freeze/crash monitor, and assert mostly mod counters. They are `creature` (smoke), `cell`, `tribe`, `civ`, `space` and `phase`. A build that throws access violations in `coop.dll`, or a guest that never shows the host moving, can pass them, the smoke `creature` test included.

### Where
- Suite entries: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/tests/suite.py#L24-L29
- `coop_two_instances.py` checks and end: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/tests/coop_two_instances.py#L99-L144
- `coop_mode.py` end: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/tests/coop_mode.py#L186-L203
- `coop_phase.py` end: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/tests/coop_phase.py#L89-L93
- `lab.py` collects fault lines for display only: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/lab.py#L116-L123

### How to reproduce
Reading the code is enough for the scripts. For the lab side, a fake test with a stub `tools/spore.sh` through `lab.cmd_run` (same driver as in the crash-after-checks issue):

```python
suite.add('faults', 'python3 -c "print(\'host faults: total faults: 3 (0xC0000005 at coop.dll+0x1234)\');print(\'RESULT: OK\')"', ('smoke',))
```

Output:

```
  OK   faults          lane 1     0 s
         host faults: total faults: 3 (0xC0000005 at coop.dll+0x1234)
PASSED: 1/1
```

### Expected
A test with a non-zero fault count, or a freeze/dead console while it ran, fails. The checks assert what the other machine shows (the guest's creature moved, the host sees it at the new place; the NPC's health in the game went down).

## Root cause
**What happens**

1. Faults are printed, not asserted (same pattern in all three scripts):
   ```python
   print('host faults:', host.call('exc')['out'].splitlines()[0])
   print('guest faults:', guest.call('exc')['out'].splitlines()[0])
   print('RESULT:', 'OK' if ok else 'FAIL')
   ```
   `twoinst.Checks.faults()` does `self.ok &= 'total faults: 0' in out[0]`, but these scripts do not use `twoinst.start`/`Checks`, so they also have no `Monitor` (no freeze or dead-console detection).
2. `lab.py` puts lines with ` faults: ` that are not `total faults: 0` into `r['faults']`, prints them under the test, but `ok` does not depend on them.
3. The checks are bookkeeping of the mod, against CLAUDE.md section 3 "Check what the other player sees":
   - `coop_two_instances.py`: a `puppet pos` line in `coop.status`, an entry in `coop.npc list`, the `hits_applied=` counter (and `hp1 is None` also passes), `coop.dna` values. The guest's click to walk is never checked to move anything.
   - `coop_mode.py`: `ok &= err < 5.0` (guest's view of the host within 5 m) but `moved` is only printed. If the host's click does not move it, the stale position matches and the test passes.

### Confidence
CONFIRMED by running (lab side, fake test) and CONFIRMED by reading (the scripts).

## Fix
Not fixed yet. Suggested in the review:

- In the three scripts use `twoinst.Checks` and call `ck.faults(host=host, guest=guest)` before `result()`; start `twoinst.watch(HOST, GUEST)` after the games are up so the monitor runs.
- In `coop_mode.py` assert `moved > 1.0` (or similar) before comparing positions; in `coop_two_instances.py` assert the guest's avatar position changed after the click and drop the `hp1 is None` escape or replace it with the game's health.
- As a safety net in `lab.py`: `ok = ok and not faults` (a line `<name> faults: total faults: N` with N > 0 fails the test).

## Verification
Not done yet. Planned test (must FAIL without the fix and PASS with it):

Lab self-test with the fake `faults` test above: FAIL with the fix, OK today. For the scripts: run `creature` once with a deliberate fault (a `call 0x1 cdecl` on a worker thread, as in `diag_check.py`) and see it fail.

### Related
CLAUDE.md section 3 ("Check what the other player sees, not our bookkeeping", "A crash after the checks is still a crash").

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #20.
- 2026-10-27: ticket opened from the issue.
