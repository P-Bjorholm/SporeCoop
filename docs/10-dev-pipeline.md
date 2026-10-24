# 10. Development pipeline

How SporeCoop is developed and tested so that a release works for a normal player: many short
feedback loops on the developer's machine, every change tested on real game instances in
parallel, random players (bots) that do what nobody planned, and written-down knowledge.
Rules for every contributor and agent: [CLAUDE.md](../CLAUDE.md).

## 10.1 Where the time goes (measured 2026-10-22, Ryzen 5 7500F, 2 instances per lane)

| Step | Time |
|---|---|
| Wine + SporeKit up | 4 s |
| host: galaxy menu → planet loaded (`coop.load`) | ~10 s |
| guest: menu settles, joins | ~8 s |
| world to the guest: host save 2.5 s + transfer of 3.8 MB + load 0.6 s | ~8 s |
| **cold start of a pair** | **~40 s** |
| **warm reset** (same save: new slot, `coop.load` via the galaxy menu, guest rejoins) | **~7 s** (5–6 s before loads went through the menu) |
| stage switch in a running game (`coop.load`: save + quit to the galaxy menu, load) | host in the new stage 5–6 s, guest together 16–18 s |
| hot reload of coop.dll in running games | ~1 s |
| memory per game | ~480 MB (12 games ≈ 6 GB) |

The world transfer was capped at 32 packets per frame (~1.1 MB/s); a 128-packet ack window
makes it ~3 MB/s with 5 % loss (unit test `bulk transfer`).

## 10.2 Test lanes

A lane is an independent copy of the whole setup, so several test pairs run at the same time:

* lane 1 = the game folder that contains this repository (the repository is cloned into the
  game folder, next to `SporebinEP1`), in the Bottles bottle `SPORE_BOTTLE` (default `Games`);
* lane N ≥ 2 = `tools/spore.sh lane create N`: reflink copies (Btrfs: no extra space) of the game
  folder (`SPORE-lab/laneN`: own `mods/`, logs, `SporeCoop.package`) and of the bottle
  (`SporeLabN`: own wineserver, so a crash or quit stays in the lane; registry paths point to the
  copy); takes 2 s.
* instances `2N-1` (host) / `2N` (guest): console ports 47800 + i − 1, co-op port 47810 + 2(N−1);
  test profiles `test` / `inst<2N>`; outputs under `out/laneN/`.
* `SPORE_LANE=N` selects a lane for `tools/spore.sh`, every test and helper (`tools/tests/lane.py`).
* Lanes 1..10 (ports, `tools/tests/lane.py`); at most 6 at once on this machine (`lab.py` caps it:
  a GPU reset under more load, docs/06-testing.md 6.19). Since 2026-10-24 every lane is a copy of
  the GOG 2024 install (`dc04aee5`, the only build supported from 0.8 on). The GOG 2016 exe is
  kept in `~/Games/Modding/_ref/gog2016/SporeApp.exe` (a 2016 lane: copy it into a lane's
  `SporebinEP1`).

## 10.3 The lab (`tools/lab.py`)

```
tools/lab.py run smoke --lanes 3                 # before a merge (~8 min)
tools/lab.py run full --lanes 3 --branch feat/x  # a branch: its own worktree + build
tools/lab.py run tribe_play,select               # chosen tests
tools/lab.py run chaos_tribe --repeat 10 --retry 0   # hunt a flaky bug
tools/lab.py reload --lane 2                     # dev loop: build + hot reload in running games
```

* Tests and their properties: `tools/tests/suite.py` (suites smoke / full / nightly, warm,
  exclusive, length, save).
* Scheduling: one test per lane at a time, longest first; a lane prefers a test on the save it
  has loaded (warm reset instead of a restart); exclusive tests (LAN discovery) run alone at the end.
  They run on the run's first lane among 1-5 (LAN discovery sees only those) and are skipped when
  the run has none; until 2026-10-23 they always took lane 1, even with `--skip 1` (a run on lanes
  7/8 started the UI test in the real install's bottle while other agents had lane 1).
* After a failure the lane restarts cold and the test runs once more: passing then = **flaky**.
  `out/lab/flaky.json` keeps the history; the summary lists the unstable tests - each one is a
  bug in the test or in the mod and gets fixed, not ignored.
* Results: `out/lab/<run>/summary.md`, `results.json`, the test logs and both games' own logs.

## 10.4 Invariants (checked in every test)

`twoinst.start()` starts a monitor for both games; `Checks.faults()` fails the test when:

* a game's frames stop for more than 10 s (frozen; the SporeKit watchdog writes the main thread
  stack into the game log after 30 s);
* a game stops answering (crash);
* the exception logger caught a fault (`exc`);
* a fixture lacks what the test needs (`tools/tests/fixtures.py` facts).

## 10.5 Bots

* **Scenario tests** (`coop_*.py`) - one feature each, with checks.
* **Chaos bot** (`coop_chaos.py`) - both players act at random but validly: clicks on own and
  partner units (`coop.targets`), selection boxes (`drag`), orders, keys, the co-op window, the
  pause menu, stage actions, rejoins, saves, network outages. Seeded: the printed replay line
  repeats the same actions. Smoke: 2 min of tribe; nightly: every stage, minutes each.
* **Bad network** (`netem.py`): `SPORE_NET=dsl|mobile|awful|delay=..,jitter=..,loss=..` puts a UDP
  proxy with delay, jitter, loss, duplicates and outages between guest and host - for any test.
  The proxy waits on both sockets with `select()` and drains them; until 2026-10-23 it polled each
  socket with a 5 ms timeout and so capped the link at ~200 packets/s (~200 KB/s, plus kernel
  drops of its full receive buffer): every world transfer over it took ~21-24 s whatever the preset.

## 10.6 Fixtures (test saves)

`out/fixtures/<name>.tgz` (main checkout, shared by worktrees), described in
`tools/tests/fixtures.py` (planet, stage, facts). New ones: `tools/tests/make_fixture.py <base>
<name> --cmd "…"` (load, run commands, save, pack) or `new_stage_game.py` for a new stage game.
Warm resets put a fixture into a fresh slot (`Game10`…`Game19`): the files of the loaded save
must not change under the game.

## 10.7 Levels

| When | What | Where |
|---|---|---|
| every build | `make test`: unit tests (netcode, DBPF, discovery), `re_lint` (symbols known, confidence) | local + GitHub Actions |
| after a change | the feature's test on a warm lane, `lab.py reload` for the next try | local |
| before a merge to `main` | `lab.py run smoke --branch <b>` passes | local |
| nightly 03:30 | `tools/nightly.sh`: the nightly suite on `main`, desktop notification | local (systemd user timer) |
| before a release | nightly green + manual play-test checklist (two people, every stage) | local |

GitHub Actions (`.github/workflows/build.yml`) only builds and runs the unit tests: game tests
need Spore, and a self-hosted runner would let anyone's pull request run code on this machine.

Nightly timer (from the repository root):

```bash
sed "s|@REPO@|$PWD|g" tools/systemd/sporecoop-nightly.service > ~/.config/systemd/user/sporecoop-nightly.service
cp tools/systemd/sporecoop-nightly.timer ~/.config/systemd/user/
systemctl --user daemon-reload && systemctl --user enable --now sporecoop-nightly.timer
```

## 10.8 Git

`main` always works. One topic per branch (`feat/…`, `fix/…`), each in its worktree
(`../SporeCoop-wt/<branch>`, made by `lab.py run --branch`); small commits; a pull request with the
lab summary; merge after smoke passes. A bug found anywhere → `docs/known-issues.md` or a GitHub
issue → a test that reproduces it → the fix.

## 10.9 Knowledge

Search before analysing, write down after learning: `data/symbols/overrides_*.sym` (functions with
confidence and signature), `docs/re/<area>.md`, `docs/known-issues.md`, this file and
`docs/06-testing.md`. `tools/re_lint.py` keeps the symbol file honest. Subagents get the same rule
in their prompt.
