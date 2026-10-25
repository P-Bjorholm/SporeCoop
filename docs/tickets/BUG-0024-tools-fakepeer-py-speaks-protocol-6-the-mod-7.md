---
id: BUG-0024
title: tools/fakepeer.py speaks protocol 6, the mod 7
status: open
kind: tooling
stage: All
reported: 2026-10-25
source: code review (tools comparison)
role: 
versions: 
fixed_in: 
branch: dev/lab-gate (WIP, stopped)
commits: 
tests: 
signature: 
docs: docs/06-testing.md
related:
---

## Report
Connection::looks_valid drops packets of another protocol version, so fakepeer is silently broken.

## Root cause
A hard-coded PROTO = 6.

## Fix
Planned: read kProtoVersion from protocol.h; a harness self-test.

## Verification


## History
- 2026-10-25: reported (code review (tools comparison)).
- 2026-10-25: ticket written from the CHANGELOG, commits and reports (backfill).
