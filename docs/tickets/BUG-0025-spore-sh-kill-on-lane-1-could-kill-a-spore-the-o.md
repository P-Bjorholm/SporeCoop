---
id: BUG-0025
title: spore.sh kill on lane 1 could kill a Spore the owner started
status: open
kind: tooling
stage: All
reported: 2026-10-25
source: code review (playbook comparison)
role: 
versions: 
fixed_in: 
branch: dev/lab-gate (WIP, stopped)
commits: 
tests: 
signature: 
docs: 
related:
---

## Report
kill uses pkill -f on the lane folder; on lane 1 that is the real install.

## Root cause
Not pid-scoped.

## Fix
Planned: pidfiles per run, kill only those PIDs.

## Verification


## History
- 2026-10-25: reported (code review (playbook comparison)).
- 2026-10-25: ticket written from the CHANGELOG, commits and reports (backfill).
