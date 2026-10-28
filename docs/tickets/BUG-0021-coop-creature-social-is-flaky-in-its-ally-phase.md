---
id: BUG-0021
title: coop_creature_social is flaky in its ally phase
status: open
kind: test
stage: Creature
reported: 2026-10-25
source: test runs
role: both
versions: 0.7.2.2+
fixed_in: 
branch: fix/w4-tests
commits: 5a72494
tests: 
signature: 
docs: docs/06-testing.md#testing-w4-tests
related:
---

## Report
3 OK / 3 FAIL on 0.7.2.2, 0.7.2.3 and main: the Kooks end at 2 of 3 impressions.

## Root cause
Not settled. Seen (2026-10-28): besides refusals, Kooks die during the ally phase and the species'
impressed count falls behind the host's `impressed` counter. Hypothesis (not checked): `engage()`
clicks the social stance button on every call, a second click may leave the social stance, and the
next key 1 is then a bite (a Kook killed, impressions lost). Next: print the stance before each key
press (or only click the button when the stance is not social) and log Kook deaths
(`coop.npc list` DEAD, attacker).

## Fix
Not fixed. fix/w4-tests (5a72494) plays a refused game again with the same Kook (it walked out of the
range: host_act refuses acts from more than kActRange + 4 m) instead of moving on. Not enough: 1 OK of
4 runs.

## Verification
- out/lab/20261028-030521: OK (ally (6, 3, 0)).
- out/lab/20261028-033546 (--repeat 3): 3 FAIL. The retry ran ("n19: no impression ... played
  again"), then: run 1 ended at (4, 2, 0) while the host's `impressed` counter said 3; runs 2 and 3
  ended at (1, 0, 1): a Kook had died during the ally phase (before the test's own kill, which is
  of another species) and the species' impressed count was 0 again.

## History
- 2026-10-25: reported (test runs).
- 2026-10-25: ticket written from the CHANGELOG, commits and reports (backfill).
- 2026-10-28: fix/w4-tests: refused games are played again; still 1 of 4 OK (Kooks die during the ally phase); not in the suite; left open.
