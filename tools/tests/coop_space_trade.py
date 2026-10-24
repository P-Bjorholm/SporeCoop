#!/usr/bin/env python3
"""Tool gifts and spice trade between players (space_trade.cpp, docs/re/space.md 10):

 1. the host has an Antimatter Bomb with 12 charges, the guest has none: a real click on the F9
    Space page ("Give 5 to Bob") moves 5 charges: the host has 7, the guest gets the tool with 5
 2. no room: the guest's bombs are at 97 of 99, the host gives 5 again: the guest keeps 2 and 3
    come back to the host (7 - 5 + 3 = 5)
 3. spice sale: at a star where the guest has a colony the host's page offers "Sell 10" at twice the
    market value; a real click sells: the money of both games (one shared budget) grows by the
    price, the 10 spice leave the host's cargo and arrive in the guest's cargo
 4. rivals mode (own budgets, the partner pays after accepting): only when space_rivalry is in
    the build (feature 10), else reported as untested

  SPORE_LANE=10 python3 tools/tests/coop_space_trade.py
"""
import re
import time

from coop_ui import VK_F9, Ui
from twoinst import Checks, shot, start

BOMB = '144DF50A'  # Antimatter Bomb (spacetools~ 0x144DF50A: uses ammo, 5 initial, 99 max)


def sp(con, *a):
    return con.call('coop.space ' + ' '.join(str(x) for x in a))['out'].strip()


def ammo(con, inst=BOMB):
    m = re.search(r'tool %s \S+ \S+ \'[^\']*\' ammo=(-?\d+)' % inst, sp(con, 'trade all'))
    return int(m.group(1)) if m else None


def money(con):
    m = re.search(r'money (-?\d+)', sp(con, 'trade money'))
    return int(m.group(1)) if m else None


def cargo(con, inst):
    m = re.search(r'cargo %s \'[^\']*\' x(\d+)' % inst, sp(con, 'trade spices'))
    return int(m.group(1)) if m else 0


def wait(pred, timeout=20):
    t0 = time.time()
    while time.time() - t0 < timeout:
        v = pred()
        if v:
            return v
        time.sleep(0.5)
    return pred()


def row_of(con, text):
    """the Space page row (= its button sbtn<n>) whose text contains `text`"""
    for line in sp(con, 'rows').splitlines():
        m = re.match(r'(\d+): (.*)', line)
        if m and text in m.group(2):
            return int(m.group(1)), m.group(2)
    return None, None


def open_space_page(ui, con):
    if ui.state().get('open') != '1':
        con.call(f'key {VK_F9}')
    st = ui.wait(lambda s: s['open'] == '1' and s['space']['vis'], 20, 'lobby with the Space button')
    if st is None:
        return False
    ui.click('space')
    return ui.wait(lambda s: s['page'] == 'space', 10, 'Space page') is not None


def click_row(ui, con, text, pick=None, tries=8):
    """click the button of the row containing `text`; with `pick`, press "Next tool" until the
    gift row shows `pick` first"""
    for _ in range(tries):
        if pick:
            n, t = row_of(con, 'x')
            gift_n, gift_t = row_of(con, pick)
            if gift_n is None:
                nx, _ = row_of(con, 'more tools to give')
                if nx is None:
                    return False
                ui.wait(lambda s: s[f'sbtn{nx}']['vis'], 5, 'Next tool')
                ui.click(f'sbtn{nx}')
                continue
        n, t = row_of(con, text)
        if n is None:
            return False
        ui.wait(lambda s: s[f'sbtn{n}']['vis'], 5, f'button of row {n}')
        print(f'  click row {n}: {t}')
        return ui.click(f'sbtn{n}')
    return False


def main():
    host, guest = start('space')
    ck = Checks()
    time.sleep(5)
    for who, c in (('host', host), ('guest', guest)):
        f = sp(c, 'features')
        ck('trade: gifts_sent=' in f, f'{who}: the trade feature is on')
    gid = int(re.search(r'id=(\d+)', guest.call('coop.status')['out']).group(1)) \
        if re.search(r'id=(\d+)', guest.call('coop.status')['out']) else 1

    # ---- 1. a tool gift by a real click ----
    print('  host bomb:', sp(host, 'tool add', '0x' + BOMB), '|', sp(host, 'trade ammo', BOMB, 12))
    ck(ammo(host) == 12, f'the host has 12 Antimatter Bomb charges ({ammo(host)})')
    ck(ammo(guest) is None, f'the guest has no Antimatter Bomb ({ammo(guest)})')
    print('  host tools:', sp(host, 'trade tools').replace('\n', ' | '))
    ui = Ui(host, 'host')
    ck(open_space_page(ui, host), 'F9: the host opens the Space page')
    print('  host rows:', sp(host, 'rows').replace('\n', ' | '))
    ck(click_row(ui, host, 'Give 5 to', pick='Antimatter'), 'the gift row of the bomb is clicked')
    shot(host, 'space_trade_gift.png')
    got = wait(lambda: ammo(guest) == 5)
    ck(got, f'the guest got the tool with 5 charges ({ammo(guest)})')
    ck(ammo(host) == 7, f'the host has 7 charges left ({ammo(host)})')

    # ---- 2. no room: the rest comes back ----
    print('  guest bomb:', sp(guest, 'trade ammo', BOMB, 97))
    ck(click_row(ui, host, 'Give 5 to', pick='Antimatter'), 'the gift row is clicked again')
    ck(wait(lambda: ammo(guest) == 99), f'the guest is full: 99 ({ammo(guest)})')
    ck(wait(lambda: ammo(host) == 5), f'3 charges came back to the host: 5 ({ammo(host)})')
    st = sp(host, 'trade')
    print('  host trade:', st)
    ck('charges_back=3' in st, 'the host counted 3 charges back')

    # ---- 3. a spice sale to the guest's colony ----
    star = int(re.search(r'star (\d+)', sp(host, 'star')).group(1))
    gstar = int(re.search(r'star (\d+)', sp(guest, 'star')).group(1))
    print(f'  host star {star}, guest star {gstar}')
    if 'roster=stand-in' in st:
        # the roster of space_map.h is not in this build: tell the host where the guest's colony is
        print('  (stand-in roster)', sp(host, 'trade colony', gid, star, '%08X' % (star | 0x1000000)))
    spices = sp(host, 'trade spices')
    m = re.search(r'spice 0 (\S+) .*price=(\d+)', spices)
    inst, price = m.group(1), int(m.group(2))
    print('  spice:', sp(host, 'trade spice 0 10'))
    deal = sp(host, 'trade deal')
    print('  host deal:', deal)
    dm = re.search(r'spice (\S+) .* x(\d+) price (\d+)', deal)
    ck(dm is not None, 'the host can sell spice to the guest\'s colony here')
    if dm:
        inst, n, price = dm.group(1), int(dm.group(2)), int(dm.group(3))
        h0, g0, c0, gc0 = money(host), money(guest), cargo(host, inst), cargo(guest, inst)
        print(f'  before: host money {h0} cargo {c0}, guest money {g0} cargo {gc0}; {n} x {price}')
        ck(click_row(ui, host, 'buys'), 'the sale row is clicked')
        shot(host, 'space_trade_sale.png')
        total = n * price
        ck(wait(lambda: money(host) == h0 + total), f'the host money grew by {total} ({money(host) - h0})')
        ck(wait(lambda: money(guest) == g0 + total, 15), f'the shared money reached the guest: +{money(guest) - g0}')
        ck(cargo(host, inst) == c0 - n, f'the spice left the host\'s cargo ({cargo(host, inst)})')
        ck(wait(lambda: cargo(guest, inst) == gc0 + n), f'the spice is in the guest\'s cargo ({cargo(guest, inst)})')

    # ---- 4. rivals mode ----
    if 'rivalry: ' in sp(host, 'features'):
        print('  rivals mode: space_rivalry is in the build: TODO when feature 10 lands')
    else:
        print('  rivals mode: UNTESTED (space_rivalry.cpp, feature 10, is not in this build)')
    ui.click('sback')
    host.call(f'key {VK_F9}')
    print('  host trade:', sp(host, 'trade'))
    print('  guest trade:', sp(guest, 'trade'))
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
