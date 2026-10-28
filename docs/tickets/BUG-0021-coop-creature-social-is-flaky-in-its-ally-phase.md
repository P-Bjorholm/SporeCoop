---
id: BUG-0021
title: coop_creature_social is flaky in its ally phase
status: fixed
kind: test
stage: Creature
reported: 2026-10-25
source: test runs
role: both
versions: 0.7.2.2+
fixed_in: unreleased
branch: fix/w4-tests
commits: 5a72494
tests: creature_social
signature: 
docs: docs/06-testing.md#testing-w4-tests
related:
---

## Report
3 OK / 3 FAIL on 0.7.2.2, 0.7.2.3 and main: the Kooks end at 2 of 3 impressions.

## Root cause
Fixed on fix/w4-tests (5a72494, test only): in the ally phase a game that ends without an impression
(the host's `refused` or nothing) is played again with the same Kook (up to 3 times, re-engaging it)
instead of moving on to the next one: the Kook walked out of the social range (host_act refuses an
act from more than kActRange + 4 m), it is not impressed or dead, so it can play again. Moving on ran
out of Kooks. The test is in suite.py now (`creature_social`, full).

## Fix
Fixed on fix/w4-tests (5a72494, test only): in the ally phase a game that ends without an impression
(the host's `refused` or nothing) is played again with the same Kook (up to 3 times, re-engaging it)
instead of moving on to the next one: the Kook walked out of the social range (host_act refuses an
act from more than kActRange + 4 m), it is not impressed or dead, so it can play again. Moving on ran
out of Kooks. The test is in suite.py now (`creature_social`, full).

## Verification
- PASS out/lab/20261028-030521 (ally (6, 3, 0) on both, reward paid once) and the repeat runs listed
  in the History.
- Root cause from the earlier runs' logs (refused=2 at 2 of 3 impressions), not reproduced on demand.

## History
- 2026-10-25: reported (test runs).
- 2026-10-25: ticket written from the CHANGELOG, commits and reports (backfill).
- 2026-10-28: fix/w4-tests: refused games are played again; the test is in the suite.
