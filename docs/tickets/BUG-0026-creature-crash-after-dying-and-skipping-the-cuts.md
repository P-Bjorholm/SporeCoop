---
id: BUG-0026
title: Creature: crash after dying and skipping the cutscene
status: not-reproduced
kind: crash
stage: Creature
reported: 2026-10-24
source: player report (Reddit)
role: 
versions: 0.7.2
fixed_in: 
branch: 
commits: 
tests: creature_death
signature: 
docs: docs/known-issues.md
related:
---

## Report
"Every time I die and skip the cutscene my game crashes."

## Root cause
Not reproduced in 16 trials (death, caption, skip).

## Fix
None.

## Verification
coop_creature_death.py covers the flow.

## History
- 2026-10-24: reported (player report (Reddit)).
- 2026-10-25: not reproduced, see Root cause.
- 2026-10-25: ticket written from the CHANGELOG, commits and reports (backfill).
