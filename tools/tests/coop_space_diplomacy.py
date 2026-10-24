#!/usr/bin/env python3
"""Shared diplomacy (space_diplomacy.cpp, docs/re/space.md 10): both players are one people for the
NPC empires of the galaxy.

1. Both copies of the galaxy have the same empires with the same political ids and the same
   relation table (the guest loaded the host's save).
2. The guest flies to an NPC empire's home star: first contact, and the host's copy has met it too.
3. The guest gives that empire a gift (ApplyRelationship with the gift event, as the comm screen
   does): the host's relation with it changes the same way. The host forms an alliance with it: the
   guest's copy is allied too.
4. The reverse direction: the host makes contact with a second empire and declares war on it, the
   guest's copy is at war; the guest makes peace, the host's copy is at peace.
5. The copies drift apart (a local change that is not shared): a real click on the F9 Space page
   "Resync" button brings the guest back to the host's table.
6. The guest leaves and joins again: the same table.

  SPORE_LANE=4 python3 tools/tests/coop_space_diplomacy.py
"""
import os
import re
import subprocess
import tarfile
import time

import fixtures
import lane
from coop_ui import VK_F9, Ui
from twoinst import Checks, ROOT, shot, start, wait_guest_in

GIFT = '526E51F'


def sp(con, *a):
    return con.call('coop.space ' + ' '.join(str(x) for x in a))['out'].strip()


def table(con):
    """{pol: 'flags=.. ev=..'} of the empires with events or a state (the relation value is left out:
    the game computes it from the events)"""
    t = {}
    for line in sp(con, 'diplomacy table').splitlines():
        m = re.match(r'rec (\w+) flags=(\d+) rel=\S+ ?(.*)', line)
        if m:
            t[m.group(1)] = f'flags={m.group(2)} {m.group(3)}'
    return t


def rec(con, pol):
    return table(con).get(pol, '')


def flags(con, pol):
    m = re.match(r'flags=(\d+)', rec(con, pol))
    return int(m.group(1)) if m else 0


def wait(pred, timeout=20):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if pred():
            return True
        time.sleep(0.5)
    return False


def space_planet():
    """the planet of the space fixture (fixtures.py, or the save inside out/fixtures/space.tgz)"""
    p = fixtures.planet_of('space')
    fx = os.path.join(os.path.dirname(os.path.dirname(subprocess.run(
        ['git', 'rev-parse', '--path-format=absolute', '--git-common-dir'], cwd=ROOT, capture_output=True,
        text=True).stdout.strip())), 'SporeCoop', 'out', 'fixtures', 'space.tgz')
    try:
        names = tarfile.open(fx).getnames()
    except (OSError, tarfile.TarError):
        return p
    spo = [n for n in names if n.endswith('.spo')]
    if p and any(n.endswith(p + '.spo') for n in names) or not spo:
        return p
    g = spo[0].split('/')
    return f'{g[-2]}/{g[-1][:-4]}'


def near(con):
    """[(pol, home star)] of the NPC empires nearest to our home"""
    return re.findall(r'near (\w+) .* home=(\d+)', sp(con, 'diplomacy near 8'))


def contact(con, other, pol, home, ck, who):
    for _ in range(5):  # a badge or caption cinematic blocks the jump: confirm it
        r = sp(con, 'jumpto', home)
        print(f'  {who} flies to {pol} at star {home}:', r)
        if 'cinematic' not in r:
            break
        con.call('key enter')
        time.sleep(2)
    met = wait(lambda: flags(con, pol) & 16, 40)
    ck(met, f'{who}: first contact with {pol} on arriving at its star')
    ok = wait(lambda: flags(other, pol) & 16, 15)
    ck(ok, f'the partner\'s copy has met {pol} too ({rec(other, pol)})')
    return met and ok


def same(host, guest, ck, what):
    ok = wait(lambda: table(host) == table(guest), 15)
    th, tg = table(host), table(guest)
    if not ok:
        for k in sorted(set(th) | set(tg)):
            if th.get(k) != tg.get(k):
                print(f'    {k}: host {th.get(k)} | guest {tg.get(k)}')
    ck(ok, f'{what}: the same relation table on both ({len(th)} empires with events)')
    return ok


def main():
    host, guest = start('space', planet=space_planet())
    ck = Checks()
    time.sleep(4)
    for who, c in (('host', host), ('guest', guest)):
        ck('diplomacy: on' in sp(c, 'features'), f'{who}: the diplomacy feature is on')
    hl, gl = sp(host, 'diplomacy list').splitlines()[0], sp(guest, 'diplomacy list').splitlines()[0]
    print('  host:', hl, '\n  guest:', gl)
    hm = re.match(r'player (\w+) .* empires (\d+)', hl)
    gm = re.match(r'player (\w+) .* empires (\d+)', gl)
    ck(hm and gm and hm.groups() == gm.groups(),
       'the same player political id and number of empires in both copies')
    ck(near(host) == near(guest), 'the same NPC empires (political ids, home stars) near home in both copies')
    same(host, guest, ck, 'after the join')

    n = near(guest)
    if not ck(len(n) >= 2, 'two NPC empires near home'):
        return ck.result()
    (p1, h1), (p2, h2) = n[1], n[0]

    # 2-3: the guest meets an empire and gives it a gift, the host allies with it
    contact(guest, host, p1, h1, ck, 'guest')
    before = rec(host, p1)
    print('  guest gift:', sp(guest, 'diplomacy apply', p1, GIFT, 1))
    ok = wait(lambda: GIFT in rec(host, p1) and rec(host, p1) == rec(guest, p1))
    print(f'  host {p1}: {before} -> {rec(host, p1)}')
    ck(ok, 'the guest\'s gift changed the host\'s relation the same way')
    print('  host alliance:', sp(host, 'diplomacy ally', p1))
    ck(wait(lambda: flags(guest, p1) & 2), f'the host\'s alliance with {p1} holds in the guest\'s copy')
    log = sp(guest, 'diplomacy log')
    ck('got state 2 from Alice' in log, 'the guest got a notice of the alliance')
    same(host, guest, ck, 'after gift and alliance')

    # 4: the other direction, war and peace
    contact(host, guest, p2, h2, ck, 'host')
    print('  host war:', sp(host, 'diplomacy war', p2))
    ck(wait(lambda: flags(guest, p2) & 1), f'the host\'s war on {p2} holds in the guest\'s copy')
    print('  guest peace:', sp(guest, 'diplomacy peace', p2))
    ck(wait(lambda: not flags(host, p2) & 1), f'the guest\'s peace with {p2} holds in the host\'s copy')
    same(host, guest, ck, 'after war and peace')

    # 5: drift, then the "Resync" button of the Space page (a real click)
    print('  guest local gift (not shared):', sp(guest, 'diplomacy local apply', p1, GIFT, 1))
    ck(rec(guest, p1) != rec(host, p1), 'a local change (not shared) makes the copies differ')
    rows = sp(guest, 'rows')
    print('  guest rows:', rows.replace('\n', ' | '))
    idx = next((int(m.group(1)) for m in re.finditer(r'^(\d+): .*\| (Resync|Сверить)$', rows, re.M)), -1)
    ck(idx >= 0, 'the guest\'s Space page has the "Resync" button')
    ui = Ui(guest, 'guest')
    if ui.state().get('open') != '1':
        guest.call(f'key {VK_F9}')
    st = ui.wait(lambda s: s['open'] == '1' and s['space']['vis'], 20, 'lobby with the Space button')
    if st:
        ui.click('space')
        st = ui.wait(lambda s: s['page'] == 'space' and s[f'sbtn{idx}']['vis'], 10, 'Space page')
    ck(st is not None and 'Resync' in st[f'sbtn{idx}']['text'], 'the Space page shows "Resync"')
    shot(guest, 'space_diplomacy_page.png')
    ui.click(f'sbtn{idx}')
    if not ck(wait(lambda: rec(guest, p1) == rec(host, p1), 15), 'Resync: the guest has the host\'s relation again'):
        print('  guest log:', sp(guest, 'diplomacy log').splitlines()[-4:])
        print('  host log:', sp(host, 'diplomacy log').splitlines()[-4:])
    ui.click('sback')
    guest.call(f'key {VK_F9}')
    same(host, guest, ck, 'after Resync')

    # 6: rejoin
    guest.call('coop.leave')
    time.sleep(2)
    guest.call(lane.JOIN)
    time.sleep(5)
    ck(wait_guest_in(host, guest, 150), 'the guest joined again')
    time.sleep(4)
    same(host, guest, ck, 'after the rejoin')
    ck(flags(guest, p1) & 2 and flags(guest, p1) & 16, 'after the rejoin the guest is still allied with ' + p1)
    for who, c in (('host', host), ('guest', guest)):
        print(f'  {who}:', sp(c, 'diplomacy'))
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
