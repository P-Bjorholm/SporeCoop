---
id: BUG-0033
title: leaving, re-hosting and hot reload each clean up a different subset of the co-op state
status: open
kind: bug
stage: All
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#11
role: host
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: #11
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/11 (full text, discussion).

A co-op session can end in five ways: the guest times out or is rejected, the player presses Leave or Stop hosting, `coop.host`/`coop.join` runs while a session is open, or the mod is hot reloaded. Each way cleans up a different subset of the stage modules. Mirrors, invulnerability flags and per-player maps survive the paths that skip a module and leak into the next session or the player's world. Severity: wrong behaviour, with knock-on crashes and save pollution in the per-stage symptoms below.

### Where
- Disconnected callback (the full list): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1491-L1507
- `do_leave`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1761-L1771
- `do_host` / `do_join`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1741-L1759
- `on_unload`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L2603-L2616
- `Session::leave` never calls `cb.disconnected`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/net/session.cpp#L77-L101 (and `host()` / `join()` call `leave("restart")` first: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/net/session.cpp#L34-L49)

### How to reproduce
1. Two instances, Space stage (or Cell): host and guest together, the guest sees the host's ships mirrored.
2. On the guest: `coop.leave`. `coop.fleet list` still shows the mirror ships; they stay in the world and fly with their own AI.
3. Variant: on a guest, `coop host 47900` while connected. `coop.status` still lists `remote 0 'Host' ...` and, after 5 s, the feed says "cannot share the world: ..." for an id that does not exist.
4. Variant: `mod.reload coop` in Space or Cell with a partner: the old mirrors stay untracked in the world.

### Expected
Every way a session ends runs the same cleanup. Starting a new session always starts from a clean state.

## Root cause
**What happens**

| module `on_session_end` | disconnected callback | `do_leave` | `on_unload` | `do_host`/`do_join` while in a session |
|---|---|---|---|---|
| npc | yes | yes | yes | no |
| social | yes | yes | yes | no |
| tribe | yes | **no** | **no** | no |
| cellsync | yes | **no** | **no** | no |
| creations | yes | **no** | **no** | no |
| civ | yes | **no** | **no** | no |
| squadron | yes | **no** | **no** | no |
| spacecoop | yes | yes | **no** | no |
| despawn + clear `g_remotes` | yes | yes | yes | **no** |

1. `Session::leave()` only closes the socket. It never fires `cb.disconnected`, so `do_leave` and `on_unload` repeat part of the callback's list by hand, and each repeats a different part.
2. `Session::host()` and `Session::join()` call `leave("restart")` first. Typing `coop host` / `coop join` (or using the cheat console) while a session is open therefore runs no cleanup at all. The old `g_remotes`, with their puppets and stale snapshot buffers, carry over into the new session. `follow_phase` then evaluates the stale `buf.latest()` of the old ids and calls `world::share_with(old id)` for players who no longer exist. A new player who gets a reused id inherits that `Remote` (`needs_world`, `world_sent`, `joined_phase`).
3. Each stage module ticks only while `connected()`: `cellsync::tick`, `squadron::tick`, `tribe::active()`, `civ::active()`. So whatever its `on_session_end` would have removed stays as it is after a `do_leave`.
4. Module state that `on_session_end` does not cover is never reset on any path: `g_vote` (separate issue), `shared`'s `have`/`synced` (separate issue), `g_roles`, `g_host_away`.

Per-stage symptoms have or get their own issues:
- Cell: the host's cells stay as AI-locked mirrors in the guest's pool after the guest's own Leave. This is in docs/known-issues.md ("a guest's own coop.leave keeps its mirrors") and is being fixed in PR WoodyWizard/SporeCoop#5 (BUG-0028). A guest that leaves while knocked out also keeps `kInvuln = 1` and the knock-out opacity, because `cellsync::tick` returns while not connected.
- Space: mirror UFOs stay as live NPC ships after Leave or hot reload, and can be saved (other reviewers open this).
- Tribe: chiefs and per-member maps stay (other reviewers open this).

### Confidence
CONFIRMED by reading the full path: the table is taken straight from the five functions above, and every module tick bails out while not connected. The exact visible effect in Tribe and Civ after a `do_leave` is not verified in game.

## Fix
Not fixed yet. Suggested in the review:

One `end_session(const char* why)` in coop.cpp that runs every module's `on_session_end`, despawns and clears `g_remotes`, resets the lobby, the vote, `shared::on_session_change()`, `g_roles` and `g_host_away`/`g_solo_dna`. Call it from:
- `cb.disconnected`
- `do_leave` (after `g_sess.leave`)
- `on_unload` (after `g_sess.leave`)
- `do_host` / `do_join` when `g_sess.role() != None` before the restart

Keep the old `do_leave` behaviour behind `coop.fix session_end off` so the lab can check that the test still catches it.

## Verification
Not done yet. Planned test (must FAIL without the fix and PASS with it):

`tools/tests/coop_leave_cleanup.py` on two instances (twoinst.start), one round per stage fixture (cell, tribe, civ, space):
- The guest presses Leave through the UI (the real button, not a command). Assert the game's own state on that machine: no object of the host's left in the world (`coop.fleet list` / `coop.pool` / `coop.tribe`), the avatar's invulnerable flag is 0 after leaving while knocked out (Cell), and `coop.status` shows no remotes.
- Repeat with `coop host` while connected.

The test must fail on main and with `coop.fix session_end off`, and pass with the fix.

### Related
- PR WoodyWizard/SporeCoop#5 (BUG-0028, Cell part)
- BUG-0006 (a stale cell pool at session end)
- docs/known-issues.md "Cell: a guest's own coop.leave keeps its mirrors as passive cells"
- The issues on the stale vote, `shared` resources and reused player ids are the same pattern.

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #11.
- 2026-10-27: ticket opened from the issue.
