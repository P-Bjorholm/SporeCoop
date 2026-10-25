---
id: BUG-0020
title: Our objects (puppets, proxies, their herds) are written into saves
status: open
kind: bug
stage: Creature
reported: 2026-10-25
source: analysis (BUG-0008)
role: host
versions: all
fixed_in: 
branch: fix/save-hygiene (WIP, stopped)
commits: 
tests: 
signature: 
docs: docs/known-issues.md, docs/re/creature.md 12
related:
---

## Report
Every host save keeps the partners' puppets and the herds made for them; they are only removed after a load (BUG-0008).

## Root cause
Nothing keeps our objects out of the serializer (COMSerializer::SaveClassObjects 0x69FB90).

## Fix
Planned: hook the save path, take our objects out for the write, put them back.

## Verification
Planned test coop_save_hygiene.py.

## History
- 2026-10-25: reported (analysis (BUG-0008)).
- 2026-10-25: ticket written from the CHANGELOG, commits and reports (backfill).
