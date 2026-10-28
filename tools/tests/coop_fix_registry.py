#!/usr/bin/env python3
"""coop.fix: one switch command over every bug fix's test switch (BUG-0116).

One game of the lane, at the galaxy menu:
  * `coop.fix list` names every switch registered in the sources (COOP_FIX lines), all on;
  * `coop.fix all off` turns every one off, `coop.fix all on` back on (the list shows it);
  * a switch and its old command see the same bool (`coop.pool fix`, `coop.npc strays`,
    `coop.worldfix`, `coop.corefix`), in both directions;
  * an unknown name is refused.

  SPORE_LANE=6 python3 tools/tests/coop_fix_registry.py"""
import glob
import os
import re
import sys

import lane
from diag_check import start_host
from twoinst import Checks

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def source_names():
    names = []
    for p in sorted(glob.glob(os.path.join(ROOT, 'mods', 'coop', '*.cpp'))):
        names += re.findall(r'COOP_FIX\("([^"]+)"', open(p, encoding='utf-8').read())
    return names


def states(c):
    out = c.call('coop.fix list')['out']
    st = {}
    for line in out.splitlines():
        m = re.match(r'^(\S+\.\S+) (on|off) ', line)
        if m:
            st[m.group(1)] = m.group(2) == 'on'
    return out, st


def main():
    ck = Checks()
    names = source_names()
    print(f'  {len(names)} switches in the sources')
    ck(len(names) >= 50, 'the sources register the fix switches (COOP_FIX)')
    c = start_host(ck)
    try:
        out, st = states(c)
        print('  ' + out.splitlines()[0] if out else '  (no answer)')
        ck(set(st) == set(names), f'coop.fix list shows every registered switch ({len(st)} of {len(names)}; '
           f'missing {sorted(set(names) - set(st))[:5]})')
        ck('refused' not in out.splitlines()[0], 'no refused (duplicate) registration')
        ck(st and all(st.values()), 'every fix is on by default')

        ck('all %d fixes off' % len(names) in c.call('coop.fix all off')['out'], 'coop.fix all off answers')
        _, st = states(c)
        ck(st and not any(st.values()), 'after all off: every switch off')
        # the old commands read the same bools
        ck('purge=0' in c.call('coop.pool fix')['out'], 'coop.pool fix shows purge off')
        ck('busy_slot 0' in c.call('coop.worldfix')['out'], 'coop.worldfix shows busy_slot off')
        ck('vote_scope 0' in c.call('coop.corefix')['out'], 'coop.corefix shows vote_scope off')

        ck('all %d fixes on' % len(names) in c.call('coop.fix all on')['out'], 'coop.fix all on answers')
        _, st = states(c)
        ck(st and all(st.values()), 'after all on: every switch on again')
        ck('purge=1' in c.call('coop.pool fix')['out'], 'coop.pool fix shows purge on')

        # one switch, both ways
        c.call('coop.npc strays off')
        _, st = states(c)
        ck(st.get('npc.strays') is False, 'coop.npc strays off shows in coop.fix')
        c.call('coop.fix npc.strays on')
        c.call('coop.fix pool.hostof off')
        ck('hostof=0' in c.call('coop.pool fix')['out'], 'coop.fix pool.hostof off shows in coop.pool fix')
        c.call('coop.pool fix hostof 1')
        _, st = states(c)
        ck(st.get('npc.strays') is True and st.get('pool.hostof') is True, 'both back on')

        r = c.call('coop.fix no.such off')
        ck('unknown fix' in r['out'], 'an unknown name is refused')
        _, st = states(c)
        ck(st and all(st.values()), 'every switch on at the end')
    finally:
        c.call('coop.fix all on')
        lane.sh('kill', check=False)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
