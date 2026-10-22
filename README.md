# SporeCoop

<!-- support:top:start -->
<p align="center">
  <a href="https://buymeacoffee.com/chamomilewoods"><img src="https://img.shields.io/badge/Buy%20me%20a%20coffee-chamomilewoods-FFDD00?style=for-the-badge&logo=buymeacoffee&logoColor=black" alt="Buy me a coffee"></a>
</p>
<p align="center"><b>☕ Like this project? <a href="https://buymeacoffee.com/chamomilewoods">Support the author on Buy Me a Coffee</a></b></p>
<!-- support:top:end -->

A co-op mod for **Spore + Galactic Adventures (GOG)** and **SporeKit**, the framework
that plugs the mod into the game. Everything is built on Linux (llvm-mingw) and
tested in Bottles (Wine).

| Component | What it is | Where |
|---|---|---|
| `steam_api.dll` (loader) | proxy DLL: the game loads it on its own, and it brings up SporeKit | `loader/` |
| `sporekit.dll` | mod runtime: symbols, hooks, frame, console, hot reload, overlay | `sporekit/` |
| `coop.dll` | the co-op mod itself | `mods/coop/` |
| `sporectl` | CLI for controlling the running game (tests, RE, screenshots) | `tools/sporectl.py` |
| `spore.sh` | deploy/launch in Bottles, profiles, fixtures, logs | `tools/spore.sh` |
| `addrport` | ports ModAPI addresses to this exe, builds "windowed" exes for REA | `tools/addrport/` |
| `fakepeer.py` | fake network player for tests without a second instance | `tools/fakepeer.py` |
| `package.sh` | zip for installing on a friend's machine (Windows/Wine) | `tools/package.sh` |

Documentation lives in [`docs/`](docs/README.md).

## Status (2026-10-22)

| Milestone | Status |
|---|---|
| M0 Infrastructure | ✅ loader, SporeKit, devkit console, screenshots, input, hot reload, exception logger, watchdog thread, profiles and 2 instances |
| M1 "Ghosts" | ✅ host/join, other players' puppets walk with animation, nicknames above them, chat, host world transfer to the guest, animation replay (jump etc.) |
| M2 "Pack" | ✅ host NPCs are mirrored on the guest (the guest's own NPCs are removed), guest hits are resolved by the host, NPC damage reaches the guest, shared time of day; ⏳ social interactions with NPCs, eggs/nests/fruit |
| M3 "Shared progress" | ✅ shared DNA (host holds the total); a dead player's puppet is hidden until respawn; ⏳ body parts, editor |
| M4 "Convenience" | ✅ the world is transferred and loaded on the guest automatically (from the menu), the host saves before the transfer; the guest plays its own creature (M8) |
| M5 "All modes" | ✅ Cell, Creature, Tribe, Civilization, Space, Adventures: see [docs/07-modes.md](docs/07-modes.md) and the table below |
| M6 "Interface" | ✅ "Co-op" entry in Spore's main menu, native connection window, lobby (players, stage, ping, readiness, chat), LAN game discovery, F9 in-game, notifications as slide-out panels with icons: [docs/08-ui.md](docs/08-ui.md) |
| M7 "Together" | ✅ one world across all phases: Cell "Symbiosis" (knockout, revival by a partner, respawn as a small cell), Creature "Pack", Tribe "Chiefs", Civilization "Governors", Space "Squadron", voting on evolution: [docs/09-coop-design.md](docs/09-coop-design.md) |
| M8 "Own creations" | ✅ everyone has their own creature and cell (from "My Creations"), visible to all, chosen in the F9 window, each evolves in their own editor; ⏳ "done" exit from the guest's editor, ship: [docs/09-coop-design.md](docs/09-coop-design.md) §9.4.1 |

| Mode | What others see | Shared |
|---|---|---|
| Cell | the player's cell with its model, at the exact position and rotation; knockout is visible to everyone | tide pool (NPCs, food), food progress |
| Creature | creature in the pack, animations | DNA, NPCs and damage via the host, time of day |
| Tribe / Civilization | a nickname pointer where the player is working with the mouse, `coop ping` | tribe food / money |
| Space | the player's ship in the same system or on the planet | empire money |
| Adventures | captain in the same adventure | — |

## Quick start (development)

```bash
make && make test                 # build everything + network unit tests on Linux
tools/spore.sh install            # once: install the steam_api.dll proxy into the game
tools/spore.sh deploy             # copy fresh DLLs/symbols/configs into the game
tools/spore.sh run -p test        # run the game in Bottles with the test profile (windowed, separate saves)
tools/sporectl.py info            # check that SporeKit is alive
tools/sporectl.py shot            # screenshot to out/shots/
make coop && tools/spore.sh deploy && tools/sporectl.py mod.reload coop   # hot reload
python3 tools/tests/coop_two_instances.py                                # full test: 2 game instances (Creature)
python3 tools/tests/coop_mode.py --fixture space --planet Game0/Maraya     # test any mode from a fixture
python3 tools/tests/coop_ui.py                                           # UI test: menu → lobby → game
```

## How to play

1. Host: main menu → **Co-op** → name → **Host**, then load a save or start a game.
   The window shows the addresses to give to your friends.
2. Guest: main menu → **Co-op** → name → click a game in the "Games on your network" list
   or enter the host's address → **Join**. The host's world arrives and loads automatically, and the guest
   appears nearby (in Space the host must be off-planet so the game allows saving).
3. In-game, **F9** opens the lobby: players, stage, ping, readiness, chat, "Send my world", "Leave".

The same is available in the cheat console (`Ctrl+Shift+C`): `coop name Alice`, `coop host`,
`coop join <ip>`, `coop status`, `coop say <text>`, `coop ping`, `coop leave`.
In Adventures, each player launches the same adventure themselves. Over the internet, UDP port 47810
on the host must be reachable (port forwarding on the router, or a VPN such as ZeroTier / Radmin VPN / Tailscale).

<!-- support:bottom:start -->
## ☕ Support the author

If this project is useful to you, you can support the author — it really helps to keep working on it:

**https://buymeacoffee.com/chamomilewoods**

<a href="https://buymeacoffee.com/chamomilewoods"><img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" alt="Buy Me a Coffee" height="50"></a>
<!-- support:bottom:end -->
