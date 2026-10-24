#!/usr/bin/env python3
"""Tool gifts and spice trade between players (space_trade.cpp, docs/re/space.md 12):

 1. the host has an Antimatter Bomb with 12 charges, the guest has none: a real click on the F9
    Space page ("Give 5", the text says "for Bob") moves 5 charges: the host has 7, the guest gets the tool with 5
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
    st = None
    for _ in range(3):  # (a key press can be lost while a caption or a toast takes the input)
        if ui.state().get('open') != '1':
            con.call(f'key {VK_F9}')
        st = ui.wait(lambda s: s['open'] == '1' and s['space']['vis'], 8, 'lobby with the Space button')
        if st is not None:
            break
    if st is None:
        return False
    ui.click('space')
    return ui.wait(lambda s: s['page'] == 'space', 10, 'Space page') is not None


def click_row(ui, con, text, pick=None, tries=8):
    """click the button of the row containing `text`; with `pick`, press "Next tool" until the
    gift row shows `pick` first"""
    for _ in range(tries):
        if pick:
            gift_n, gift_t = row_of(con, pick)
            if gift_n is None:
                nx, _ = row_of(con, 'more tools to give')
                if nx is None:
                    return False
                ui.wait(lambda s: s[f'sbtn{nx}']['vis'], 5, 'Next tool')
                ui.click(f'sbtn{nx}')
                continue
        n, t = row_of(con, (pick + ' x') if pick else text)
        if n is None or text not in t:
            return False
        ui.wait(lambda s: s[f'sbtn{n}']['vis'], 5, f'button of row {n}')
        print(f'  click row {n}: {t}')
        return ui.click(f'sbtn{n}')
    return False


def rivals(ck, host, guest, ui):
    """rivals mode: own budgets; the host offers spice at 1.5x, the guest accepts on its own page
    (a real click) and pays from its budget"""
    print('  rivals:', sp(host, 'rivalry mode rivals'))
    ck(wait(lambda: 'rivals=1' in sp(guest, 'rivalry') or 'mode rivals' in sp(guest, 'rivalry'), 20),
       'rivals mode is on for the guest')
    print('  guest rivalry:', sp(guest, 'rivalry').replace('\n', ' | ')[:200])
    print('  spice:', sp(host, 'trade spice 0 10'), sp(host, 'trade spice 1 10'))
    deal = sp(host, 'trade deal')
    print('  host deal:', deal)
    dm = re.search(r'spice (\S+) .* x(\d+) price (\d+)', deal)
    ck(dm is not None, 'rivals: the host can offer spice to the guest\'s colony')
    if not dm:
        return
    inst, n, price = dm.group(1), int(dm.group(2)), int(dm.group(3))
    h0, g0, c0, gc0 = money(host), money(guest), cargo(host, inst), cargo(guest, inst)
    print(f'  before: host money {h0} cargo {c0}, guest money {g0} cargo {gc0}; {n} x {price}')
    ck(click_row(ui, host, 'Offer'), 'the offer row is clicked')
    ck(cargo(host, inst) == c0 - n, f'the offered spice is held back ({cargo(host, inst)})')
    gui = Ui(guest, 'guest')
    ck(open_space_page(gui, guest), 'F9: the guest opens the Space page')
    ck(wait(lambda: row_of(guest, 'offers')[0] is not None, 15), 'the guest\'s page shows the offer')
    shot(guest, 'space_trade_offer.png')
    ck(click_row(gui, guest, 'offers'), 'the guest accepts by a click')
    total = n * price
    ck(wait(lambda: money(host) == h0 + total), f'the host got {total} ({money(host) - h0})')
    ck(wait(lambda: money(guest) == g0 - total), f'the guest paid {total} from its own budget ({money(guest) - g0})')
    ck(wait(lambda: cargo(guest, inst) == gc0 + n), f'the spice is in the guest\'s cargo ({cargo(guest, inst)})')
    gui.click('sback')
    guest.call(f'key {VK_F9}')
    # an offer left open when the host ends rivals mode is declined: the spice comes back
    print('  spice:', sp(host, 'trade spice 0 10'), sp(host, 'trade spice 1 10'))
    c1 = cargo(host, inst)
    print('  offer:', sp(host, 'trade sell'))
    ck(wait(lambda: 'offers=1' in sp(guest, 'trade')), 'a second offer waits on the guest')
    print('  allies:', sp(host, 'rivalry mode allies'))
    ck(wait(lambda: 'pending=0' in sp(host, 'trade') and cargo(host, inst) == c1), 
       f'back in allies mode the open offer is declined and the spice is back ({cargo(host, inst)} == {c1})')


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
    ck(click_row(ui, host, 'Give 5', pick='Anti-Matter Bomb'), 'the gift row of the bomb is clicked')
    shot(host, 'space_trade_gift.png')
    got = wait(lambda: ammo(guest) == 5)
    ck(got, f'the guest got the tool with 5 charges ({ammo(guest)})')
    ck(ammo(host) == 7, f'the host has 7 charges left ({ammo(host)})')

    # ---- 2. no room: the rest comes back ----
    print('  guest bomb:', sp(guest, 'trade ammo', BOMB, 97))
    ck(click_row(ui, host, 'Give 5', pick='Anti-Matter Bomb'), 'the gift row is clicked again')
    ck(wait(lambda: ammo(guest) == 99), f'the guest is full: 99 ({ammo(guest)})')
    ck(wait(lambda: ammo(host) == 5), f'3 charges came back to the host: 5 ({ammo(host)})')
    st = sp(host, 'trade')
    print('  host trade:', st)
    ck('charges_back=3' in st, 'the host counted 3 charges back')

    # ---- 3. a spice sale to the guest's colony ----
    star = int(re.search(r'star (\d+)', sp(host, 'star')).group(1))
    gstar = int(re.search(r'star (\d+)', sp(guest, 'star')).group(1))
    print(f'  host star {star}, guest star {gstar}')
    # the guest's colonies come from its roster (space_map.h): in the same save its home planet is
    # in this system; a colony does not buy its own spice, so the host carries two kinds
    print('  spice:', sp(host, 'trade spice 0 10'), sp(host, 'trade spice 1 10'))
    wait(lambda: 'deal ' in sp(host, 'trade deal'), 15)
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
        rivals(ck, host, guest, ui)
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
