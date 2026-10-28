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
commits: 45acba3, 0d79bae, 2be1123, 3699255, 0a6d447
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
- 2026-10-28: fix/w2-world (wave 2) read the stopped WIP branch fix/save-hygiene (bea0f3e): it is only
  the interface `mods/coop/savestrip.h` (init, per-mode filters, a switch, a "saving" probe), no
  implementation, no hook, no filter, no test. Its design (hook the serializer's reference writer and
  write a null reference for our objects, so the world is never touched) is plausible but unverified:
  which function writes an object reference is not known (`Simulator::COMSerializer::Write`
  0x6A05F0 in march2017_dc04aee5.sym is a name only, signature and role not checked), and a null
  reference where the game expects an object may fault on load. Needs reverse engineering of
  SaveClassObjects 0x69FB90 first, then `coop_save_hygiene.py` (host saves with a guest puppet,
  restart, load: no creature of ours in the world). Not reached in wave 2 (budget went to the data
  loss tickets); left open, the WIP branch is not merged.
