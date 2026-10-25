---
id: BUG-0019
title: Creature: a guest that gets the world right after the host's editor stays a hatchling
status: open
kind: bug
stage: Creature
reported: 2026-10-25
source: test (coop_migration)
role: guest
versions: 0.7.2.4
fixed_in: 
branch: 
commits: 
tests: migration
signature: 
docs: docs/known-issues.md
related:
---

## Report
The world sent right after an evolution that starts a migration is saved during CRG_SpeciesMigrate; the guest's creature stays at age 0 and never gets the Migrate goal.

## Root cause
Open.

## Fix
None yet.

## Verification
Seen 6 times on lane 6; the test saves and shares again after the caption to work around it.

## History
- 2026-10-25: reported (test (coop_migration)).
- 2026-10-25: ticket written from the CHANGELOG, commits and reports (backfill).
