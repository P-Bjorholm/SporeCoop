#!/usr/bin/env python3
"""Civilization: a second superweapon request during a running vote (BUG-0117).

1. The guest asks for superweapon type 3 (`coop.civ super 3`: the game's FireSuperweapon, hooked:
   the guest sends the request, the host opens the vote).
2. While that vote runs, the guest asks for type 9.
3. The host passes the vote (`coop.vote go`).
4. The weapon the host hands to the game's FireSuperweapon is type 3 (the vote's own), and the
   guest is told its second request was not taken. Before the fix the second request overwrote
   the statics the vote read: type 9 fired under the first vote.

--nofix: `coop.civ fix sw_vote 0` (the old statics): must FAIL.

  SPORE_LANE=7 python3 tools/tests/coop_civ_sw_vote.py [--nofix]
"""
import argparse
import re
import time

from twoinst import Checks, start


def sw(con):
    out = con.call('coop.civ sw')['out']
    return {k: int(v) for k, v in re.findall(r'(\w+)=(-?\d+)', out)}, out.strip()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--nofix', action='store_true')
    a = ap.parse_args()
    host, guest = start('civ')
    ck = Checks()
    if a.nofix:
        print(' ', host.call('coop.civ fix sw_vote 0')['out'].strip())
    time.sleep(3)
    print('  guest:', guest.call('coop.civ super 3')['out'].strip())
    time.sleep(2)
    v = host.call('coop.vote')['out']
    print('  host vote:', v.splitlines()[0] if v else v)
    ck('wants to fire a superweapon' in v and 'active' in v, 'the guest\'s request opened a vote')
    print('  guest:', guest.call('coop.civ super 9')['out'].strip())
    time.sleep(2)
    s, out = sw(host)
    print('  host:', out)
    ck(s.get('last_request') == 9, 'the host got the second request')
    host.call('coop.vote go')
    time.sleep(2)
    s, out = sw(host)
    print('  host after the vote:', out)
    ck(s.get('fired') == 3, 'the vote fired its own weapon (type 3), not the later request\'s')
    gst = guest.call('coop.status')['out']
    ck('another vote is running' in gst, 'the guest was told its second request was not taken')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
