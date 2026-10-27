---
id: BUG-0029
title: Session cleanup paths are incomplete
status: open
kind: bug
stage: All
reported: 2026-10-27
source: code review
role: 
versions: 
fixed_in: 
branch: 
commits: 
tests: 
signature: 
docs: 
related: BUG-0028
---

## Report
Code review found that voluntary leave and mod unload do not run all the session cleanup handlers
called by the disconnected callback. Voluntary leave omits the Tribe, creations, Civ and squadron
handlers; unload omits those handlers as well as Cell and Space cleanup. This is separate from the
Cell voluntary-leave fix in BUG-0028.

## Root cause
The cleanup calls are repeated separately in the disconnected, voluntary-leave and unload paths.

## Fix
Not implemented. Each omitted handler needs a lifetime and stage-safety check before it is added.

## Verification
Identified by comparing the cleanup calls in `setup_callbacks`, `do_leave`, and `on_unload` in
`mods/coop/coop.cpp`. Runtime effects and reproduction have not been verified.

## History
- 2026-10-27: recorded from code review; open.
