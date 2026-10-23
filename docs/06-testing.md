# 6. Testing

> Running the tests in parallel (lanes, `tools/lab.py`, warm resets, chaos bot, bad network):
> [10-dev-pipeline.md](10-dev-pipeline.md). This page covers the single-lane basics.

## 6.1 Environment

* The game is installed in the Bottles bottle **Games** (runner `caffe-10.0`, DXVK); the Spore registry entries are already there.
* Launch: `tools/spore.sh run [-p profile] [-i N]` → `bottles-cli run -b Games -e .../SporeApp.exe -- <args>`.
  Game arguments go after `--`. Bottles filters environment variables, so settings are passed
  as command-line switches (`-sporekit-profile:test`, `-sporekit-instance:2`).
* Build: llvm-mingw (`~/.local/opt/llvm-mingw-*/bin/i686-w64-mingw32-clang++`), `make`.

## 6.2 Profiles — tests don't touch your saves

`-sporekit-profile:<name>` redirects Spore's user data to
`AppData/Roaming/SporeKit-<name>/Spore`. `spore.sh run -p test` creates the profile from a copy of
your settings, but with a 1440×1080 window and no tutorials or hints. Instance `-i 2` uses the
`inst2` profile automatically. A fixture with a Creature stage save:

```bash
tools/spore.sh fixture save creature test       # capture
tools/spore.sh fixture restore creature inst2   # put into another profile
python3 tools/tests/load_game.py -i 2           # load it via the menu
```

Fixtures for other modes (`out/fixtures/*.tgz`) are made like this:

```bash
python3 tools/tests/new_stage_game.py <cell|creature|tribe|civ|space>  # new game via stage selection
python3 tools/tests/new_stage_game.py civ --npc-cities 3  # civ with cities of other nations
python3 tools/tests/space_setup.py      # space: new game, skip tutorial, leave the planet, save
tools/sporectl.py coop.save             # save (same as the menu button)
tools/spore.sh fixture save <name> test
tools/sporectl.py coop.load Game0 Planet   # load a profile save directly from the menu
```

`new_stage_game.py` wants a test profile without saves (empty `Games` folder) and the 1440x1080
window: it finds a free planet slot by itself (each slot ring of the galaxy menu has a 48x48 UI
window with id 0x02E5A4A8 in `coop.ui tree`; `--slot N` picks the N-th, `--slot X,Y` a point),
hovers before clicking and checks that "Choose A Stage" opened. The wizard after it clicks fixed
points; a run that ends with "FAILED: still GalaxyGameEntry" can simply be repeated.

The fixtures (tools/tests/fixtures.py lists planet and checked facts):

| Fixture | Planet | What it has | Made by |
|---|---|---|---|
| `creature`, `cell`, `tribe`, `space` | see fixtures.py | a game of that stage | `new_stage_game.py`, `space_setup.py` |
| `civ` | Game0/Agrauel | the player city only | `new_stage_game.py civ` |
| `civ_npc` | Game0/Taldon | the player city (with its free vehicle) and three cities of other nations (nearest about 400 m away, the planet radius is about 500), intro text dismissed, $3000 | `new_stage_game.py civ --npc-cities 3`, `coop.save`, `spore.sh fixture save civ_npc test` (2026-10-23) |

A new Civilization game has no other cities at first: they appear one by one when the player has
claimed spice and has money, after a timer (docs/re/civ.md "NPC cities"; about 18 minutes of play
in one test). `--npc-cities N` calls `cCitySimulator::AppearNextCity` N times instead.

Test runs don't get in the way of whoever is at the computer:
* **muted**: `prepare_profile` sets `AudioMuteAll true` and `AudioMasterVolume 0` in test
  profiles (the player's sound settings and PipeWire are left alone: PipeWire would remember
  the mute by application name and apply it to normal play too);
* **invisible**: `spore.sh run` starts the test games on a headless GNOME Shell
  (`tools/headless.sh up`: its own D-Bus session and runtime, config and state folders, a 5760x3240
  virtual monitor; its Xwayland display is passed to Bottles). Nothing appears on the user's
  desktop and the games keep drawing at 30 fps. SporeKit places each window in its own cell of a
  4-wide grid (`[sporekit] test_monitor = grid`): a covered window gets no frames. Do not move test
  windows to a hidden workspace of the real desktop: GNOME sends them no frame callbacks, DXVK waits
  and the game stops at frame 1. `SPORE_HEADLESS=0` shows the games on the desktop (then SporeKit
  moves them to a non-primary monitor: `test_monitor = auto|primary|off`). `tools/headless.sh down`
  stops the headless shell. Sharing the real runtime folder with it would unmount the Flatpak
  document portal, so it never does.

## 6.3 Development loop

```bash
make && tools/spore.sh deploy
tools/spore.sh run -p test
python3 tools/tests/load_game.py                 # up to the Creature stage
make coop && tools/spore.sh deploy && tools/sporectl.py mod.reload coop
tools/sporectl.py shot                           # look at the screen
tools/sporectl.py exc                            # caught exceptions (address + symbol)
tools/sporectl.py hangdump                       # main thread stack to the log (can hang under Wine)
tools/sporectl.py stack.here 300 all            # main thread call chain, read ON the main thread
tools/sporectl.py stack.scan 300                 # exe return addresses on the main stack, no suspend
tools/spore.sh logs                              # tail of sporekit.log
```

## 6.4 Test levels

| Level | What | How |
|---|---|---|
| Unit (Linux) | serialization, reliable delivery under 30% loss, localhost session, interpolation | `make test` |
| In-game loopback | a puppet mirrors your own avatar | `sporectl coop.echo on 6 0.3` |
| Protocol | host in the game + a fake Python guest | `python3 -I tools/fakepeer.py 127.0.0.1:47810 --mode circle` |
| Integration | two game instances on one machine, host/join, world transfer | `python3 tools/tests/coop_two_instances.py` |
| Modes | any mode from a fixture: guest joins from the menu, receives and loads the world, both puppets, movement | `coop_mode.py --fixture <cell\|tribe\|civ\|space> --planet GameN/Planet`, `coop_mode.py --adventure` |
| Manual | real game over the network | two PCs / VPN, package from `tools/package.sh` |

## 6.5 Startup speed

The two-instance test is ready for checks in about 40 s (it used to take about 90):
* both instances start right away (2 s apart), without waiting for the first one;
* instead of long frame-count waits after loading, a readiness check plus a 4 s pause;
* the host's "game saved" box (the host saves before sending the world) is confirmed by the
  mod once it is visible (docs/08-ui.md); `twoinst` also presses Enter when the host's frames
  stall. A box left open pauses the host's game without stopping its frames: the host's cell
  does not move (coop_cell checks that no box is left);
* over LAN/localhost the world is sent with a large window (600 messages in flight at ping
  < 40 ms, 4 MB socket buffers): 3.8 MB in ~2.5 s instead of 7.5 s.

The game caps itself at 30 frames per second (33 ms per frame, `App::FrameLimit_ptr`).
`[sporekit] test_frame_ms` speeds up menu frames in test instances, `frame_ms` does so everywhere;
console: `frame.limit <ms>|auto`. Off by default: cell physics is tuned for 30 fps
(at 130 fps the player's cell drifts on its own), and UI tests wait in frames.

### Sync accuracy

`tools/tests/coop_sync.py` compares, in every phase, the position and rotation of each
synchronized object on the host and on the guest (command `coop.sync`: players, animals, cells,
citizens, vehicles, ships). Each guest sample is compared with the closer of the two host samples
around it. Typical values (90th percentile): cells 0.02 m, animals 0.2–0.7 m, citizens
0.8 m / 6°, partner 1 m / 6° (lag equal to the smoothing time), vehicles 1.2 m, ships
8.5 m at speeds around 100 m/s.

## 6.6 Known Wine quirks

* **Flatpak document portal.** Every Bottles launch bind-mounts `/run/user/<uid>/doc/by-app/com.usebottles.bottles`
  (FUSE of `xdg-document-portal`). It vanishes now and then while many Bottles sandboxes exit at once (seen every 10–20 min during lab runs on 2026-10-22; systemd then shows the portal restarted), and every start fails with
  `bwrap: Can't find source path …` in `out/run.<inst>.log`. `systemctl --user restart xdg-document-portal`
  brings it back; `tools/lab.py` checks it before every test and retries such starts (not counted as flaky).

* MinHook must not freeze threads (`VirtualProtect` may hang).
* In the second process of the same wineserver, blocking sockets/`select()` and starting new threads
  sometimes hang — the console runs on non-blocking sockets in a single thread.
* Spore stops rendering without focus; test runs enable SporeKit's "background mode".
* **A window on a hidden workspace is minimized.** Mutter marks every X11 window on a workspace that
  is not shown as iconic, and Wine then minimizes it (`WM_SYSCOMMAND SC_MINIMIZE`, `IsIconic` = 1).
  A minimized Spore draws nothing: `App::Canvas::Tick` (0x848260) calls the canvas's IsMinimized
  (vtable +0x5C, `IsIconic(GetAncestor(hwnd, 3))`) and sleeps 200 ms per tick, so a test game on
  the agent-ws `sporecoop` workspace stalled at frame 1 to 70 (watchdog "no frames", main thread in
  `Canvas::Tick`, 2026-10-22). Background mode ignores `SC_MINIMIZE` now (log line "minimize
  ignored"); hooking `IsIconic` alone was not enough (the main thread then spun in the message loop).
* **Exiting one instance kills all instances in the bottle** (that's how launching through
  bottles-cli behaves). So tests restart both instances together: the guest first, then the host.
* **Stack of a frozen game.** `stack` and `hangdump` suspend the main thread; under Wine that hung
  the console for good (2026-10-22). When frames stop but the console still answers `game.mode`,
  the main thread is pumping messages in some nested loop: `stack.here 300 all` runs on the main
  thread itself and lists every return address from its own frame up (modules and offsets; coop.dll
  offsets: `llvm-symbolizer --obj=build/coop.dll 0x1000_0000+offset`). `stack.scan` reads the stack
  from the TEB without suspending (stale values mixed in). Filter exe values to real return
  addresses (a call instruction before them) — UTF-16 text looks like code addresses. `winedbg`
  attach from `flatpak enter <wineserver pid>` failed with error 5.
* A hung Wine process can be left half-dead (a zombie leader with live threads)
  and keep holding UDP port 47810. `spore.sh kill` finishes such processes off with `SIGKILL`.

## 6.7 Reading console output in tests

* Tests parse `coop.status` with regular expressions that span lines, e.g. coop_cell / coop_creations
  / coop_vote expect the `puppet pos=… species=…` line **right after** the `remote <id> '<name>'`
  line. New per-remote lines go after the puppet line (the `together <id> <0|1>` line that
  `twoinst` waits for is the last line of each remote's block). On 2026-10-22 a `together` line
  inserted between them made coop_cell report "sync samples taken: samples 0" on every run while the
  partner cell was there and exact — it looked like a missing puppet / timing bug.
* Cross-check before hunting a "missing puppet": the `remote … puppet=<ptr>` value, and in the cell
  stage `local_gameplay_cells` (counts every foreground cell except the own avatar) = mirrors + 1
  when the partner's cell exists.
* A real missing partner (`partner_dist=-1` on the guest, no puppet, bites and food still work)
  was the host's "game saved" box left open (fixed 2026-10-22): look at the host screenshot
  first. A host that "moves" 0.00 m in the sync samples is paused the same way.
