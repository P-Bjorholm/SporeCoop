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
    ck(hu.state().get('open') == '0', 'the co-op window closed itself after the vote')
    shot(host, 'vote_editor.png')
    gs = guest.call('coop.uistate 1')['out']
    time.sleep(2)
    ck('evolving the species in the editor' in guest.call('coop.uistate')['out'], 'the guest sees the host evolving')
    guest.call('coop.uistate 0')

    # 3. leaving the editor: the evolved species goes to everybody
    host.call('click 1416 1062')  # the green "done" check of the editor
    t0 = time.time()
    while time.time() - t0 < 30 and mode(host) != m0:
        time.sleep(1)
    ck(mode(host) == m0, 'the host is back in its stage')
    t0 = time.time()
    got = False
    while time.time() - t0 < 120 and not got:
        got = 'evolved the species' in guest.call('coop.status')['out'] and guest.call('coop.avatar')['ok']
        time.sleep(2)
    time.sleep(20)
    st = guest.call('coop.status')['out']
    ck('evolved the species' in st, 'everybody is told about the evolution')
    ck(re.search(r"remote 0 'Alice' puppet=[0-9A-F]{8} ", st) and 'puppet=00000000' not in st.split("remote 0")[1][:40],
       'the guest got the evolved world and sees the host again')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
