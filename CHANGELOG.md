# Changelog

## [Unreleased]

### Install and update
- `SporeCoopSetup.exe`: finds the game, tells whether the build is the tested one, installs,
  updates and uninstalls with one button; downloads the newest release from GitHub (or uses the
  update pack next to it, offline).
- Updates in the game: SporeCoop checks this repository's releases at the start; the Co-op window
  shows an Update button; files are checked against the release's SHA-256 sums and installed for
  the next start (`[update] check=0` in `mods\coop.ini` turns the check off).
- Releases also carry `SporeCoop-<version>.pack` (the update) and `SporeCoopSetup.exe`.

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
