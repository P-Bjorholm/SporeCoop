---
id: BUG-0204
title: joining by host name resolves DNS on the game's main thread: a slow name server freezes the game
status: open
kind: freeze
stage: Menu
reported: 2026-10-28
source: split from BUG-0138 (fix/w3-net)
role: guest
versions: 0.7.2.4 and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0138
---

## Report
Third point of BUG-0138 (GitHub #117), not fixed there. A guest that types a host *name* (not an
IP address) and presses Join waits for `getaddrinfo` inside the click handler, on the game's main
thread: with a slow or unreachable name server the game stops drawing until the resolver gives up
(seconds, up to the resolver's own timeout).

How to reproduce: point the system at a name server that does not answer, type `somehost:47810`
into "Host address", press Join: the frames stop until "Cannot join: cannot resolve somehost".

## Root cause
`Session::join` -> `Endpoint::parse` (net/udp.cpp) calls `getaddrinfo` synchronously. Since
fix/w3-net a numeric address is parsed with `AI_NUMERICHOST` first and never waits; only names
still block. Inferred from the code, not run with a dead name server.

## Fix
Not fixed. Suggested: resolve names on a worker thread and finish the join from the main loop (the
window shows "Resolving <name>..."), or accept numeric addresses only with a clear message.

## Verification
<!-- the test: FAIL without the fix (run dir), PASS with it (runs) -->

## History
- 2026-10-28: opened.
