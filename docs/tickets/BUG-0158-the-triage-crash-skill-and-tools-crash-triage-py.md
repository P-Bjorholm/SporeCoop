---
id: BUG-0158
title: the triage-crash skill and tools/crash_triage.py are referenced but do not exist
status: open
kind: bug
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#137
role: 
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: #137
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/137 (full text, discussion).

CLAUDE.md section 5 lists three agent skills, including `triage-crash` ("turn a crash report into a build, a signature and an action"). Only two exist in `.claude/skills/`: `fix-player-report` and `merge-batch`. The fix-player-report skill and the agent brief template in docs/10-dev-pipeline.md also refer to `tools/crash_triage.py`, which is not in the repository. An agent following the documented steps for a crash report finds neither.

### Where
- CLAUDE.md skill list: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/CLAUDE.md#L133-L136
- fix-player-report step 1: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/.claude/skills/fix-player-report/SKILL.md#L12-L15
- Agent brief template: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/10-dev-pipeline.md#L143
- The skills that exist: https://github.com/WoodyWizard/SporeCoop/tree/563f72d98cce2acf2c44b859f1dfe32f9588d401/.claude/skills

### How to reproduce
**How to reproduce / check**

`ls .claude/skills tools/crash_triage.py` on main.

### Expected
The documented skill and tool exist, or the docs list only what exists.

## Root cause
**What happens**

- The claims:
  - CLAUDE.md: "`.claude/skills/`: `fix-player-report` (...), `triage-crash` (turn a crash report into a build, a signature and an action), `merge-batch` (...)".
  - SKILL.md: "**Identify**: `tools/tickets.py find <address | symptom>` and (when it exists) `python3 -I tools/crash_triage.py <file>`".
  - 10-dev-pipeline.md brief: "Context: <known-issues anchor, docs/re anchor, crash_triage output>".
- The reality: `ls .claude/skills` shows `fix-player-report` and `merge-batch` only, and `tools/crash_triage.py` does not exist. The skill hedges ("when it exists"); CLAUDE.md and the brief template do not.

### Confidence
Confirmed by listing the tree. A second reviewer verified it.

## Fix
Not fixed yet. Suggested in the review:

- Either add `.claude/skills/triage-crash/SKILL.md` and `tools/crash_triage.py` (build from the exe hash in the report, signature `<build>:<address>`, `tickets.py find` on it), or remove `triage-crash` from CLAUDE.md section 5 and the `crash_triage` references from the skill and the brief.
- Have `tools/docs_lint.py` check that `tools/...` paths and skill names named in CLAUDE.md, skills and docs exist.

## Verification
Not done yet. Planned test (must FAIL without the fix and PASS with it):

`make test` fails while a referenced tool or skill is missing.

### Related
None of #7 to #82 covers this.

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #137.
- 2026-10-27: ticket opened from the issue.
