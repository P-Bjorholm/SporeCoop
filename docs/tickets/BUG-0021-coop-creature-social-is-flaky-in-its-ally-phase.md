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
branch: 
commits: 
tests: coop_creature_social
signature: 
docs: docs/06-testing.md 6.24
related:
---

## Report
3 OK / 3 FAIL on 0.7.2.2, 0.7.2.3 and main: the Kooks end at 2 of 3 impressions.

## Root cause
The game refused one social game (refused=2): a test timing, not a regression.

## Fix
Planned: retry the social game in the action.

## Verification


## History
- 2026-10-25: reported (test runs).
- 2026-10-25: ticket written from the CHANGELOG, commits and reports (backfill).
