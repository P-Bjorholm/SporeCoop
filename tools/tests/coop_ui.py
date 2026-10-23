#!/usr/bin/env python3
"""UI test: the whole co-op flow through the native Spore UI, two instances.

host (instance 1, profile test, creature save) and guest (instance 2, empty profile):
 1. both: the "Co-op" item is in the main menu; clicking it opens the co-op window
 2. host: types the name, presses Host, closes the window and loads the saved game
 3. guest: types the name, the host appears under "Games on your network", a click joins
 4. the guest receives and loads the host world
 5. in game, F9 opens the lobby on both: two players, stage, ping
 6. guest presses "I'm ready" -> the host sees it; chat line from the guest -> host
 7. guest presses "Leave" -> the host lobby has one player again
Screenshots: out/shots/ui_*.png

usage: coop_ui.py [--no-restart]
"""
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
from sporectl import Console, recompress_png  # noqa: E402
import lane  # noqa: E402

VK_END, VK_BACK, VK_RETURN, VK_F9 = 'end', 'back', 'enter', 'f9'  # devkit key names


def sh(*args):
    lane.sh(*args)


def wait_up(inst, t=120):
    t0 = time.time()
    while time.time() - t0 < t:
        try:
            if Console(inst).call('ping')['ok']:
                return True
        except OSError:
            pass
        time.sleep(1)
    return False


def shot(con, name):
    path = os.path.join(lane.out_dir('shots'), name)
    if con.call('shot ' + path)['ok']:
        recompress_png(path, 0.5)
    return path


class Ui:
    """coop.uistate parser + clicks on named widgets"""

    def __init__(self, con, who):
        self.c, self.who = con, who

    def state(self):
        st = {'rows': [], 'chat': []}
        for line in self.c.call('coop.uistate')['out'].splitlines():
            if line.startswith('open='):
                for kv in line.split():
                    k, v = kv.split('=')
                    st[k] = v
                continue
            parts = line.split(' ', 1)
            if len(parts) < 2:
                continue
            key, rest = parts
            if key == 'row':
                st['rows'].append([p for p in rest.split("'")[1::2]])
            elif key == 'chat':
                st['chat'].append(rest.strip("'"))
            elif key in ('session', 'lan', 'status', 'world', 'creation'):
                st[key] = rest.strip("'")
            else:
                f = rest.split(' ', 5)
                if len(f) >= 5:
                    st[key] = {'rect': [int(float(x)) for x in f[:4]], 'vis': f[4] == '1',
                               'text': f[5].strip("'") if len(f) > 5 else ''}
        return st

    def wait(self, pred, timeout=30, what=''):
        t0 = time.time()
        while time.time() - t0 < timeout:
            st = self.state()
            try:
                if pred(st):
                    return st
            except (KeyError, IndexError, TypeError):
                pass
            time.sleep(0.5)
        print(f'  {self.who}: timeout waiting for {what}')
        return None

    def click(self, name):
        st = self.state()
        w = st.get(name)
        if not w or not w['vis']:
            print(f'  {self.who}: {name} not visible')
            return False
        x1, y1, x2, y2 = w['rect']
        self.c.call(f'click {(x1 + x2) // 2} {(y1 + y2) // 2}')
        self.c.call('wait.frames 12 5000')
        return True

    def type_into(self, name, text):
        if not self.click(name):
            return False
        self.c.call(f'key {VK_END}')
        for _ in range(30):
            self.c.call(f'key {VK_BACK}')
        self.c.call('wait.frames 10 5000')
        self.c.call('text "' + text + '"')
        self.c.call('wait.frames 10 5000')
        return True


def main():
    restart = '--no-restart' not in sys.argv
    if restart:
        sh('kill')
        time.sleep(2)
        sh('deploy')
        sh('fixture', 'restore', 'creature', lane.HOST_PROFILE)
        subprocess.run(['rm', '-rf', lane.GUEST_DIR + '/Games', lane.GUEST_DIR + '/Games-sporecoop-backup'])
        lane.run_guest()
        assert wait_up(lane.GUEST), f'instance {lane.GUEST} did not start'
        time.sleep(5)
        lane.run_host()
        assert wait_up(lane.HOST), f'instance {lane.HOST} did not start'
    hc, gc = Console(lane.HOST), Console(lane.GUEST)
    host, guest = Ui(hc, 'host'), Ui(gc, 'guest')
    ok = True

    def check(cond, msg):
        nonlocal ok
        print(('  ok   ' if cond else '  FAIL ') + msg)
        ok &= bool(cond)

    for con in (hc, gc):
        for _ in range(180):
            if 'GalaxyGameEntry' in con.call('game.mode')['out']:
                break
            time.sleep(1)
        con.call('wait.frames 700 60000')
    for ui in (host, guest):
        st = ui.wait(lambda s: s['menu']['vis'], 30, 'menu item')
        check(st is not None, f'{ui.who}: "Co-op" item in the main menu')
        ui.click('menu')
        st = ui.wait(lambda s: s['open'] == '1' and s['join']['vis'], 20, 'co-op window')
        check(st is not None, f'{ui.who}: the co-op window opens')
    shot(hc, 'ui_connect.png')

    # host
    host.type_into('name', 'Alice')
    host.click('host')
    st = host.wait(lambda s: s['page'] == 'lobby' and s['rows'], 15, 'host lobby')
    check(st and 'Alice' in st['rows'][0][0], f'host: hosting, lobby shows {st["rows"] if st else None}')
    shot(hc, 'ui_host_lobby.png')
    host.click('close')
    check(host.wait(lambda s: s['open'] == '0', 10, 'closed') is not None, 'host: the window closes')

    # guest: discovery + join
    guest.type_into('name', 'Bob')
    # our host among the LAN rows (other lanes' hosts on this machine are listed too)
    ours = lambda s: next((r for r in ('lan0', 'lan1', 'lan2') if s[r]['vis'] and 'Alice' in s[r]['text']  # noqa: E731
                           and f':{lane.PORT}' in s[r]['text']), None)
    st = guest.wait(lambda s: ours(s) is not None, 30, 'LAN game')
    row = ours(st) if st else None
    check(row is not None, f'guest: host found on the network: {st[row]["text"] if row else None}')
    shot(gc, 'ui_guest_lan.png')
    guest.click(row or 'lan0')
    st = guest.wait(lambda s: s['page'] == 'lobby' and len(s['rows']) >= 2, 20, 'guest lobby')
    check(st is not None, f'guest: joined, lobby {st["rows"] if st else None}')

    # host loads its game; the guest gets the world and loads it
    r = subprocess.run([sys.executable, os.path.join(HERE, 'load_game.py'), '-i', str(lane.HOST)], capture_output=True,
                       text=True, timeout=600)
    check(hc.call('coop.avatar')['ok'], 'host: game loaded')
    t0 = time.time()
    while time.time() - t0 < 240 and not gc.call('coop.avatar')['ok']:
        time.sleep(2)
    check(gc.call('coop.avatar')['ok'], f'guest: in the host world after {time.time() - t0:.0f}s')
    gc.call('wait.frames 200 60000')

    # in game: F9 lobby
    for ui in (host, guest):
        if ui.state().get('open') != '1':
            ui.c.call(f'key {VK_F9}')
        st = ui.wait(lambda s: s['open'] == '1' and s['page'] == 'lobby' and len(s['rows']) == 2, 20, 'F9 lobby')
        check(st is not None, f'{ui.who}: F9 lobby in game: {st["rows"] if st else None}')
    st = guest.state()
    check(any('Creature' in r[1] for r in st['rows']), 'guest: stages shown')
    check(any(r[2].endswith('ms') for r in st['rows']), 'guest: ping shown')

    guest.click('ready')
    st = host.wait(lambda s: any('Bob' in r[0] and r[3] == 'ready' for r in s['rows']), 10, 'ready')
    check(st is not None, 'host: sees the guest ready')

    guest.type_into('chatedit', 'hello from Bob')
    gc.call(f'key {VK_RETURN}')
    st = host.wait(lambda s: any('hello from Bob' in c for c in s['chat']), 10, 'chat')
    check(st is not None, 'host: chat line from the guest')
    shot(hc, 'ui_host_ingame.png')
    shot(gc, 'ui_guest_ingame.png')

    guest.click('leave')
    st = host.wait(lambda s: len(s['rows']) == 1, 20, 'guest left')
    check(st is not None, 'host: the guest left the lobby')
    check(guest.wait(lambda s: s['page'] == 'connect', 10, 'guest connect page') is not None,
          'guest: back on the connect page')

    for name, con in (('host', hc), ('guest', gc)):
        f = con.call('exc')['out'].splitlines()[0]
        print(f'{name} faults: {f}')
        ok &= 'total faults: 0' in f
    print('RESULT:', 'OK' if ok else 'FAIL')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
