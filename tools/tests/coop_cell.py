#!/usr/bin/env python3
"""Cell co-op ("Symbiosis") on two instances: one tide pool simulated by the host, guest mirrors,
guest bites applied by the host, shared food progress, rescue next to the partner, regen."""
import re
import time

from twoinst import Checks, shot, start


def pool(con):
    out = con.call('coop.pool')['out']  # avatar line + the cell(...) status line only
    d = {}
    for k in ('hp', 'local_gameplay_cells', 'food', 'mirrors', 'hits_in', 'hits_sent', 'rescues', 'purged'):
        m = re.search(k + r'=(-?\d+)', out)
        d[k] = int(m.group(1)) if m else None
    return d, out


def main():
    host, guest = start('cell', 'Game0/Karke')
    ck = Checks()
    time.sleep(5)
    h, ho = pool(host)
    g, go = pool(guest)
    print(' ', ho.strip().replace('\n', ' | '))
    print(' ', go.strip().replace('\n', ' | '))
    ck((g['mirrors'] or 0) > 0, f'guest mirrors the host pool ({g["mirrors"]} cells)')
    ck(g['local_gameplay_cells'] is not None and g['local_gameplay_cells'] <= (g['mirrors'] or 0) + 1,
       f'guest has no cells of its own ({g["local_gameplay_cells"]} local for {g["mirrors"]} mirrors)')

    print(' ', guest.call('coop.pool bite')['out'])
    time.sleep(1)
    h2, _ = pool(host)
    ck((h2['hits_in'] or 0) > (h['hits_in'] or 0), f'the host applied the guest bite (hits_in {h["hits_in"]} -> {h2["hits_in"]})')

    f0 = h2['food']
    guest.call('coop.pool food 20')
    time.sleep(2)
    h3, _ = pool(host)
    g3, _ = pool(guest)
    ck(h3['food'] == f0 + 20, f'shared food progress on the host ({f0} -> {h3["food"]})')
    ck(g3['food'] == h3['food'], f'guest has the same total ({g3["food"]})')

    host.call('coop.pool hurt 1 50')
    time.sleep(1.5)
    g4, _ = pool(guest)
    ck(g4['rescues'] == 1 and g4['hp'] == 6, f'guest saved next to the partner (rescues {g4["rescues"]}, hp {g4["hp"]})')
    ck('symbiosis' in host.call('coop.status')['out'], 'everybody sees the rescue')
    guest.call('coop.pool hp 3')
    time.sleep(7)
    g5, _ = pool(guest)
    ck((g5['hp'] or 0) > 3, f'regen next to the partner (hp 3 -> {g5["hp"]})')
    shot(host, 'cell_host.png')
    shot(guest, 'cell_guest.png')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
