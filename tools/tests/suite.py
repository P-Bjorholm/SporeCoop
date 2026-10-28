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
add('cell_meat', P + 'coop_cell_meat.py', warm=True, minutes=4, fixture='cell')  # kills, their meat, eating once
add('cell_spin', P + 'coop_cell_spin.py', warm=True, minutes=4, fixture='cell')  # creatures face as on the host on the guest; guest bites kill
add('cell_leave', P + 'coop_cell_leave.py', minutes=4)  # Cell -> Creature with mirrors on the guest, then the session ends
add('leave_cleanup', P + 'coop_leave_cleanup.py', minutes=4, fixture='space')  # every session end cleans every stage module (BUG-0033): Space mirrors after Leave, host while connected
add('space_coop', P + 'coop_space.py', ('smoke',), warm=True, minutes=2, fixture='space')
add('stage_switch', P + 'coop_stage_switch.py', minutes=13)  # every stage pair that matters, in game
add('vote', P + 'coop_vote.py', minutes=5)
add('vote_stale', P + 'coop_vote_stale.py', minutes=5)  # a vote ends with its session and its stage (BUG-0043)
add('leave_during_share', P + 'coop_leave_during_share.py', minutes=4)  # Leave right after a share save: the host can still leave the stage (BUG-0140)
add('exit_thread', P + 'coop_exit_thread.py', minutes=2)  # ExitProcess without Shutdown: no abort from the keepalive std::thread (BUG-0107)
add('ui_menu_leave', P + 'coop_ui_menu_leave.py', minutes=2)  # the galaxy menu's Co-op item is dropped as the game leaves the menu (BUG-0163)
add('reload_update', P + 'coop_reload_update.py', minutes=2)  # hot reload while the update check waits: the old thread stays in the old image (BUG-0060)
add('guest_editor', P + 'coop_guest_editor.py', minutes=4)
add('notify', P + 'coop_notify.py', minutes=3)
add('sync', P + 'coop_sync.py', warm=True, minutes=6)
add('creations', P + 'coop_creations.py', minutes=4)
add('net_robust', P + 'coop_net_robust.py', minutes=4)  # malformed messages from a joined player (BUG-0201)
add('creations_cell', P + 'coop_creations_cell.py', minutes=4)
add('editor_desync', P + 'coop_editor_desync.py', minutes=7)  # mating call and editors in Cell and Creature, real input
add('creature_desync', P + 'coop_creature_desync.py', minutes=4)  # editors, a crash and a reload: companion of a host-only species, no NPC copies, kills, the guest's look
add('editor_strays', P + 'coop_editor_strays.py', minutes=6)  # two evolutions: no puppet left over, herd homes on the surface (crash 0xB88BEA)
add('creature_e84', P + 'coop_creature_e84.py', minutes=5)  # a save with the partner's puppet, loaded after a restart: no crash on attack (0xD48634), removed
add('alt_enter', P + 'coop_alt_enter.py', minutes=4)  # Alt+Enter / Alt+Shift+Enter in co-op: no fault at 0x7777BE (device recreation), on the ground
add('nest_species', P + 'coop_nest_species.py', minutes=4)  # the guest owns another creation under the key of the host's: the host's species at its nests, its table
add('migration', P + 'coop_migration.py', minutes=5)  # brain upgrade, editors, migration; both claim the new nest: no fault on the guest (0xC6AE60)
add('select', P + 'coop_select.py', ('smoke',), warm=True, minutes=3, fixture='creature')
add('creature_kills', P + 'coop_creature_kills.py', minutes=3)  # a guest's kill dies on the guest too (no 0 hp mirror standing), NPCs notice the partner
add('creature_death', P + 'coop_creature_death.py', minutes=4)  # alone: die, skip the caption and the respawn cutscene
add('creature_respawn', P + 'coop_creature_respawn.py', minutes=5)  # both players die (also after a pack rescue): the hatchling grows up and walks
add('diag', P + 'diag_check.py --crash', minutes=5)  # kept logs, environment, version in the menu, report button, hang / crash reports
add('kit_freeze', P + 'kit_freeze.py', minutes=4)  # stack / hangdump under a held heap, cheat-console waits, faults inside the log / fault logger, hook churn (BUG-0050/0065/0115/0132; --old: every case fails)
# frames from the game loop when Present is lost (issue #4, the Steam overlay on Windows)
add('frames_loop', P + 'coop_frames_loop.py', ('smoke',), warm=True, minutes=2, fixture='creature')
add('exit_menu', P + 'coop_exit_menu.py', minutes=3)  # a guest exits to the main menu and comes back
add('host_away_kill', P + 'coop_host_away_kill.py', minutes=4)  # BUG-0082: the guest's kill list does not outlive the host's absence
add('npc_reuse', P + 'coop_npc_reuse.py', minutes=3)  # BUG-0044 / BUG-0023: a reused NPC object gets a new id (the guest's mirror is alive), ids never repeat
add('anim_teardown', P + 'coop_anim_teardown.py', minutes=8)  # BUG-0091: no animation on a puppet freed by a world teardown
add('join_new_game', P + 'coop_join_new_game.py', minutes=5)  # the host starts a new game (typed planet name) next to an old one: the guest gets the new one
add('tribe_play', P + 'coop_tribe_play.py', ('smoke',), warm=True, minutes=3, fixture='tribe')
add('civ_play', P + 'coop_civ_play.py', warm=True, minutes=3, fixture='civ_npc')
add('civ_reload', P + 'coop_civ_reload.py', warm=True, minutes=5, fixture='civ_npc')  # tables after a load and a leave (BUG-0040, BUG-0059; --nofix fails)
add('tribe_chief_removed', P + 'coop_tribe_chief_removed.py', warm=True, minutes=3, fixture='tribe')  # a chief removed at once (BUG-0081; --nofix fails)
add('tribe_gameover', P + 'coop_tribe_gameover.py', minutes=4)  # a member added during the game-over caption (BUG-0172; --nofix fails)
# two bots play Civilization for 10 minutes through real input (docs/stability/civ.md)
add('play_civ', P + 'play_civ.py --minutes 10', ('nightly',), minutes=13, fixture='civ_npc')
add('space_play', P + 'coop_space_play.py', ('smoke',), minutes=6)
add('space_page', P + 'coop_space_page.py', ('smoke',), minutes=4)  # the F9 Space page (space_ext.h), jump by a real click
add('space_map', P + 'coop_space_map.py', minutes=6)  # partners and their colonies on the galaxy map, the roster (space_map.h)
add('space_jump', P + 'coop_space_jump.py', minutes=7)  # jumps into unvisited systems: planets, first contact
add('space_death', P + 'coop_space_death.py', minutes=5)
add('space_guest_ui', P + 'coop_space_guest_ui.py', minutes=4)  # a guest joining during the menu intro: no movie left over, real clicks work
add('space_puppet', P + 'coop_space_puppet.py', minutes=5)  # the game frees partner puppets
add('space_rescue', P + 'coop_space_rescue.py', minutes=6)  # disabled by real (relayed) NPC hits
add('space_stale', P + 'coop_space_stale.py', warm=True, minutes=5, fixture='space')  # hits about freed mirrors / puppets, star ids past the table, the second repair's first step (BUG-0054, BUG-0092, BUG-0147)
add('space_contracts', P + 'coop_space_contracts.py', minutes=9)  # co-op contracts: armada, survey, timeout (F9 Space page clicks)
add('space_rivalry', P + 'coop_space_rivalry.py', minutes=8)  # rivals mode: own budgets, war and peace by real clicks, PvP
add('space_diplomacy', P + 'coop_space_diplomacy.py', minutes=5)  # shared relations with NPC empires
add('space_trade', P + 'coop_space_trade.py', minutes=4)  # tool gifts and spice sales by real clicks (space_trade.cpp)
# a partner's colonies as an allied empire in the guest's galaxy, its raid fought there, a clean save
add('space_allies', P + 'coop_space_allies.py', minutes=10)
add('space_finale', P + 'coop_space_finale.py', minutes=12)  # the galaxy core together (space_finale.cpp): wait, joint entry, Staff of Life, rejoin
add('space_finale_alone', P + 'coop_space_finale.py --alone', minutes=10)  # "Enter now" alone: the partner gets Steve's gift by message
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
