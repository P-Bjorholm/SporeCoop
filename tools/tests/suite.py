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
    suites = set(suites) | ({'nightly'} if 'nightly' in suites else {'full', 'nightly'})
    T.append(dict(name=name, cmd=cmd, suites=suites, warm=warm,
                  exclusive=exclusive, minutes=minutes, fixture=fixture))


P = 'python3 tools/tests/'
add('unit', 'make test', ('smoke',), minutes=1)
add('creature', P + 'coop_two_instances.py --guest-menu', ('smoke',), minutes=4)
add('cell', P + 'coop_mode.py --fixture cell', minutes=3)
add('tribe', P + 'coop_mode.py --fixture tribe', minutes=3)
add('civ', P + 'coop_mode.py --fixture civ', minutes=3)
add('space', P + 'coop_mode.py --fixture space', minutes=3)
add('phase', P + 'coop_phase.py', minutes=6)
add('ui', P + 'coop_ui.py', ('smoke',), exclusive=True, minutes=5)
add('team', P + 'coop_team.py', ('smoke',), warm=True, minutes=3, fixture='creature')
add('tribe_coop', P + 'coop_tribe.py', ('smoke',), warm=True, minutes=4, fixture='tribe')
add('civ_coop', P + 'coop_civ.py', ('smoke',), warm=True, minutes=3, fixture='civ')
add('cell_coop', P + 'coop_cell.py', ('smoke',), warm=True, minutes=4, fixture='cell')
add('space_coop', P + 'coop_space.py', ('smoke',), warm=True, minutes=2, fixture='space')
add('stage_switch', P + 'coop_stage_switch.py', minutes=13)  # every stage pair that matters, in game
add('vote', P + 'coop_vote.py', minutes=5)
add('guest_editor', P + 'coop_guest_editor.py', minutes=4)
add('notify', P + 'coop_notify.py', minutes=3)
add('sync', P + 'coop_sync.py', warm=True, minutes=6)
add('creations', P + 'coop_creations.py', minutes=4)
add('creations_cell', P + 'coop_creations_cell.py', minutes=4)
add('select', P + 'coop_select.py', ('smoke',), warm=True, minutes=3, fixture='creature')
add('exit_menu', P + 'coop_exit_menu.py', minutes=3)  # a guest exits to the main menu and comes back
add('tribe_play', P + 'coop_tribe_play.py', ('smoke',), warm=True, minutes=3, fixture='tribe')
add('civ_play', P + 'coop_civ_play.py', warm=True, minutes=3, fixture='civ_npc')
# two bots play Civilization for 10 minutes through real input (docs/stability/civ.md)
add('play_civ', P + 'play_civ.py --minutes 10', ('nightly',), minutes=13, fixture='civ_npc')
add('space_play', P + 'coop_space_play.py', ('smoke',), minutes=6)
add('space_page', P + 'coop_space_page.py', ('smoke',), minutes=4)  # the F9 Space page (space_ext.h), jump by a real click
add('space_death', P + 'coop_space_death.py', minutes=5)
add('space_guest_ui', P + 'coop_space_guest_ui.py', minutes=4)  # a guest joining during the menu intro: no movie left over, real clicks work
add('space_puppet', P + 'coop_space_puppet.py', minutes=5)  # the game frees partner puppets
add('space_rescue', P + 'coop_space_rescue.py', minutes=6)  # disabled by real (relayed) NPC hits
# joins and rejoins over a bad network (goodbye lost every second round), the setup + in-game update
add('rejoin_mobile', 'SPORE_NET=mobile ' + P + 'coop_rejoin_net.py', minutes=5)
# many rejoins / world loads in a row, fault log checked after each (docs/re/space.md 8.3): a world sent
# again while the guest loads, a load right after the galaxy menu came up in fresh games, rejoins at
# random moments
# the mod confirms the host's "game saved" box without answering other menus (pause, exit question)
add('save_box', P + 'coop_save_box.py', ('full',), minutes=4)
add('rejoin_loop', P + 'coop_rejoin_loop.py --rounds 15 --reshare 0:2.5', minutes=5)
add('menu_load', P + 'coop_rejoin_loop.py --rounds 5 --local-load 0:1.5 --cold', minutes=5)
add('rejoin_race_long', P + 'coop_rejoin_loop.py --rounds 40 --race 1:9', ('nightly',), minutes=8)
add('rejoin_awful', 'SPORE_NET=awful ' + P + 'coop_rejoin_net.py', ('nightly',), minutes=8)
add('update', P + 'update_check.py', minutes=6)  # a lane outside the run (SPORE_FREE_LANE), one game
# chaos bot (random real input by both players, network churn); smoke: 2 minutes of tribe
add('chaos_tribe', P + 'coop_chaos.py --fixture tribe --minutes 2', ('smoke',), minutes=3)
for st, m in (('creature', 6), ('cell', 6), ('tribe', 8), ('civ', 8), ('space', 6)):
    add(f'chaos_{st}_long', P + f'coop_chaos.py --fixture {st} --minutes {m}', ('nightly',), minutes=m + 1)
# two bots PLAY the Cell stage together (real input: swim, eat, flee, revive), sync / food / regression checks
add('play_cell', P + 'play_cell.py --minutes 10', ('nightly',), warm=True, minutes=12, fixture='cell')
# played like people play (real input, measurements; docs/stability/creature.md)
add('play_creature', P + 'play_creature.py --minutes 10', ('nightly',), minutes=12)
# games played by bots through real input for many minutes (docs/stability/<stage>.md)
add('play_tribe', P + 'play_tribe.py --minutes 10', ('nightly',), warm=True, minutes=12, fixture='tribe')
# the same tests over a bad network between guest and host (tools/tests/netem.py)
for name, cmd in (('tribe_coop', 'coop_tribe.py'), ('cell_coop', 'coop_cell.py'), ('civ_coop', 'coop_civ.py'),
                  ('team', 'coop_team.py'), ('space_coop', 'coop_space.py')):
    add(f'{name}_mobile', 'SPORE_NET=mobile ' + P + cmd, ('nightly',), minutes=4)
# the Space stage played by two bots with real input (docs/stability/space.md)
add('play_space', P + 'play_space.py --minutes 12', ('nightly',), minutes=14)
add('chaos_tribe_awful', 'SPORE_NET=awful ' + P + 'coop_chaos.py --fixture tribe --minutes 5', ('nightly',),
    minutes=6)


def select(spec):
    """suites (smoke / full / nightly) and test names, comma-separated and mixed: 'smoke,vote'"""
    names = {t['name'] for t in T}
    want = set()
    for w in (x.strip() for x in spec.split(',') if x.strip()):
        if w in ('smoke', 'full', 'nightly'):
            want |= {t['name'] for t in T if w in t['suites']}
        else:
            assert w in names, f'unknown test: {w} (known: {sorted(names)})'
            want.add(w)
    return [t for t in T if t['name'] in want]
