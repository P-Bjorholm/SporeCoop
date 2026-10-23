"""The test saves ("fixtures", out/fixtures/<name>.tgz in the main checkout) and what each one
guarantees. twoinst.start() loads the planet given here and checks the facts, so a test that
needs something the save does not have fails with that reason. New fixtures come from
tools/tests/make_fixture.py (a base fixture + commands + save), never from the player's saves.

fact = (what, console command, regex that must match its output)

Both players' own creations are fixtures too: out/fixtures/creations-host.package and
creations-guest.package become the profiles' EditorSaves.package on every cold start.
"""

FIXTURES = {
    'creature': dict(planet='Game0/Scalinus', stage='Creature',
                     facts=[('the avatar plays', 'coop.avatar', r'Creature')]),
    'cell': dict(planet='Game0/Karke', stage='Cell',
                 facts=[('cells in the pool', 'coop.pool', r'local_gameplay_cells=[1-9]')]),
    'tribe': dict(planet='Game0/Cohrach', stage='Tribe',
                  facts=[('the player tribe has members', 'coop.tribe', r'player'),
                         ('another tribe exists', 'coop.tribe', r'tribe \d+(?! .*player)')]),
    'civ': dict(planet='Game0/Agrauel', stage='Civ',
                facts=[('a player city', 'coop.civ', r'city')]),
    # a new Civilization game (new_stage_game.py civ --npc-cities 3): the player city and three
    # cities of other nations (the nearest about 400 m away on a planet of radius 500)
    'civ_npc': dict(planet='Game0/Taldon', stage='Civ',
                    facts=[('a player city', 'coop.civ', r'city .*\(player\)'),
                           ('a city of another nation', 'coop.civ', r'city -?\d+ \S+ pol=[0-9A-F]+ owner=')]),
    'space': dict(planet='Game0/Maraya', stage='Space',
                  facts=[('in a star system', 'coop.space star', r'place 8')]),
}


def planet_of(name):
    f = FIXTURES.get(name)
    return f['planet'] if f else None


def check(con, name):
    """[(fact, ok)] for the loaded fixture"""
    import re
    out = []
    for what, cmd, rx in FIXTURES.get(name, {}).get('facts', []):
        try:
            text = con.call(cmd).get('out', '')
        except (OSError, ValueError):
            text = ''
        out.append((what, re.search(rx, text, re.S) is not None))
    return out
