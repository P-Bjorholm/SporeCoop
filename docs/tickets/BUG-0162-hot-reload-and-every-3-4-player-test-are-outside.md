---
id: BUG-0162
title: hot reload and every 3-4 player test are outside the lab suites, so they never run
status: open
kind: test
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#141
role: 
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: #141
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/141 (full text, discussion).

Three test scripts that cover advertised features are in no suite of `tools/tests/suite.py`, so `lab.py run smoke|full|nightly` never runs them:
- `hotreload_check.py`: hot reload of coop.dll, the documented dev loop `lab.py reload`.
- `coop_multi.py`: a 4-player chaos session with rejoins.
- `coop_npc_multi.py`: NPC sync with 4 players.

The two multi-player tests need a second lane (`SPORE_LANE_B`), and the lab gives each test exactly one lane, so there is no way to run them in the lab at all. As a result, no 3- or 4-player path is ever tested automatically, although the README advertises "2-4 players" and the stability report lists `coop_multi.py` as the 4-player test. (`coop_creature_social.py` is also not in a suite, but on purpose: docs/06-testing.md 6.24 and BUG-0021.)

### Where
- The suite list (no entry for these scripts): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/tests/suite.py#L22-L111
- `hotreload_check.py`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/tests/hotreload_check.py#L1-L3
- Second lane in coop_multi.py: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/tests/coop_multi.py#L1-L14 and https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/tests/coop_multi.py#L34
- Second lane in coop_npc_multi.py: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/tests/coop_npc_multi.py#L1-L14
- The lab runs a test with one `SPORE_LANE`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/lab.py#L99-L102
- The 2-4 player claim: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/README.md#L11 and https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/stability-report.md#L15
- The dev loop that hotreload_check covers: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/CLAUDE.md#L104

### How to reproduce
**How to reproduce / check**

Run the loop above, or `python3 -c "import sys; sys.path.insert(0,'tools/tests'); import suite; print(sorted(t['name'] for t in suite.T))"`.

### Expected
- hot reload runs at least nightly;
- the lab can run a test that needs two lanes;
- at least one 3- or 4-player scenario runs nightly.

## Root cause
**What happens**

```
$ for f in tools/tests/*.py; do grep -q "$(basename $f)" tools/tests/suite.py || echo $f; done
... tools/tests/coop_creature_social.py  tools/tests/coop_multi.py  tools/tests/coop_npc_multi.py
    tools/tests/hotreload_check.py  (plus helpers: twoinst, lane, fixtures, ...)
```

- `coop_multi.py` reads `SPORE_LANE_B` (default `SPORE_LANE + 1`) and starts the extra guests in that lane's bottle. The lab neither reserves nor passes a second lane, so even a hand-added suite entry would collide with the test running on lane N+1.
- Code that only runs with 3+ players (player ids 2-3, relays between guests, roster rows, "Jump to" with three players) has no automated test. Several open bugs live there (#50, #53, #54, #57, #42).
- `hotreload_check.py` uses `twoinst.start` and `Checks` and would run as is. It is simply missing, so a hot-reload regression goes unseen (#38).

### Confidence
Confirmed by reading. A second reviewer verified it, and noted that coop_creature_social is excluded on purpose.

## Fix
Not fixed yet. Suggested in the review:

1. Add `add('hotreload', P + 'hotreload_check.py', minutes=4)` (full).
2. Add a `lanes=2` property to `suite.add`. The scheduler reserves two free lanes and passes `SPORE_LANE_B`.
3. Add `coop_npc_multi.py --players 4` (nightly, short) and `coop_multi.py --players 4 --minutes 6` (nightly) with it.

## Verification
Not done yet. Planned test (must FAIL without the fix and PASS with it):

`lab.py run nightly` lists and runs `hotreload` and the two multi-player tests, on two lanes each for the latter.

### Related
#38 (hot reload crash), #50, #53, #54, #57, #42 (3+ player bugs).

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #141.
- 2026-10-27: ticket opened from the issue.
