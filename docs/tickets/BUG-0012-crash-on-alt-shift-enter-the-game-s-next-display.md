---
id: BUG-0012
title: Crash on Alt+Shift+Enter (the game's next display adapter key)
status: fixed
kind: crash
stage: All
reported: 2026-10-25
source: Spore Exception Reports (host, 18:37 / 18:40, 0.7.2.3)
role: host
versions: all
fixed_in: 0.7.2.4
branch: fix/alt-enter
commits: ab11049
tests: alt_enter
signature: dc04aee5:0x7777BE read 0xFFFE0310 (ReleaseShaders on a duplicate list entry)
docs: docs/re/graphics.md
related:
---

## Report
The host crashed when switching to fullscreen.

## Root cause
Spore's own bug: Alt+Shift+Enter moves the device to the next adapter on multi-monitor PCs; ReleaseShaders visits duplicate shader entries and calls through bytecode. Also without SporeCoop.

## Fix
gamefixes.cpp: the next-adapter action toggles fullscreen instead.

## Verification
coop_alt_enter.py FAIL with the hook off (fault at 0x7777BE), OK 2/2.

## History
- 2026-10-25: reported (Spore Exception Reports (host, 18:37 / 18:40, 0.7.2.3)).
- 2026-10-25: fixed on fix/alt-enter, released in 0.7.2.4.
- 2026-10-25: ticket written from the CHANGELOG, commits and reports (backfill).
