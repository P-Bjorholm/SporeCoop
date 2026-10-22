#!/usr/bin/env python3
"""Selecting the other player's creature (a puppet) in the creature stage must not crash the game
(the pack panel read data puppets do not have)."""
import re
import time

from twoinst import Checks, host_frame, shot, start


def click_partner(con, who, ck):
    out = con.call('coop.team point')['out']
    print(f'  {who}:', out.strip())
    m = re.search(r'point (\d) (-?\d+) (-?\d+)', out)
    ok = m and m.group(1) == '1' and 0 < int(m.group(2)) < 1440 and 0 < int(m.group(3)) < 1080
    ck(ok, f'{who}: the partner is on the screen ({m.group(0) if m else None})')
    if not ok:
        return
    x, y = int(m.group(2)), int(m.group(3))
    g0 = int(re.search(r'guards=(\d+)', out).group(1))
    for dy in (-30, -60, -90, -120, -150, -10):  # the body is above its root point
        con.call(f'click {x} {y + dy}')
        time.sleep(1)
        g = int(re.search(r'guards=(\d+)', con.call('coop.team point')['out']).group(1))
        if g > g0:
            break
    print(f'  {who}: guarded selections {g0} -> {g}')
    fm = re.search(r'flags=([0-9A-F]{8}) e84=([0-9A-F]{8})', out)
    risky = fm and int(fm.group(1), 16) & 1 and int(fm.group(2), 16) == 0
    if risky:  # the state that crashed the game: the guard must have run
        ck(g > g0, f'{who}: the partner itself was selected (the guarded path ran)')
    else:
        print(f'  {who}: this partner has pack data, nothing to guard')
    f1 = host_frame(con)
    time.sleep(2)
    f2 = host_frame(con)
    ck(f2 > f1, f'{who}: still running after selecting the partner ({f1} -> {f2})')
    ck('total faults: 0' in con.call('exc')['out'], f'{who}: no fault')


def main():
    host, guest = start()
    ck = Checks()
    time.sleep(10)  # puppets with their own creatures are built
    click_partner(host, 'host', ck)
    shot(host, 'select_host.png')
    click_partner(guest, 'guest', ck)
    shot(guest, 'select_guest.png')
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
