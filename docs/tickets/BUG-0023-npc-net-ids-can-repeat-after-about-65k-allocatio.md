---
id: BUG-0023
title: NPC net ids can repeat after about 65k allocations in one world
status: open
kind: bug
stage: Creature
reported: 2026-10-25
source: code review (tools comparison)
role: both
versions: all
fixed_in: 
branch: dev/net-fuzz (WIP, stopped)
commits: 
tests: 
signature: 
docs: 
related:
---

## Report
npc.cpp host_tick allocates ids with g_next_id++ and wraps without checking g_host_by_id; creatures unseen for 2 s get new ids when they return.

## Root cause
Latent, not seen at runtime.

## Fix
Planned: an allocator that skips ids in use, with a unit test.

## Verification


## History
- 2026-10-25: reported (code review (tools comparison)).
- 2026-10-25: ticket written from the CHANGELOG, commits and reports (backfill).
