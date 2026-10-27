---
id: BUG-0024
title: tools/fakepeer.py speaks protocol 6, the mod 7
status: fixed
kind: tooling
stage: All
reported: 2026-10-25
source: code review (tools comparison)
role: 
versions: 
fixed_in: unreleased
branch: fix/fakepeer-protocol
commits: 
tests: fakepeer_check
signature: 
docs: docs/06-testing.md
related:
---

## Report
Connection::looks_valid drops packets of another protocol version, so fakepeer is silently broken.

## Root cause
A hard-coded PROTO = 6.

## Fix
Default to protocol 7, retain protocol 6 through `--protocol 6`, and test interoperability against
the C++ session. Check the wire format before adding a future protocol version.

## Verification
`fakepeer_check.py` exercises the handshake, player state, chat, leave and build mismatch.

## History
- 2026-10-25: reported (code review (tools comparison)).
- 2026-10-25: ticket written from the CHANGELOG, commits and reports (backfill).
- 2026-10-27: fixed on fix/fakepeer-protocol, not released yet.
