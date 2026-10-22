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

    # the host swims around: its cell on the guest follows (position and facing)
    errs, rots, lags = [], [], []
    for i, (dx, dy) in enumerate([(25, 0), (0, 25), (-25, 0), (0, -25), (18, 18)]):
        host.call(f'coop.pool goto {dx} {dy}')
        for _ in range(6):
            time.sleep(0.4)
            hp_ = re.search(r'pos=\((-?[\d.]+) (-?[\d.]+)\)', host.call('coop.pool')['out'])
            st = guest.call('coop.status')['out']
            m = re.search(r"remote 0 'Alice'.*\n\s+puppet pos=\((-?[\d.]+) (-?[\d.]+) [^)]*\) err=([\d.]+) rot_err=(-?[\d.]+)", st)
            if m and hp_:
                errs.append(float(m.group(3)))
                rots.append(float(m.group(4)))
                lags.append(((float(m.group(1)) - float(hp_.group(1))) ** 2 + (float(m.group(2)) - float(hp_.group(2))) ** 2) ** 0.5)
            if i == 1 and _ == 3:
                shot(guest, 'cell_sync_guest.png')
                shot(host, 'cell_sync_host.png')
    print(f'  samples {len(errs)}: puppet vs remote state max {max(errs or [0]):.2f}, facing max {max(rots or [0]):.1f} deg, '
          f'vs the real host cell avg {sum(lags) / max(1, len(lags)):.2f} max {max(lags or [0]):.2f}')
    ck(len(errs) >= 20, 'sync samples taken')
    ck(max(errs or [9]) < 0.3, f'the partner cell is exactly where its state says (max {max(errs or [9]):.2f})')
    ck(max(rots or [999]) < 10, f'and faces the same way (max {max(rots or [999]):.1f} deg)')
    ck(sum(lags) / max(1, len(lags)) < 2.0, f'close to the real host cell (avg {sum(lags) / max(1, len(lags)):.2f})')

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
    ck('avatar_opacity=0.35' in go4, 'the knocked out cell is translucent for its owner')
    hk = host.call('coop.pool')['out']
    m = re.search(r'ko\[1\]=(\d) left=([\d.]+) hold=([\d.]+) op=([\d.-]+)', hk)
    print(' ', m.group(0) if m else hk)
    ck(m and m.group(1) == '1' and 0 < float(m.group(2)) < 10, 'the host sees the guest knocked out with a countdown')
    ck(m and m.group(4) == '0.35', 'and its cell translucent')
    shot(host, 'ko_host.png')
    shot(guest, 'ko_guest.png')
    # the partner comes right next to it: revive
    host.call('coop.pool tp 1 0')  # the host swims right up to the knocked out guest
    seen_hold = 0.0
    for i in range(8):
        time.sleep(0.5)
        host.call('coop.pool tp 1 0')
        m = re.search(r'ko\[1\]=1 left=[\d.]+ hold=([\d.]+)', host.call('coop.pool')['out'])
        if m and float(m.group(1)) > seen_hold:
            seen_hold = float(m.group(1))
            if seen_hold > 0.3 and seen_hold < 0.95:
                shot(host, 'ko_revive_host.png')
    ck(seen_hold > 0.1, f'the host sees the revive progress ({seen_hold:.2f})')
    time.sleep(1)
    g5, go5 = pool(guest)
    print(' ', go5.splitlines()[0])
    ck(g5['downed'] == 0 and g5['revives'] == 1 and 3 <= (g5['hp'] or 0) <= 5,  # 3, +1 regen every 3 s near the partner
       f'the partner right next to it revives it (revives {g5["revives"]}, hp {g5["hp"]})')
    ck(g5['body'] == 1.0, 'a revived cell keeps its size')
    ck('revived' in host.call('coop.status')['out'], 'everybody sees the revive')

    # knockout again, nobody comes: after 10 s a small respawn next to the partner
    size0 = g5['target']
    time.sleep(3)  # a revived cell is invulnerable for 2 s
    guest.call('coop.pool tp 60 0')  # off the host's screen: an arrow on the edge
    time.sleep(1)
    host.call('coop.pool hurt 1 50')
    time.sleep(3)
    shot(host, 'ko_arrow_host.png')
    time.sleep(3)
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
    shot(host, 'cell_small_host.png')
    hk = host.call('coop.pool')['out']
    ck(re.search(r'ko\[1\]=3 .* op=1.00', hk) is not None, 'the host sees it up again (opaque)')
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
