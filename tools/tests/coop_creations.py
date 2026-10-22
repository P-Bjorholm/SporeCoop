#!/usr/bin/env python3
"""Every player keeps their own creature: the guest plays its newest own creation (My Creations of
its profile), the host keeps its world's species; each sees the other with the other's creature."""
import re
import time

from twoinst import Checks, shot, start


def field(out, pat):
    m = re.search(pat, out)
    return m.group(1) if m else None


def main():
    host, guest = start()
    ck = Checks()
    t0 = time.time()
    while time.time() - t0 < 30:
        gs = guest.call('coop.status')['out']
        if 'player 1 creature' in host.call('coop.creations')['out'] and 'avatar_swaps=1' in gs:
            break
        time.sleep(2)
    hc, gc = host.call('coop.creations')['out'], guest.call('coop.creations')['out']
    print('  host :', hc.replace('\n', ' | '))
    print('  guest:', gc.replace('\n', ' | '))
    g_mine = field(gc, r"mine creature: ([0-9A-F]{8}) '([^']*)'")
    g_name = field(gc, r"mine creature: [0-9A-F]{8} '([^']*)'")
    ck(g_mine and g_mine != '00000000', f'the guest picked its own creature: {g_mine} {g_name}')
    g_key_on_host = field(hc, r'player 1 creature: ([0-9A-F]{8})')
    ck(g_key_on_host is not None, f'the host received it: {g_key_on_host}')
    ga = guest.call('coop.avatar')['out']
    av = field(ga, r'key=0x([0-9A-F]{8})')
    ck(av == g_mine, f'the guest plays it (avatar species {av})')
    time.sleep(3)
    hs, gs = host.call('coop.status')['out'], guest.call('coop.status')['out']
    hp = field(hs, r"remote 1 'Bob'.*\n.*species=([0-9A-F]{8})")
    gp = field(gs, r"remote 0 'Alice'.*\n.*species=([0-9A-F]{8})")
    h_av = field(host.call('coop.avatar')['out'], r'key=0x([0-9A-F]{8})')
    print(f'  host avatar {h_av}, guest avatar {av}; host shows Bob as {hp}, guest shows Alice as {gp}')
    ck(hp == g_key_on_host, 'the host shows the guest with the guest creature')
    ck(gp is not None and gp != av, f'the guest shows the host with the host creature ({gp})')
    ck(h_av == gp, f'the host plays its world species and the guest sees it ({h_av})')
    time.sleep(8)  # a new creature is baked the first time it is shown
    shot(host, 'creations_host.png')
    shot(guest, 'creations_guest.png')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
