# Changelog

## [Unreleased]

### Fixed
- **Creature: no more crash of the host after an evolution, and no copies of the partner's
  creature left in the world** (player report, host crash at 0xB88BEA). After the editor the
  partner's old creature stayed on the planet as a wild creature next to the new one, and the
  herd made for the partner's creature could get its home deep inside the planet: when that
  creature looked for land around its home the game read past its map and crashed. The old
  creature is now removed when the players are back, a partner who comes back into the world
  appears where it is (not between its last position and the planet's centre), and the herd of
  a partner's creature stays under it. Test: `coop_editor_strays.py` (suite `editor_strays`).
- **Creature: no crash when you attack or kill the creatures a save brought back** (player report,
  host crash at 0xD48634, three times). A save made while the partner played kept the partner's
  creature; loaded again after a restart it was a wild creature ("these guys appeared after I
  loaded save"), and attacking it crashed the game. Such creatures are now removed when a world
  loads (also from saves you already have), the game no longer crashes on any creature that lacks
  that data, and the partner's creature no longer leaves a new herd in the world (and in every
  save) each time it appears. Test: `coop_creature_e84.py` (suite `creature_e84`).

## [0.7.2.2] - 2026-10-25

Fixes from a player report (two brothers playing the Cell and the Creature stage). 0.7.2.2 plays
together with 0.7.x (the network protocol did not change), but the fixes need both players on
0.7.2.2: host and guest share the work of kills, meat and editors.

### Fixed
- **Cell: the host's creatures no longer spin on the guest's screen, and the guest can kill them.**
  A creature now faces the same way for both players (it kept turning on the guest, between the
  host and the guest), so its mouth and spikes are where they are for the host. The guest's
  killing bite and bites with a mouth that eats chunks now reach the host (before, the guest could
  wound a creature but never finish it). Test: `coop_cell_spin.py` (suite `cell_spin`).
- **Cell: both players see the meat of a kill, and a chunk is eaten once.** When the guest killed a
  creature, its meat appeared only on the guest's screen for a moment and the host's creature just
  vanished; now the kill happens on the host with the game's own death and meat, which both see. A
  creature the host kills also dies on the guest's screen (before it stayed there as if alive). Food
  from a chunk both eat at the same moment counts once. Test: `coop_cell_meat.py` (suite `cell_meat`).
- **Joining sends the game the host plays now:** a guest that joined a host who had started a new game
  could be put into an older game of the host (the host had to delete it). Every game of a galaxy is
  in the same save folder, and a planet named with letters outside English (for example in Cyrillic)
  was not found, so another planet of that folder went out. Planet names in any letters now work on
  both sides, the host always names the planet it plays (saving again if its file is not written
  yet), and the guest never loads another planet instead.
- **Editors in the Cell and the Creature stage** (player report): a guest can call a mate in the Cell
  stage and evolve its own cell (its mate was removed by the pool sync at once); after the host's
  editor the guest no longer crashes and sees the host's new cell or creature right away (a new
  creation was sent once without its records: the guest's game faulted building the host's cell, or
  showed its own body for the host until the next send); the host's mating call starts one vote,
  not a second one that ran on in the editor; a rejoin applies the creations again (writing their
  files failed while the game had them open); after the own cell editor the new cell stays the own
  one; the guest's own creature editor after the vote opens once its world has loaded (opened right
  after the start-of-Creature world arrived, it came up without parts or buttons and the guest was
  stuck in it). Test: `coop_editor_desync.py` (suite `editor_desync`).
- **Crash reports:** the minidump of a crash came out empty on some systems and then the archive was
  not made; now the report, the archive and the mark for the next start come first, the minidump
  (of the crashed thread) last, and the archive is made again with it.

## [0.7.2.1] - 2026-10-24

Finding problems: crash reports ready to send, kept logs, the version in the game. 0.7.2.1 plays
together with 0.7.0-0.7.2 (the network protocol did not change).

### Finding problems
- **Crash reports, ready to send:** when the game crashes, freezes for 30 s or closes right after an
  error, SporeCoop packs `mods\logs\SporeCoop-report-<date-time>.zip` by itself (the crash report:
  what crashed, where, the call chain, the co-op session, other overlays and mods; a minidump; the
  logs; the settings), and the next start of the game tells the player its name.
- **Logs are kept:** the logs of the last 10 runs stay in `mods\logs\previous\` (restarting the game
  after a crash used to overwrite the crash's log).
- **"Report a problem"** in the co-op window (F9) packs everything into one file to send,
  `mods\logs\SporeCoop-report-<date-time>.zip` (the report, this run's and the last runs' logs, the
  newest crash reports with minidumps, the mod settings with the last joined address hidden), and
  opens its folder with the file selected.
- **The version in the game:** under the Co-op item of the main menu and at the bottom of the co-op
  window (with the game build: GOG 2024, Steam, ...).
- The log starts with the environment: Windows or Wine, memory and the 4GB flag, the graphics card,
  and other programs that hook the game (Steam / Discord / NVIDIA overlays, RivaTuner, ReShade, ModAPI).
- A fault inside SporeCoop's own code is told in the game once, with where the report is.
- SporeKit 0.3.4: `env`, `report`, `crash.test` console commands; mod API `kit_version`, `build_name`,
  `diag_report`, `diag_context`, `logs_dir`, event `SK_EV_FAULT` (docs/02-modkit.md 2.7).
- Tests: `diag_check.py` (suite `diag`), `coop_creature_death.py` (dying alone and skipping the
  respawn cutscene, a player report that did not reproduce) with the `coop.team die` test command.

## [0.7.2] - 2026-10-24

**No Co-op item on Windows with the Steam overlay** ([issue #4](https://github.com/WoodyWizard/SporeCoop/issues/4)).
The mod loaded, but after the menu came up SporeKit got no frames from the game: another overlay
(most likely the Steam overlay while Steam is online) took over the game's `Present` call, which
is where SporeKit's frame begins. Everything the mod does each frame stopped: no Co-op item in the
menu, F9 did nothing, no network. The log shows it as `WATCHDOG (no frames, ...)` while the game
still draws.

- SporeKit 0.3.3 has a second frame source: the game loop (`App::Canvas::Tick`). When `Present`
  has not reached SporeKit for 2 s while the game window has the focus and is not minimized, the
  frames come from the game loop, and back from `Present` when it returns. The log says
  `frames: Present has not reached SporeKit ...`. In that mode the mod's on-screen labels (names
  above players, notes drawn over the picture) are off; the menu item, the co-op window, F9 and
  the whole co-op work.
- Tested on Windows 11 with the Steam version (offline mode) and `[sporekit] frames=loop`, which
  ignores `Present` the way such an overlay does: the Co-op item appears, F9 opens the window, and a
  Steam guest in that mode played with a GOG 2024 host in the Creature stage (world transfer,
  puppets, NPC damage through the host, shared DNA), no faults. In the normal mode loads of saves
  (Creature, Space) and stage changes never switched to the game loop. Not tested with the Steam
  overlay online (the case of the issue): please report how it goes.
- First test of the Steam version on Windows 11 (0.7.1 and 0.7.2): every feature on, Steam and GOG
  players together.
- `sporekit.ini`: `background_mode` was under `[mods]` in the template (no effect when uncommented);
  new `frames=auto|loop`. Console: `frames.source [auto|loop]`; `info` names the frame source.
- `tools/tests/coop_frames_loop.py` (smoke): co-op with the guest's frames from the game loop.

## [0.7.1] - 2026-10-24

**The Steam version works.** Steam's `SporeApp.exe` (since its October 2024 update) is the GOG 2024
game wrapped in Steam's DRM: the game code is encrypted in the file and unpacked when the game
starts, so SporeCoop found no addresses for it and turned co-op off ("no symbol database for this
SporeApp.exe build", [issue #4](https://github.com/WoodyWizard/SporeCoop/issues/4)). Now SporeKit
recognises the exe by its data, waits for the unpacked code, checks that it is exactly the GOG 2024
code and then uses the GOG 2024 addresses. Steam and GOG players can play together.

- Tested on Linux with Steam (offline mode) and Proton: every feature on, a Steam guest joined a GOG
  2024 host in the Creature stage (world transfer, own creature, the host's NPCs), no faults. Not
  tested yet on Windows.
- Install as for GOG: into the Steam game's `SporebinEP1` (`SporeCoopSetup.exe` and `install.bat`
  now name the Steam build). 0.7.0 and 0.7.1 can play together (the network protocol did not change).
- SporeKit 0.3.2: `data_sha256` / `text_sha256` in symbol files, the late boot for packed exes,
  `[sporekit] defer_symbols=1` for tests; `tools/exe_info.py` prints both hashes.

## [0.7.0] - 2026-10-23

"A galaxy together": the Space stage becomes a game of two (to four) rulers of one galaxy who see
each other, help each other, trade and finish the game together. Everything is on the new
**Space** page of the co-op window (F9 in the Space stage, the "Space >" button).

**0.7 cannot play with 0.6.x** (the network protocol changed): every player needs 0.7.0.

**0.7.x is the last version for the old GOG 2016 build** (`SporeApp.exe` `922af7c7…`). From 0.8 only
the current GOG build (the 2024 update, `dc04aee5…`, the code of the March 2017 patch that Spore
ModAPI also targets) is supported: update the game through GOG.

### Space: new
- **Partners on the galaxy map**: a name tag in the partner's colour on their star (or on their
  ship while they fly in the galaxy view), an arrow at the screen edge when they are out of view;
  a click on the tag flies you there. The Space page lists where everybody is ("Bob: at Fibucius
  [Fly there]").
- **Your own territories**: the stars with a partner's own colonies get a ring in the partner's
  colour, and the Space page lists them one by one with "Fly there".
- **Partner colonies as an allied empire**: a partner's colonies founded after you joined exist in
  your galaxy too, as the cities of "Alice's empire", an ally you have already met. You can fly
  there, land at their city, and fight off a raid on that colony in your own game: winning it
  counts as defended in the owner's game. The mirrored empire never reaches your save.
- **Tool gifts**: give charges of consumable tools (bombs, terraforming charges, the colony tool,
  monoliths, colony boosters and turrets, fireworks ...) to a partner. Permanent tools (weapons,
  drives) stay with their owner.
- **Trade between players**: at a star with a partner's colony, sell spice to it for twice the
  market value (an export bonus paid by the galaxy into the shared budget, up to 30 units per
  colony every 10 minutes); the spice arrives in the partner's cargo.
- **Co-op contracts**: every 10 minutes the host's game offers a job made for two: a **pirate
  armada** that attacks one of the host's colonies only while two ships of the crew are there
  (two waves of tougher raiders), or a **joint survey** of 2-3 nearby stars that the whole crew
  has to visit together. Money and a tool for everybody in the crew.
- **Shared diplomacy**: the players are one people for the NPC empires. A first contact, a gift,
  a mission, a trade route, an attack, an alliance, a war or a peace by one player changes every
  player's galaxy the same way, with a notice. The host's relation table keeps all copies equal.
- **The galactic core together**: partners are told when someone reaches the Grox zone or the
  core, and can fly to a partner who is there. The core waits until everybody in Space has
  arrived (or "Enter now"); then everybody sees the ending and gets the Staff of Life. A partner
  who was not there gets the gift too, also when joining later.
- **Rivals mode** (a host option): every player becomes a separate empire with an own budget (the
  shared money is split; switching back adds them up). Players can propose alliances, declare war
  and make peace on the Space page; at war, ships really damage each other (with the game's own
  death and respawn), and an enemy never repairs your ship.

### Fixes
- A guest who joined (or a host who loaded) in the first ~16-26 s after the galaxy menu came up
  could be stuck without a HUD in every stage: the menu's intro cinematic was left playing by the
  world load and took every click. The mod now ends it.
- The jump to a partner (and every co-op jump) into a star system nobody had visited arrived
  without planets and without first contact. It now travels like a player through the galaxy view:
  planets load, empires meet you.
- Two games generated different planets for the same unvisited star (rarer now, see known issues).
- A jump to an id that is not a star of the galaxy crashed the game (GetStarRecord).
- The Space page and the jump buttons handle many rows (pages with "<" and ">").
- Rescue on a planet: a partner flying over your disabled ship at another zoom height could not
  repair it (the distance was measured in 3D, and a ship's height on a planet is its zoom). The
  repair distance is now measured along the ground.
- A click on the main menu's **Co-op** item while the galaxy menu's intro plays (about 16-30 s
  after the menu appears) did nothing: the mod now skips the intro (like Esc) and opens the window.
- LAN discovery asks 20 ports (it found hosts on ports 47810-47819 only).

### Testing
- New two-game tests for every feature (`coop_space_map`, `_allies`, `_trade`, `_contracts`,
  `_diplomacy`, `_finale`, `_rivalry`, `_jump`, `_guest_ui`, `_page`), all with real clicks on the
  Space page, on both supported builds (GOG 2016 lanes and the new GOG 2024 lanes 9 and 10 under
  Wine). All fixtures were made anew for the GOG 2024 install, plus `space_empire` (drive,
  colonies, empire contacts).
- `tools/re_lint.py` checks every symbol the code uses against every supported build.
- Release gate: the full suite (45 tests) on 6 lanes, three GOG 2024 and three GOG 2016: 43 of
  45, and the two others (`space_rivalry`, `space_puppet`: the GOG 2024 landing delay after a
  load, now retried by the tests) passed 4 of 4 on GOG 2024 lanes afterwards.

### Known issues
- On GOG 2024, zooming in to land on a planet can do nothing for up to ~80 s right after a world
  load; then it works.
- Two games can still generate different planets for a star nobody visited (rarely).
- While a game panel that waits for OK is open (for example the badge points after a raid), the
  co-op window gets no clicks: press Enter or OK first.
- Details and more: [docs/known-issues.md](docs/known-issues.md).


## [0.6.2] - 2026-10-23

Builds with the same code as a supported one, and a list of where SporeCoop has been tested.
0.6.0, 0.6.1 and 0.6.2 can play together (the network protocol did not change); all players need
the same game build.

### Where SporeCoop has been tested

| Game build | System | Tested |
|---|---|---|
| GOG 2016 (`SporeApp.exe` SHA-256 `922af7c7…`) | Linux, Wine / Bottles | Yes: developed on it; every stage, 2-4 players, the stability runs of 0.6.0 |
| GOG 2024, the current GOG download (`dc04aee5…`) | Windows 11 | Yes (0.6.1): every stage as host + guest, two game instances on one PC |
| GOG 2024 with a changed header (same code) | Windows 11 | Only with a modified copy of the GOG 2024 exe (0.6.2) |
| GOG 2016 | Windows | Not tested (0.6.0 and older could not work on Windows, fixed in 0.6.1) |
| GOG 2024 | Linux, Wine | Not tested |
| Steam, EA app / Origin, disc | any | Not tested: co-op stays off unless the exe's code matches a build above |
| Two different PCs over the internet or a VPN | Windows | Not tested with 0.6.1+ on Windows |

The same list, kept up to date: [INSTALL.md, section 2](https://github.com/WoodyWizard/SporeCoop/blob/main/docs/INSTALL.md#where-sporecoop-has-been-tested).
Tried another build? An [issue](https://github.com/WoodyWizard/SporeCoop/issues) with
`mods\logs\sporekit.log` (it names the exe's `sha256` and `code_sha256`) helps.

### Game builds
- A `SporeApp.exe` that differs from a supported build only in the PE header, the resources or a
  signature now gets that build's addresses: symbol files carry a `code_sha256` (the code and
  data sections and their addresses, `tools/exe_info.py`), and SporeKit uses it when no file
  names the exact exe. Such a player counts as that build when joining. Tested with a modified
  copy of the GOG 2024 exe (loaded at another base, joined a GOG 2024 host); a copy with one
  changed code byte is still refused.
- SporeCoopSetup tells such a build apart ("same code as the GOG 2024 build").
- INSTALL.md lists which game builds and systems SporeCoop has been tested on.
- SporeKit 0.3.1: `SkHost::symbols_build`.

## [0.6.1] - 2026-10-23

Support for the current GOG build of Spore, and a fix that made the mod work on Windows at all.
Tested on Windows 11 with the GOG 2024 build: host and guest (two game instances on one PC) played
every stage: cell, creature, tribe, civilization and space (joining, world transfer, puppets,
NPC damage through the host, shared DNA / money / food, civ purchases, buildings and orders, tribe
orders, ship damage, saving), with no faults.

If you installed 0.6.0 on the current GOG build, the Co-op item was missing and
`mods\logs\sporekit.log` said "no symbol database for this SporeApp.exe build". Install 0.6.1 with
`SporeCoopSetup.exe` or `install.bat` (the in-game update cannot start there, because the mod was
off). 0.6.0 and 0.6.1 can play together (the network protocol did not change), but all players
need the same game build.

### The current GOG build and Windows
- The current GOG download of the SPORE Collection (the 2024 update, `SporeApp.exe` 24 895 536
  bytes, SHA-256 `dc04aee5…`) is supported: it has its own symbol files
  (`march2017_dc04aee5.sym`, `overrides_dc04aee5.sym`). Before, SporeKit found no symbols for it
  and the mod turned itself off. Players of one game still need the same build.
- Windows: every game address was off by the exe's relocation when Windows loaded the exe at
  another base (ASLR, e.g. 0x410000). SporeKit now reads the exe's preferred base from the file,
  not from the header in memory, which Windows rewrites. Wine does not move the exe.
- The last hard-coded game addresses in the mod (vehicle, citizen, tree and drawable vtables, the
  civ growth and city planner return addresses, the allocator) are symbols now.
- `tools/build_windows.sh` builds the DLLs on Windows (Git Bash + llvm-mingw, no make).

## [0.6.0] - 2026-10-23

Installer, updates from GitHub, and a full stability campaign: bots played every stage for hours,
4-player sessions with rejoins, long chaos runs and bad networks. About 40 bugs found and fixed;
the numbers are in [docs/stability-report.md](https://github.com/WoodyWizard/SporeCoop/blob/main/docs/stability-report.md).

All players need 0.6.0 (the network protocol changed; 0.5.0 cannot join).

### Install and update
- `SporeCoopSetup.exe`: finds the game, tells whether the build is the tested one, installs,
  updates and uninstalls with one button; downloads the newest release from GitHub (or uses the
  update pack next to it, offline).
- Updates in the game: SporeCoop checks this repository's releases at the start; the Co-op window
  shows an Update button; files are checked against the release's SHA-256 sums and installed for
  the next start (`[update] check=0` in `mods\coop.ini` turns the check off).
- Releases also carry `SporeCoop-<version>.pack` (the update) and `SporeCoopSetup.exe`.

### Crashes and freezes fixed
- A guest loading the host's world could crash in the galaxy menu's planet view, or come up on a
  planet without its height map (ground 6 m low, animals falling through). Loads now always run
  outside the game's frame.
- A guest could hang in the loading screen for good when it left to the main menu during an
  evolution vote (a save written while the editor opened); saves and editors now wait for each
  other. The host's own save no longer breaks when it leaves the game while a guest joins.
- Tribe: a rare fault when the stage was set up right after a load.
- Space: a fault when the respawn caption ended after the ship had left the planet; the host could
  crash when the game removed a partner's ship.
- Civilization: a guest could crash and freeze on a vehicle the game had removed.
- Creature: a guest fault in the creature event handler.

### Sync fixes
- Busy Tribe and Civilization planets (24+ citizens, many vehicles) no longer lose their state on
  guests: snapshots were larger than one packet and dropped every time.
- Animals stand where the host has them on every guest (some proxies were never updated).
- Cell: the host no longer destroys cells nobody ate; mirrored cells no longer fly away; the
  partner's cell no longer blinks.
- Creature and Tribe: killed animals no longer fly into the sky.
- Civilization: NPC cities, captures, ruins, spice claims and new cities match on every machine;
  money from derricks is no longer counted twice.
- Creature: a guest's kill pays DNA; guests keep the animals while the host's game is paused;
  goal cards no longer call species extinct.

### Gameplay fixes
- The host no longer leaves the game by itself: the mod used to answer "Do you want to save your
  game?" for 30 s after each save. Only the "game saved" box is confirmed now.
- Cell: the host's creatures hunt every player, not only the host.
- Creature: guests can befriend animals and make alliances; when the host goes to the main menu,
  guests are told and play on; name labels hide under menus.
- Tribe: guests can gather from fruit trees; gifted members can no longer be ordered by the giver;
  a knocked-out chief is replaced.
- Space: a disabled guest ship can be repaired in real fights; partners' ships no longer fire on
  their own.

### Testing
- Bots that play each stage through real input (`tools/tests/play_*.py`), 4-player sessions
  (`coop_multi.py`), longer chaos runs, regression tests for every fix; tests run on an invisible
  display. See [docs/06-testing.md](https://github.com/WoodyWizard/SporeCoop/blob/main/docs/06-testing.md).

### Known issues
See [docs/known-issues.md](https://github.com/WoodyWizard/SporeCoop/blob/main/docs/known-issues.md). The ones players can meet: the
host's capture cinematic in Civilization pauses everybody until the host presses OK; in Cell a
guest far from the host swims in an empty pool; the guest's social game in Creature is simplified.

## [0.5.0] - 2026-10-23

The first public release: co-op in every stage of Spore, with a shared world, everybody's own
creations and mechanics that only work together.

Tested with the GOG "SPORE Collection" build (`SporebinEP1\SporeApp.exe`, SHA-256 `922af7c7…`).
Install: [docs/INSTALL.md](https://github.com/WoodyWizard/SporeCoop/blob/main/docs/INSTALL.md) - extract the zip into `SporebinEP1`, run
`install.bat`. All players need this same version.

### Playing together
- **Co-op** item in Spore's main menu: host or join (LAN discovery, address, VPN), lobby with
  players, stage, ping, readiness and chat; **F9** in game. Notifications slide in as native panels.
- One world in every stage: the host simulates, guests mirror it and act through the host; the
  host's world is sent to guests and loaded automatically, including after evolving.
- Evolution by vote: everybody presses *I'm ready* before the editor or the next stage.
- Own creations: every player plays their own creature, cell and ship from *My Creations*.

### Stages
- **Cell: Symbiosis:** shared tide pool and food; a knocked-out cell is revived by a partner
  right next to it, otherwise it respawns small next to the partner.
- **Creature: Pack:** joint hunting +25 %, the pack saves a dying player, healing near a partner.
- **Tribe: Chiefs:** members split between players, a chief per player with an aura, pincer
  attacks +50 %, duet performances, healer food, epic creatures at the village, gifts of members.
- **Civilization: Governors:** own cities and vehicles, military / economic / religious roles,
  joint sieges, city alarms with beacons, superweapons by vote, trade and gifts between players.
- **Space: Squadron:** shared enemies, rescue of a disabled ship, jump to a partner's star, raid
  alerts, mission reward tools for partners, terraforming together, spice gifts.

### Fixed in this release
- Loading a world while the Space stage ran crashed the game (every guest rejoin in Space); loads
  now leave to the galaxy menu first.
- The host's "game saved" box could stay open and pause the host.
- The Co-op menu item could hang the game after several returns to the galaxy menu.
- Cell teleports and respawns landed metres away.
- World transfer is about 3× faster (128-packet acknowledgement window).

### Known issues
See [docs/known-issues.md](https://github.com/WoodyWizard/SporeCoop/blob/main/docs/known-issues.md): a rejoin over a very lossy connection can take
long; the guest's *Done* button in its own editor does nothing (*Cancel* works).

## [0.2.0] - 2026-10-21 (not published)

Co-op presence in every game mode: other players visible in Cell, Creature, Tribe, Civilization,
Space and Adventures, shared DNA, food and money, host world transfer.
