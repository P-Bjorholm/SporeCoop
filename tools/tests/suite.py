"""Every automated test with its properties (used by tools/lab.py and tools/tests/run_all.sh).

  name      result logs: out/lab/<run>/<name>.log
  cmd       command line, run from the repository root with SPORE_LANE set
  suites    smoke (before a merge, ~10 min on 3 lanes), full (every test), nightly (full + long)
  warm      may reuse running games of the lane (twoinst.start with SPORE_WARM=1: the fixture is
            loaded into a fresh save slot, the guest rejoins; no restart). Tests that change the
            profile, enter editors or start their own games are cold.
  exclusive needs the machine alone (LAN discovery in the UI test sees other lanes' hosts)
  minutes   rough length, the scheduler starts long tests first
"""

T = []


def add(name, cmd, suites=('full',), warm=False, exclusive=False, minutes=4, fixture=None):
    T.append(dict(name=name, cmd=cmd, suites=set(suites) | {'full', 'nightly'}, warm=warm,
                  exclusive=exclusive, minutes=minutes, fixture=fixture))


P = 'python3 tools/tests/'
add('unit', 'make test', ('smoke',), minutes=1)
add('creature', P + 'coop_two_instances.py --guest-menu', ('smoke',), minutes=4)
add('cell', P + 'coop_mode.py --fixture cell --planet Game0/Karke', minutes=3)
add('tribe', P + 'coop_mode.py --fixture tribe --planet Game0/Cohrach', minutes=3)
add('civ', P + 'coop_mode.py --fixture civ --planet Game0/Agrauel', minutes=3)
add('space', P + 'coop_mode.py --fixture space --planet Game0/Maraya', minutes=3)
add('phase', P + 'coop_phase.py', minutes=6)
add('ui', P + 'coop_ui.py', ('smoke',), exclusive=True, minutes=5)
add('team', P + 'coop_team.py', ('smoke',), warm=True, minutes=3, fixture='creature')
add('tribe_coop', P + 'coop_tribe.py', ('smoke',), warm=True, minutes=3, fixture='tribe')
add('civ_coop', P + 'coop_civ.py', ('smoke',), warm=True, minutes=3, fixture='civ')
add('cell_coop', P + 'coop_cell.py', ('smoke',), warm=True, minutes=4, fixture='cell')
add('space_coop', P + 'coop_space.py', ('smoke',), warm=True, minutes=2, fixture='space')
add('vote', P + 'coop_vote.py', minutes=5)
add('notify', P + 'coop_notify.py', minutes=3)
add('sync', P + 'coop_sync.py', warm=True, minutes=6)
add('creations', P + 'coop_creations.py', minutes=4)
add('creations_cell', P + 'coop_creations_cell.py', minutes=4)
add('select', P + 'coop_select.py', ('smoke',), warm=True, minutes=3, fixture='creature')
add('tribe_play', P + 'coop_tribe_play.py', ('smoke',), warm=True, minutes=3, fixture='tribe')
add('civ_play', P + 'coop_civ_play.py', warm=True, minutes=3, fixture='civ')
add('space_play', P + 'coop_space_play.py', ('smoke',), minutes=6)


def select(spec):
    """'smoke' / 'full' / 'nightly' or comma-separated test names"""
    names = {t['name'] for t in T}
    if spec in ('smoke', 'full', 'nightly'):
        return [t for t in T if spec in t['suites']]
    want = [s.strip() for s in spec.split(',') if s.strip()]
    bad = [w for w in want if w not in names]
    assert not bad, f'unknown tests: {bad} (known: {sorted(names)})'
    return [t for t in T if t['name'] in want]
