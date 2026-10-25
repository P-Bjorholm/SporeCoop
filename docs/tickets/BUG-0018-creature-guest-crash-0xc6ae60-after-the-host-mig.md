---
id: BUG-0018
title: Creature: guest crash 0xC6AE60 after the host migrated to a new nest
status: fixed
kind: crash
stage: Creature
reported: 2026-10-25
source: player report (Discord) + Spore Exception Report (guest, 18:55, 0.7.2.3)
role: guest
versions: 0.7.2.3
fixed_in: 0.7.2.4
branch: fix/migration-crash
commits: f690418, e2e4b9b
tests: migration
signature: dc04aee5:0xC6AE60 read 0x160 (Herd_NestPosition, null nest of a destroyed creature's herd)
docs: docs/re/creature.md 16
related:
---

## Report
"My friend crashed after I completed migration to a new nest."

## Root cause
The guest's game claims the new nest with CRG_ClaimNest using its own nest creatures; the mod's update filter destroyed them in the same frame; the cinematic's actor placement read their cleared herd.

## Fix
game::destroy_creature holds creatures while a cinematic runs and destroys them after it.

## Verification
coop_migration.py FAIL 4/4 without the fix, OK 3/3 with it.

## History
- 2026-10-25: reported (player report (Discord) + Spore Exception Report (guest, 18:55, 0.7.2.3)).
- 2026-10-25: fixed on fix/migration-crash, released in 0.7.2.4.
- 2026-10-25: ticket written from the CHANGELOG, commits and reports (backfill).
