# Tickets

Every player report, crash and bug found gets a ticket: one file `BUG-NNNN-<slug>.md` here, made with
`tools/tickets.py new`. The file says what was reported, the root cause, the fix (branch, commits,
version) and the test that catches it; its `## History` grows by dated lines. Commit messages of a
fix name the ticket (`BUG-0012`), so `tools/tickets.py log BUG-0012` finds them. Security problems
that are not fixed yet stay in `out/tickets/` (not in git) until the fix is released.

This table is written by `tools/tickets.py index` (checked by `make test`). Statuses: open, fixed,
not-reproduced, game-bug (Spore itself), wontfix, duplicate.

| Ticket | Status | Kind | Stage | Reported | Fixed in | Title |
|---|---|---|---|---|---|---|
| [BUG-0001](BUG-0001-cell-enemies-spin-on-the-guest-and-the-guest-can.md) | fixed | desync | Cell | 2026-10-24 | 0.7.2.2 | Cell: enemies spin on the guest and the guest cannot finish them |
| [BUG-0002](BUG-0002-cell-the-other-player-does-not-see-the-meat-of-a.md) | fixed | desync | Cell | 2026-10-24 | 0.7.2.2 | Cell: the other player does not see the meat of a kill |
| [BUG-0003](BUG-0003-join-the-guest-is-put-into-an-older-game-of-the.md) | fixed | bug | Menu | 2026-10-24 | 0.7.2.2 | Join: the guest is put into an older game of the host |
| [BUG-0004](BUG-0004-editors-desyncs-after-customizing-guest-crash-0x.md) | fixed | crash | Cell | 2026-10-24 | 0.7.2.2 | Editors: desyncs after customizing, guest crash 0xE83EC8, guest cannot call a mate |
| [BUG-0005](BUG-0005-crash-reports-the-minidump-can-be-empty-and-the.md) | fixed | bug | All | 2026-10-24 | 0.7.2.2 | Crash reports: the minidump can be empty and the archive is not made |
| [BUG-0006](BUG-0006-guest-crash-on-session-end-after-the-cell-stage.md) | fixed | crash | Creature | 2026-10-25 | 0.7.2.3 | Guest crash on session end after the Cell stage (coop modes::cell_at) |
| [BUG-0007](BUG-0007-host-crash-0xb88bea-a-herd-searching-land-around.md) | fixed | crash | Creature | 2026-10-25 | 0.7.2.3 | Host crash 0xB88BEA: a herd searching land around a home inside the planet |
| [BUG-0008](BUG-0008-host-crash-0xd48634-after-loading-a-save-these-g.md) | fixed | crash | Creature | 2026-10-25 | 0.7.2.3 | Host crash 0xD48634 after loading a save; "these guys appeared" |
| [BUG-0009](BUG-0009-creature-npcs-the-host-killed-stay-alive-on-the.md) | fixed | desync | Creature | 2026-10-25 | 0.7.2.3 | Creature: NPCs the host killed stay alive on the guest after an editor or a load |
| [BUG-0010](BUG-0010-creature-the-guest-does-not-see-a-companion-of-a.md) | fixed | desync | Creature | 2026-10-25 | 0.7.2.3 | Creature: the guest does not see a companion of a species only the host has |
| [BUG-0011](BUG-0011-creature-the-host-sees-the-guest-as-its-own-spec.md) | not-reproduced | desync | Creature | 2026-10-25 |  | Creature: the host sees the guest as its own species / not its changes |
| [BUG-0012](BUG-0012-crash-on-alt-shift-enter-the-game-s-next-display.md) | fixed | crash | All | 2026-10-25 | 0.7.2.4 | Crash on Alt+Shift+Enter (the game's next display adapter key) |
| [BUG-0013](BUG-0013-game-puts-me-underground-after-alt-enter.md) | not-reproduced | bug | Creature | 2026-10-25 |  | "Game puts me underground after Alt+Enter" |
| [BUG-0014](BUG-0014-creature-the-guest-stays-a-hatchling-after-a-dea.md) | fixed | bug | Creature | 2026-10-25 | 0.7.2.4 | Creature: the guest stays a hatchling after a death (egg loop, no control) |
| [BUG-0015](BUG-0015-creature-killed-enemies-stand-alive-at-0-health.md) | fixed | desync | Creature | 2026-10-25 | 0.7.2.4 | Creature: killed enemies stand alive at 0 health on the guest (hunt quest) |
| [BUG-0016](BUG-0016-creature-aggressive-creatures-ignore-the-partner.md) | fixed | bug | Creature | 2026-10-25 | 0.7.2.4 | Creature: aggressive creatures ignore the partner |
| [BUG-0017](BUG-0017-creature-the-guest-shows-another-species-at-the.md) | fixed | desync | Creature | 2026-10-25 | 0.7.2.4 | Creature: the guest shows another species at the host's nests ("Datzu") |
| [BUG-0018](BUG-0018-creature-guest-crash-0xc6ae60-after-the-host-mig.md) | fixed | crash | Creature | 2026-10-25 | 0.7.2.4 | Creature: guest crash 0xC6AE60 after the host migrated to a new nest |
| [BUG-0019](BUG-0019-creature-a-guest-that-gets-the-world-right-after.md) | open | bug | Creature | 2026-10-25 |  | Creature: a guest that gets the world right after the host's editor stays a hatchling |
| [BUG-0020](BUG-0020-our-objects-puppets-proxies-their-herds-are-writ.md) | open | bug | Creature | 2026-10-25 |  | Our objects (puppets, proxies, their herds) are written into saves |
| [BUG-0021](BUG-0021-coop-creature-social-is-flaky-in-its-ally-phase.md) | open | test | Creature | 2026-10-25 |  | coop_creature_social is flaky in its ally phase |
| [BUG-0022](BUG-0022-editor-strays-fails-once-under-a-full-lab-load.md) | open | test | Creature | 2026-10-25 |  | editor_strays fails once under a full lab load |
| [BUG-0023](BUG-0023-npc-net-ids-can-repeat-after-about-65k-allocatio.md) | open | bug | Creature | 2026-10-25 |  | NPC net ids can repeat after about 65k allocations in one world |
| [BUG-0024](BUG-0024-tools-fakepeer-py-speaks-protocol-6-the-mod-7.md) | open | tooling | All | 2026-10-25 |  | tools/fakepeer.py speaks protocol 6, the mod 7 |
| [BUG-0025](BUG-0025-spore-sh-kill-on-lane-1-could-kill-a-spore-the-o.md) | open | tooling | All | 2026-10-25 |  | spore.sh kill on lane 1 could kill a Spore the owner started |
| [BUG-0026](BUG-0026-creature-crash-after-dying-and-skipping-the-cuts.md) | not-reproduced | crash | Creature | 2026-10-24 |  | Creature: crash after dying and skipping the cutscene |
| [BUG-0027](BUG-0027-steam-build-no-co-op-button-steamstub-then-no-pr.md) | fixed | bug | Menu | 2026-10-24 | 0.7.2 | Steam build: no Co-op button (SteamStub, then no Present on Windows) |
