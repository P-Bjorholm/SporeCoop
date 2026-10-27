---
id: BUG-0138
title: a pasted host address with a leading space fails as "cannot resolve", and a wrong port is silently accepted
status: open
kind: bug
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#117
role: host
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: #117
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/117 (full text, discussion).

The Join field and `Endpoint::parse` accept or reject addresses in ways the player cannot understand:

- Only trailing spaces are trimmed. A leading space, which is typical when copying from a chat ("Friends connect to: 192.168.1.5:47810"), gives "Cannot join: cannot resolve  192.168.1.5:47810". So does pasting the whole hint line with two addresses.
- The port is read with `atoi` and cast to `uint16_t`. "1.2.3.4:" becomes port 0, "1.2.3.4:abc" becomes 0, and "1.2.3.4:99999" becomes 34463. No error is shown; the player waits 10 s for "no answer from host (timeout)".
- Host names are resolved with a blocking `getaddrinfo` on the game's main thread, inside the click handler. With slow or unreachable DNS, the game freezes for that time.

### Where
- Trimming only at the end: [coop_ui.cpp#L288-L306](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop_ui.cpp#L288-L306)
- Parsing and resolving: [net/udp.cpp#L54-L74](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/net/udp.cpp#L54-L74)
- Called from `Session::join`: [net/session.cpp#L47-L55](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/net/session.cpp#L47-L55)

### How to reproduce
- Paste `" 192.168.1.5:47810"` (leading space) into "Host address" and press Join: "Cannot join: cannot resolve ...".
- Type `192.168.1.5:99999` and press Join: "Connecting to 192.168.1.5:99999..." then a timeout after 10 s. The log shows `joining 192.168.1.5:34463`.
- Type a host name whose DNS server does not answer: the game stops drawing until `getaddrinfo` returns.

### Expected
- Whitespace (spaces, tabs, newlines) trimmed on both sides.
- A port outside 1..65535, or not a number, rejected with "the port must be a number from 1 to 65535".
- DNS resolution off the main thread (or numeric addresses only, with a clear message).

## Root cause
**What happens**

A host-side copy of `Endpoint::parse` (numeric addresses):

| Input | Result |
|---|---|
| `" 192.168.1.5:47810"` | fails, "cannot resolve" |
| `"192.168.1.5:47810"` | ok, port 47810 |
| `"192.168.1.5:"` | ok, **port 0** |
| `"192.168.1.5:99999"` | ok, **port 34463** |
| `"192.168.1.5:47810,  10.0.0.2:47810"` (the hint line) | fails, "cannot resolve" |

With a port of 0 or a wrapped port, the guest sends to the wrong port. The join then ends with "no answer from host (timeout)", which points the player to the host's firewall instead of their typo.

### Confidence
CONFIRMED, low severity: a host-side harness ran on the real `Endpoint::parse` logic. A second reviewer verified it.

## Fix
Not fixed yet. Suggested in the review:

- In `do_join` trim both ends with `isspace`. If the text contains `,` or a space inside, take the first `host:port` token.
- In `Endpoint::parse` use `strtoul` with an end-pointer check and range 1..65535. Report an error string instead of a bool.
- Resolve on a worker thread and finish the join from the main loop, or accept numeric IPv4 only via `inet_pton` and resolve names asynchronously.

## Verification
Not done yet. Planned test (must FAIL without the fix and PASS with it):

Host-side unit test in tests/net_tests.cpp:
- `Endpoint::parse(" 127.0.0.1:47810")` succeeds with port 47810;
- `parse("127.0.0.1:")`, `parse("127.0.0.1:99999")` and `parse("127.0.0.1:abc")` fail.

FAILS on main 563f72d, PASSES with the fix.

### Related
#8 (join timeout), #83 (the status keeps "Connecting to ..." after such a failed join).

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #117.
- 2026-10-27: ticket opened from the issue.
