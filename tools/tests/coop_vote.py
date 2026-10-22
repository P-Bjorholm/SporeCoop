#!/usr/bin/env python3
"""Evolution vote on two instances: the host's editor request waits until every player is
ready, then the editor opens; afterwards the guests get the evolved world."""
import re
import time

from coop_ui import Ui
from twoinst import Checks, shot, start


def mode(con):
    return con.call('game.mode')['out']


def main():
    host, guest = start()
    ck = Checks()
    hu, gu = Ui(host, 'host'), Ui(guest, 'guest')
    time.sleep(2)

    # 1. a vote without an editor: the guest's "I'm ready" ends it
    print(' ', host.call('coop.vote test')['out'])
    st = gu.wait(lambda s: s['open'] == '1' and 'wants to evolve' in s.get('world', ''), 15, 'vote on the guest')
    ck(st is not None, f'the guest sees the vote: {st.get("world") if st else None}')
    gu.click('ready')
    time.sleep(2)
    ck('vote none' in host.call('coop.vote')['out'], 'the vote ends when everybody is ready')
    ck('evolution starts (everybody is ready)' in host.call('coop.status')['out'], 'everybody is told')

    # 2. a real editor request (creature editor, as ModAPI mods open it)
    m0 = mode(host)
    print(' ', host.call('coop.vote editor')['out'].strip())
    time.sleep(2)
    ck('vote active' in host.call('coop.vote')['out'], 'the editor request started a vote')
    ck(mode(host) == m0, f'the host waits in its stage ({mode(host)})')
    gu.wait(lambda s: s['ready']['vis'], 10, 'ready button')
    gu.click('ready')
    t0 = time.time()
    while time.time() - t0 < 30 and mode(host) == m0:
        time.sleep(1)
    print('  host mode after the vote:', mode(host))
    ck(mode(host) != m0, 'the editor opened after the vote')
    host.call('wait.frames 200 60000')
    shot(host, 'vote_editor.png')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
