#!/usr/bin/env python3
"""Cell co-op ("Symbiosis") on two instances: one tide pool simulated by the host, guest mirrors,
guest bites applied by the host, shared food progress, knockout: revive by the partner or a small respawn next to it, regen."""
import re
import time

from twoinst import Checks, shot, start


def pool(con):
    out = con.call('coop.pool')['out']  # avatar line + the cell(...) status line only
    d = {}
    for k in ('hp', 'local_gameplay_cells', 'food', 'mirrors', 'hits_in', 'hits_sent', 'revives', 'respawns',
              'purged', 'downed'):
        m = re.search(k + r'=(-?\d+)', out)
        d[k] = int(m.group(1)) if m else None
    for k in ('size', 'target', 'body', 'partner_dist', 'revive_dist'):
        m = re.search(k + r'=(-?[\d.]+)', out)
        d[k] = float(m.group(1)) if m else None
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

    # knockout far from the partner: nobody saves it at once
    print(' ', guest.call('coop.pool tp 9 0')['out'])
    time.sleep(1)
    print(' ', pool(guest)[1].splitlines()[0])
    host.call('coop.pool hurt 1 50')
    time.sleep(3)
    g4, go4 = pool(guest)
    print(' ', go4.splitlines()[0])
    ck(g4['downed'] == 1 and g4['hp'] == 1, f'a lethal bite knocks the guest out (downed {g4["downed"]}, hp {g4["hp"]})')
    # the partner comes right next to it: revive
    host.call('coop.pool tp 1 0')  # the host swims right up to the knocked out guest
    for _ in range(8):
        time.sleep(0.5)
        host.call('coop.pool tp 1 0')
    time.sleep(1)
    g5, go5 = pool(guest)
    print(' ', go5.splitlines()[0])
    ck(g5['downed'] == 0 and g5['revives'] == 1 and g5['hp'] in (3, 4),  # 3, +1 regen next to the partner
       f'the partner right next to it revives it (revives {g5["revives"]}, hp {g5["hp"]})')
    ck(g5['body'] == 1.0, 'a revived cell keeps its size')
    ck('revived' in host.call('coop.status')['out'], 'everybody sees the revive')

    # knockout again, nobody comes: after 10 s a small respawn next to the partner
    size0 = g5['target']
    time.sleep(3)  # a revived cell is invulnerable for 2 s
    guest.call('coop.pool tp 9 0')
    time.sleep(1)
    host.call('coop.pool hurt 1 50')
    time.sleep(6)
    g6, _ = pool(guest)
    ck(g6['downed'] == 1 and g6['respawns'] == 0, 'still knocked out after 6 s')
    time.sleep(8)
    g7, go7 = pool(guest)
    print(' ', go7.splitlines()[0])
    ck(g7['downed'] == 0 and g7['respawns'] == 1 and g7['hp'] == 6, f'respawn after 10 s (respawns {g7["respawns"]}, hp {g7["hp"]})')
    ck(g7['partner_dist'] is not None and 0 <= g7['partner_dist'] < 6, f'next to the partner (distance {g7["partner_dist"]})')
    ck(g7['body'] == 0.5 and g7['target'] < size0 * 0.6, f'small again (target size {size0} -> {g7["target"]})')
    ck(g7['size'] < size0 * 0.75, f'the body really shrank (size {g7["size"]})')
    ck('respawned small' in host.call('coop.status')['out'], 'everybody sees the respawn')
    shot(guest, 'cell_small.png')
    guest.call('coop.pool food 20')
    time.sleep(1)
    g8, _ = pool(guest)
    ck(g8['body'] is not None and 0.65 < g8['body'] < 0.75, f'eating grows it back (body {g8["body"]})')

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
