---
id: BUG-0185
title: Space: an unvisited star's planets can still differ between the two games
status: open
kind: desync
stage: Space
reported: 2026-10-23
source: docs/known-issues.md (ticket backfilled 2026-10-27)
role: both
versions: 0.7.2.4 (main) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Copied from docs/known-issues.md, where the entry stays up to date:

* Each game generates the planets of a star the first time it is needed (a flight there, the jump);
  the generation draws from the game's global random generator, so host and guest made different
  planets for the same star (count and PlanetIDs, e.g. 4 and 6). Since fix/space-jump the generation of
  the generating thread draws from a private generator seeded by the star (docs/re/space.md 7.3): the
  same planets in 8 of 8 probe sessions and in most `coop_space_jump.py` runs, but once the host made 10
  planets of 0046101B and the guest 6 (lane 7, GOG 2016, 2026-10-23). Not found: which draw still
  differs (another generator object, `rand()`, or state such as star +0x0C = play minutes, set right
  before the generation, or the empire's state read by 0xC309F0). `coop_space_jump.py` prints "same" or
  "DIFFERENT planets" without failing. A sure fix: the host sends the planet records of a newly
  generated star (or the guest asks for them before entering).

## Root cause
See the report (observed vs inferred as marked there).

## Fix
Not fixed yet.

## Verification

## History
- 2026-10-23: first recorded in docs/known-issues.md.
- 2026-10-27: ticket backfilled from docs/known-issues.md (every entry carries its ticket id, CLAUDE.md 1).
- 2026-10-28: fix/w2-space: still open. Not reached in wave 2 (budget spent on the 7 other Space tickets). What is left: find the draw that still differs (or the sure fix from the report: the host sends the planet records of a newly generated star, a protocol feature). No new runtime data.
- 2026-10-28: fix/w3-space: still open. BUG-0123 (fixed there) is one cause that makes exactly this difference: with another thread inside GenerateRandomName during the generation the guest made 5 planets where the host made 6 and 4 where it made 1 (coop_space_seed_thread.py --without-fix); with the per-thread state both rounds matched. Whether the game itself ever names things off the main thread is not known yet: the mod now logs once (`space: GenerateRandomName runs on thread N, not on the main thread`). Not reached: the sure fix (the host sends a new star's planet records).
