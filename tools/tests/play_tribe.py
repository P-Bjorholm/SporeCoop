#!/usr/bin/env python3
"""Tribe stage played by two bot players for many minutes, mostly through real input, the way two
friends play an RTS together: they look around with the minimap and the mouse wheel, box-select
their own tribe members, right-click fruit trees and food plants (gather), animals (hunt), the
other tribe's members and hut (raid, in the attack mode of the tribe UI) or the other tribe's hut in
the social mode (socialize), send their chiefs along, give members to each other (F9 window +
coop.give) and click the partner's members too (those must never get selected). Like friends they
agree on a plan for a while (gather, hunt, raid together, socialize together, kill the epic
creature together), with some own ideas in between.

Console commands only measure or find things (coop.targets objs, coop.avatar for the point under
the mouse when the minimap is calibrated, coop.tribe for where the other tribe lives) and never
play. Measured all the time (docs/stability/tribe.md):
  * orders: every order a click gave reaches the host (guest: never rejected) and the ordered
    members act on it on the host (they move or the target loses health);
  * food: gathering and hunting bring food; the shared food is the same on both machines;
  * the player tribe: the same members, owners and chiefs on both machines, nobody has a
    partner's member selected;
  * sync of citizens (t*) and animals (n*): p90 position error like coop_sync.py;
  * co-op mechanics that happen on their own (pincer, chiefs' aura and rescues, duets, epic);
  * no freeze, crash or fault (twoinst monitors, exc).

  play_tribe.py [--minutes 10] [--seed N] [--rate 1.0]
"""
import argparse
import math
import random
import re
import sys
import time

import lane
from coop_sync import LIMITS, angle, pct, sample
from twoinst import Checks, Monitor, shot, start

SOCIAL_BTN, ATTACK_BTN = (725, 1030), (795, 1030)  # tribe UI interaction mode (1440x1080)
MINIMAP = (110, 980)                               # a point inside the minimap
PLAN = ['gather', 'hunt', 'raid', 'gather', 'social', 'gather', 'raid', 'hunt']


def tribe_state(con):
    """{id: dict(owner, age, sel, hp, pos, tribe)}, tribe 'p' = the player tribe"""
    out, tribe = {}, None
    for line in con.call('coop.tribe')['out'].splitlines():
        m = re.match(r'tribe (\d+)( \(player\))? at \(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', line)
        if m:
            tribe = 'p' if m.group(2) else int(m.group(1))
            continue
        m = re.match(r'\s+#(\d+) \S+ owner=(-?\d+) age=(\d+) sel=(\d+) hp=([\d.]+) pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)',
                     line)
        if m and m.group(1) != '0':
            out[int(m.group(1))] = dict(owner=int(m.group(2)), age=int(m.group(3)), sel=int(m.group(4)),
                                        hp=float(m.group(5)), pos=tuple(float(m.group(i)) for i in (6, 7, 8)),
                                        tribe=tribe)
    return out


def tribe_centres(con):
    out = {}
    for line in con.call('coop.tribe')['out'].splitlines():
        m = re.match(r'tribe (\d+)( \(player\))? at \(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', line)
        if m:
            out['p' if m.group(2) else int(m.group(1))] = tuple(float(m.group(i)) for i in (3, 4, 5))
    return out


def status(con):
    return con.call('coop.status')['out']


def counter(text, name):
    m = re.search(r'\b' + name + r'=(\d+)', text)
    return int(m.group(1)) if m else 0


def food(con):
    m = re.search(r'food=(-?[\d.]+)', con.call('coop.res')['out'])
    return float(m.group(1)) if m else None


def screen(con):
    """things on screen: [(key, x, y)]: member<id>, t<id>, n<id>, tree, o2C9CC91 (food plant), hut<i>[p]"""
    out = []
    for line in con.call('coop.targets objs')['out'].splitlines():
        f = line.split()
        if len(f) == 5 and f[0] == 'member':
            out.append(('member' + f[1], int(f[3]), int(f[4])))
        elif len(f) == 5 and f[0] == 'obj':
            out.append((f[1], int(f[3]), int(f[4])))
    return out


def epic_key(host):
    """the epic creature (when one came): the animal with the most health"""
    if counter(status(host), 'epics') == 0 or 'epic=00000000' in status(host):
        return None
    best = None
    for line in host.call('coop.npc list')['out'].splitlines():
        m = re.match(r'\s+#(\d+) .*hp=([\d.]+)', line)
        if m and float(m.group(2)) >= 200 and (not best or float(m.group(2)) > best[1]):
            best = ('n' + m.group(1), float(m.group(2)))
    return best[0] if best else None


class Player:
    def __init__(self, name, con, pid, rng, log, w, h):
        self.name, self.con, self.pid, self.rng, self.log = name, con, pid, rng, log
        self.w, self.h = w, h
        self.count = {}
        self.pending = []            # orders to check later
        self.orders = {'ok': 0, 'lost': 0, 'idle': 0, 'none': 0}  # none: the click gave no order
        self.busy = {}               # member -> busy until (a real player lets a gatherer work)
        self.J = None                # minimap calibration

    def do(self, line):
        self.log.append(f'{time.strftime("%H:%M:%S")} {self.name}: {line}')
        return self.con.call(line).get('out', '')

    def note(self, k):
        self.count[k] = self.count.get(k, 0) + 1

    def on_screen(self, objs):
        return [o for o in objs if 20 < o[1] < self.w - 60 and 60 < o[2] < self.h - 130]

    # ---- the camera: the minimap (calibrated by looking at where a click on it takes the view) ----
    def centre(self):
        self.con.call(f'move {self.w // 2} {self.h // 2}')
        time.sleep(0.3)
        m = re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', self.con.call('coop.avatar')['out'])
        return tuple(float(m.group(i)) for i in (1, 2, 3)) if m else None

    def minimap(self, x, y):
        self.do(f'click {int(x)} {int(y)}')
        time.sleep(2.0)
        return self.centre()

    def calibrate(self):
        x0, y0 = MINIMAP
        o, a, b = self.minimap(x0, y0), self.minimap(x0 + 30, y0), self.minimap(x0, y0 + 20)
        if o and a and b:
            self.J = ([(a[i] - o[i]) / 30 for i in range(3)], [(b[i] - o[i]) / 20 for i in range(3)])
            self.mm = (x0, y0 + 20, b)
        return self.J is not None

    def look(self, w):
        """centre the view on a world point through the minimap"""
        if not self.J:
            return False
        J = self.J
        x, y, c = self.mm
        for _ in range(3):
            d = [w[i] - c[i] for i in range(3)]
            a11, a22 = sum(v * v for v in J[0]), sum(v * v for v in J[1])
            a12 = sum(J[0][i] * J[1][i] for i in range(3))
            b1, b2 = sum(J[0][i] * d[i] for i in range(3)), sum(J[1][i] * d[i] for i in range(3))
            det = a11 * a22 - a12 * a12 or 1
            x, y = x + (b1 * a22 - b2 * a12) / det, y + (a11 * b2 - a12 * b1) / det
            x, y = min(215, max(8, x)), min(1032, max(935, y))
            c = self.minimap(x, y)
            if not c:
                return False
            self.mm = (x, y, c)
            if math.dist(c, w) < 25:
                self.note('look')
                return True
        self.note('look_far')
        return False

    # ---- selecting own members ----
    def select(self, ids, objs, group=False):
        """box-select own members: a small box around one of `ids` (or a box around all of them)"""
        own = [o for o in self.on_screen(objs) if o[0].startswith('member') and int(o[0][6:]) in ids]
        if not own:
            return []
        if group and len(own) > 1:
            xs, ys = [o[1] for o in own], [o[2] for o in own]
            box = (min(xs) - 30, min(ys) - 45, max(xs) + 30, max(ys) + 25)
        else:
            _, x, y = self.rng.choice(own)
            box = (x - 22, y - 40, x + 22, y + 15)
        x1, y1, x2, y2 = max(5, box[0]), max(65, box[1]), min(self.w - 65, box[2]), min(self.h - 130, box[3])
        if x2 - x1 < 10 or y2 - y1 < 10:
            return []
        self.do(f'drag {x1} {y1} {x2} {y2}')
        time.sleep(1.2)  # the drag takes ~15 frames
        return [i for i, v in tribe_state(self.con).items() if v['sel'] and v['tribe'] == 'p']

    def last_order(self):
        """(the last order line, number of orders): host = its own orders, guest = orders sent"""
        out = status(self.con)
        if self.pid == 0:
            m = re.search(r'tribe last local order: #(\d+) (.*)', out)
            return (m.group(2), int(m.group(1))) if m else ('', 0)
        m = re.search(r'tribe last order: (.*)', out)
        return (m.group(1) if m else ''), counter(out, 'cmds_sent')

    def mode(self, want):
        m = re.search(r'tribe ui mode=(-?\d)', status(self.con))
        if m and int(m.group(1)) != want:
            btn = SOCIAL_BTN if want == 0 else ATTACK_BTN
            self.do(f'click {btn[0]} {btn[1]}')
            time.sleep(0.6)
            self.note('mode_social' if want == 0 else 'mode_attack')


class Game:
    def __init__(self, host, guest, seed, rate):
        self.host, self.guest = host, guest
        self.rng = random.Random(seed)
        self.rate = rate
        self.log = []
        w, h = 1440, 1080
        m = re.search(r'(\d+)x(\d+)', host.call('shot ' + shot_path('size')).get('out', ''))
        if m:
            w, h = int(m.group(1)), int(m.group(2))
        self.w, self.h = w, h
        self.p = [Player('host', host, 0, random.Random(seed * 3 + 1), self.log, w, h),
                  Player('guest', guest, 1, random.Random(seed * 3 + 2), self.log, w, h)]
        self.food = []
        self.food_rises = 0
        self.food_mismatch = 0
        self.roster_mismatch = []
        self.partner_selected = []
        self.sync_pos = {'t': [], 'n': []}
        self.sync_rot = {'t': [], 'n': []}
        self.rejected0 = counter(status(host), 'rejected')
        self.attacked = {}
        self.both_attacked = 0
        self.plan_i, self.plan_until = 0, 0
        self.phases = []
        self.pending_gift = None

    # ---------------- the plan the two friends agree on ----------------
    def plan(self):
        now = time.time()
        if epic_key(self.host):
            if not self.phases or self.phases[-1][1] != 'epic':
                self.phases.append((now, 'epic'))
            return 'epic'
        if now > self.plan_until:
            self.plan_i += 1
            self.plan_until = now + self.rng.uniform(70, 120)
            self.phases.append((now, PLAN[self.plan_i % len(PLAN)]))
        return PLAN[self.plan_i % len(PLAN)]

    # ---------------- playing ----------------
    def step(self, pl):
        rng = pl.rng
        st = tribe_state(pl.con)
        mine = [i for i, v in st.items() if v['tribe'] == 'p' and v['owner'] == pl.pid]
        partner = [i for i, v in st.items() if v['tribe'] == 'p' and v['owner'] not in (pl.pid, 254, -1)]
        now = time.time()
        idle = [i for i in mine if pl.busy.get(i, 0) < now] or (mine if rng.random() < 0.15 else [])
        plan = self.plan()
        kind = plan if rng.random() < 0.75 else rng.choice(['gather', 'hunt', 'partner', 'camera', 'give', 'walk'])
        if kind in ('gather', 'hunt', 'raid', 'social', 'epic', 'walk') and not idle:
            kind = rng.choice(['partner', 'camera', 'wait'])
        pl.note(kind)
        centres = tribe_centres(self.host)  # where the tribes live (a player sees them on the minimap)
        enemies = [t for t in centres if t != 'p']
        if kind in ('gather', 'hunt', 'walk'):
            objs = self.home_view(pl, centres)
            want = {'gather': ('tree', 'o2C9CC91'), 'hunt': ('n',)}.get(kind)
            vis = pl.on_screen(objs)
            if want:
                tg = [o for o in vis if o[0] in want or (want == ('n',) and o[0][0] == 'n' and o[0][1:].isdigit())]
                if kind == 'gather':  # near the village: the food comes home sooner
                    hut = [o for o in objs if o[0].startswith('hut') and o[0].endswith('p')]
                    if hut:
                        tg.sort(key=lambda o: math.hypot(o[1] - hut[0][1], o[2] - hut[0][2]))
                        tg = tg[:6]
                if not tg:
                    pl.note(kind + '_nothing')
                    return self.pause(pl)
            sel = pl.select(idle, objs, group=kind == 'hunt' and rng.random() < 0.5)
            if not self.check_sel(pl, sel, st):
                return self.pause(pl)
            if kind == 'walk':
                tgt = ('ground', rng.randint(300, self.w - 300), rng.randint(400, self.h - 300))
            else:
                tgt = rng.choice(tg)
            self.click_order(pl, kind, sel, tgt, busy=35 if kind == 'gather' else 20)
        elif kind in ('raid', 'social') and enemies:
            et = enemies[0] if rng.random() < 0.8 else rng.choice(enemies)
            objs = self.home_view(pl, centres)
            sel = pl.select(idle, objs, group=rng.random() < 0.6)
            if not self.check_sel(pl, sel, st):
                return self.pause(pl)
            pl.look(centres[et])
            objs = pl.on_screen(screen(pl.con))
            tg = [o for o in objs if (o[0][0] == 't' and o[0][1:].isdigit() and st.get(int(o[0][1:]), {}).get('tribe') == et)
                  or o[0] == f'hut{et}']
            if kind == 'social':
                tg = [o for o in tg if o[0].startswith('hut')] or tg
            if not tg:
                pl.note(kind + '_nothing')
                return self.pause(pl)
            pl.mode(1 if kind == 'raid' else 0)
            tgt = rng.choice(tg)
            if kind == 'raid':
                hits = self.attacked.setdefault(et, {})
                hits[pl.pid] = time.time()
                if len([t for t in hits.values() if time.time() - t < 10]) >= 2:
                    self.both_attacked += 1
            self.click_order(pl, kind, sel, tgt, busy=30)
        elif kind == 'epic':
            ek = epic_key(self.host)
            hs = sample(self.host).get(ek) if ek else None
            if not hs:
                return self.pause(pl)
            objs = self.home_view(pl, centres)
            sel = pl.select(idle, objs, group=True)
            if not self.check_sel(pl, sel, st):
                return self.pause(pl)
            pl.look(hs[0])
            tg = [o for o in pl.on_screen(screen(pl.con)) if o[0] == ek]
            if not tg:
                pl.note('epic_nothing')
                return self.pause(pl)
            pl.mode(1)
            self.click_order(pl, 'epic', sel, tg[0], busy=20)
        elif kind == 'partner':  # real clicks on the partner's members: they must never get selected
            objs = self.home_view(pl, centres)
            pm = [o for o in pl.on_screen(objs) if o[0].startswith('member') and int(o[0][6:]) in partner]
            if not pm:
                return self.pause(pl)
            k, x, y = rng.choice(pm)
            how = rng.choice(['click', 'box', 'right'])
            if how == 'click':
                pl.do(f'click {x} {y}')
            elif how == 'box':
                pl.do(f'drag {x - 40} {y - 50} {x + 40} {y + 30}')
            elif pl.select(idle or mine, objs):  # an order with an own member on the partner's member
                pl.do(f'click {x} {y} right')
            time.sleep(1.3)
            st2 = tribe_state(pl.con)
            bad = [i for i, v in st2.items() if v['sel'] and v['tribe'] == 'p' and v['owner'] != pl.pid]
            if bad:
                self.partner_selected.append(f'{pl.name} {how} on #{k[6:]}: selected {bad}')
            pl.note('partner_' + how)
        elif kind == 'camera':
            what = rng.choice(['wheel', 'wheel', 'pan', 'rotate'])
            if what == 'wheel':
                pl.do(f'wheel {self.w // 2} {self.h // 2} {rng.choice([-3, -2, 2, 3])}')
            elif what == 'pan':
                k = rng.choice(['0x25', '0x26', '0x27', '0x28'])  # arrow keys
                pl.do(f'key {k} down')
                time.sleep(rng.uniform(0.2, 0.6))
                pl.do(f'key {k} up')
            else:
                x = rng.randint(400, self.w - 400)
                pl.do(f'drag {x} 600 {x + rng.choice([-200, 200])} 600 right')
        elif kind == 'give' and len(mine) > 1:  # one own member to the partner: select, F9, give
            sel = pl.select(mine, self.home_view(pl, centres))
            if len(sel) == 1:
                pl.do('key 0x78')  # F9: the co-op window
                time.sleep(1.0)
                out = pl.do('coop.give')
                time.sleep(0.5)
                pl.do('key 0x78')
                self.log.append(f'   give #{sel[0]}: {out.strip()[:80]}')
                self.pending_gift = (time.time() + 3, sel[0], 1 - pl.pid)
                pl.note('gave')
        self.pause(pl)

    def pause(self, pl):
        time.sleep(pl.rng.expovariate(self.rate))

    def home_view(self, pl, centres):
        """the view at the own village (where the members are), through the minimap when needed"""
        objs = screen(pl.con)
        if not any(o[0].startswith('member') for o in pl.on_screen(objs)) and 'p' in centres:
            pl.look(centres['p'])
            objs = screen(pl.con)
        return objs

    def check_sel(self, pl, sel, st):
        if not sel:
            pl.note('no_selection')
            return False
        bad = [i for i in sel if st.get(i, {}).get('owner') != pl.pid]
        if bad:
            self.partner_selected.append(f'{pl.name} box-selected {bad}')
        return True

    def click_order(self, pl, kind, sel, tgt, busy):
        before = pl.last_order()
        dy = -6 if tgt[0] in ('tree', 'o2C9CC91') else 0
        pl.do(f'click {tgt[1]} {tgt[2] + dy} right')
        time.sleep(0.8)
        last, n = pl.last_order()
        if n == before[1]:  # the click gave no order (missed the target, hit the UI)
            pl.orders['none'] += 1
            return
        for i in sel:
            pl.busy[i] = time.time() + busy
        hs = tribe_state(self.host)
        hp = self.target_hp(tgt[0]) if tgt[0][0] in 'tn' and tgt[0][1:].isdigit() else None
        pl.pending.append(dict(due=time.time() + 8, kind=kind, sel=sel, tgt=tgt[0], last=last, hp=hp,
                               rej=counter(status(self.host), 'rejected'),
                               pos={i: hs[i]['pos'] for i in sel if i in hs}))

    def target_hp(self, key):
        if key[0] == 't':
            v = tribe_state(self.host).get(int(key[1:]))
            return v['hp'] if v else 0
        for line in self.host.call('coop.npc list')['out'].splitlines():
            m = re.match(rf'\s+#{key[1:]} .*hp=([\d.]+)', line)
            if m:
                return float(m.group(1))
        return 0

    def check_orders(self, pl, final=False):
        now = time.time()
        for o in [o for o in pl.pending if final or o['due'] < now]:
            pl.pending.remove(o)
            st = tribe_state(self.host)
            moved = [i for i, p in o['pos'].items() if i in st and math.dist(p, st[i]['pos']) > 1.5]
            hurt = o['hp'] is not None and self.target_hp(o['tgt']) < o['hp'] - 0.01
            if pl.pid != 0 and counter(status(self.host), 'rejected') > o['rej']:
                pl.orders['lost'] += 1
                self.log.append(f'   {pl.name} {o["kind"]} {o["tgt"]} {o["sel"]}: LOST ({o["last"][:110]})')
            elif moved or hurt:
                pl.orders['ok'] += 1
            else:  # the order arrived but nobody moved (the target right there, gone, or dead)
                pl.orders['idle'] += 1
                self.log.append(f'   {pl.name} {o["kind"]} {o["tgt"]} {o["sel"]}: no movement ({o["last"][:110]})')

    # ---------------- measuring ----------------
    def measure(self):
        fh, fg = food(self.host), food(self.guest)
        if fh is not None and fg is not None:
            if abs(fh - fg) > 0.5:  # the shared food travels every few frames: look twice
                time.sleep(1.0)
                fh, fg = food(self.host), food(self.guest)
                if abs(fh - fg) > 0.5:
                    self.food_mismatch += 1
                    self.log.append(f'   food differs: host {fh} guest {fg}')
            if self.food and fh > self.food[-1][1] + 0.5:
                self.food_rises += 1
            self.food.append((time.time(), fh, fg))
        hs, gs = tribe_state(self.host), tribe_state(self.guest)

        def roster(s):
            return {i: v['owner'] for i, v in s.items() if v['tribe'] == 'p'}
        if roster(hs) != roster(gs):
            time.sleep(1.5)  # a member born or killed this moment: look again
            hs, gs = tribe_state(self.host), tribe_state(self.guest)
            if roster(hs) != roster(gs):
                self.roster_mismatch.append(f'host {roster(hs)} guest {roster(gs)}')
        hc, gc = re.search(r'chiefs=(.*)', status(self.host)), re.search(r'chiefs=(.*)', status(self.guest))
        if hc and gc and set(hc.group(1).split()) != set(gc.group(1).split()):
            time.sleep(1.5)
            hc, gc = re.search(r'chiefs=(.*)', status(self.host)), re.search(r'chiefs=(.*)', status(self.guest))
            if hc and gc and set(hc.group(1).split()) != set(gc.group(1).split()):
                self.roster_mismatch.append(f'chiefs host {hc.group(1)} guest {gc.group(1)}')
        for name, s, me in (('host', hs, 0), ('guest', gs, 1)):
            bad = [i for i, v in s.items() if v['sel'] and v['tribe'] == 'p' and v['owner'] != me]
            if bad:
                self.partner_selected.append(f'{name} has partner members selected: {bad}')
        h1, g, h2 = sample(self.host), sample(self.guest), sample(self.host)
        for k, (gp, gq) in g.items():
            if k[0] not in 'tn' or k not in h1 or k not in h2:
                continue
            e1, e2 = math.dist(gp, h1[k][0]), math.dist(gp, h2[k][0])
            self.sync_pos[k[0]].append(min(e1, e2))
            self.sync_rot[k[0]].append(angle(gq, h1[k][1] if e1 <= e2 else h2[k][1]))
        if self.pending_gift and self.pending_gift[0] < time.time():
            _, mid, to = self.pending_gift
            self.pending_gift = None
            ho, go = hs.get(mid, {}).get('owner'), gs.get(mid, {}).get('owner')
            if mid in hs and (ho != to or go != to):
                self.roster_mismatch.append(f'gift of #{mid} to {to}: host owner {ho}, guest owner {go}')


def shot_path(name):
    import os
    return os.path.join(lane.out_dir('shots'), f'play_tribe_{name}.png')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--minutes', type=float, default=10)
    ap.add_argument('--seed', type=int, default=None)
    ap.add_argument('--rate', type=float, default=1.0, help='mean pauses per second between actions')
    a = ap.parse_args()
    seed = a.seed if a.seed is not None else random.randrange(1 << 30)
    print(f'  play_tribe: {a.minutes} min, seed {seed}', flush=True)
    host, guest = start('tribe')
    ck = Checks()
    t0 = time.time()
    while time.time() - t0 < 90:  # the split of the player tribe arrives with the first states
        if any(v['owner'] == 1 for v in tribe_state(guest).values()):
            break
        time.sleep(2)
    g = Game(host, guest, seed, a.rate)
    food0 = food(host)
    for p in g.p:
        ck(p.calibrate(), f'{p.name}: the minimap takes the view where it points')
    print(f'  start: food {food0}, {len(tribe_state(host))} citizens, screen {g.w}x{g.h}', flush=True)
    t0, last_measure, n, ok = time.time(), 0, 0, True
    try:
        while time.time() - t0 < a.minutes * 60 and ok:
            g.step(g.p[n % 2])
            n += 1
            if time.time() - last_measure > 10:
                last_measure = time.time()
                for p in g.p:
                    g.check_orders(p)
                g.measure()
            if Monitor.events:
                ok = False
            if n % 30 == 0:
                print(f'  {time.time() - t0:5.0f} s [{g.phases[-1][1] if g.phases else "-"}]: food '
                      f'{g.food[-1][1] if g.food else "?"}, ' + ', '.join(f'{p.name} {p.orders}' for p in g.p), flush=True)
    except (OSError, ValueError) as e:
        g.log.append(f'  !! console error: {e!r}')
        ok = False
    played = time.time() - t0
    time.sleep(5)
    for p in g.p:
        g.check_orders(p, final=True)
    st1, gst = status(host), status(guest)
    print(f'  played {played / 60:.1f} min, {n} actions; plan: '
          + ' '.join(f'{(t - t0) / 60:.1f}:{k}' for t, k in g.phases), flush=True)
    for p in g.p:
        print(f'  {p.name}: ' + ' '.join(f'{k}={v}' for k, v in sorted(p.count.items())), flush=True)
        print(f'  {p.name} orders: {p.orders}', flush=True)
    fs = [f for _, f, _ in g.food]
    print(f'  food: start {food0}, end {fs[-1] if fs else "?"}, max {max(fs) if fs else "?"}, '
          f'rises {g.food_rises}, samples {len(fs)}, host/guest mismatches {g.food_mismatch}', flush=True)
    for name, s in (('host', st1), ('guest', gst)):
        m = re.search(r'tribe\((?:host|guest)\).*', s)
        print(f'  {name}:', m.group(0) if m else '?')
    co = re.search(r'tribe co-op: .*', st1)
    print('  co-op:', co.group(0) if co else '?', f'(both raided one tribe within 10 s: {g.both_attacked}x)')
    for kind, name in (('t', 'citizens'), ('n', 'animals')):
        p, r = g.sync_pos[kind], g.sync_rot[kind]
        if p:
            print(f'  sync {name:9}: samples {len(p):4} pos p50 {pct(p, .5):5.2f} p90 {pct(p, .9):5.2f} '
                  f'max {max(p):6.2f} m | facing p90 {pct(r, .9):5.1f} deg', flush=True)
    ck(ok, 'the games survived the play')
    ck(not g.partner_selected, f'nobody selects a partner\'s member ({g.partner_selected[:3]})')
    ck(g.food_mismatch <= max(1, len(fs) // 20), f'the shared food is the same on both ({g.food_mismatch} differences)')
    ck(g.food_rises >= 2, f'gathering and hunting bring food ({g.food_rises} rises)')
    ck(len(g.roster_mismatch) <= 1, f'the same members, owners and chiefs on both ({g.roster_mismatch[:3]})')
    for p in g.p:
        tot = p.orders['ok'] + p.orders['lost'] + p.orders['idle']
        ck(p.orders['lost'] == 0, f'{p.name}: no order lost ({p.orders["lost"]} of {tot})')
        ck(tot >= 10, f'{p.name}: gave orders through real clicks ({tot})')
        ck(p.orders['ok'] >= 0.7 * tot, f'{p.name}: the members act on the orders ({p.orders})')
    ck(counter(st1, 'rejected') == g.rejected0, f'the host rejected no guest order ({counter(st1, "rejected")})')
    for kind in 'tn':
        if g.sync_pos[kind]:
            ck(pct(g.sync_pos[kind], .9) <= LIMITS[kind][0], f'sync {kind}*: 90% within {LIMITS[kind][0]} m')
    if not ok or not ck.ok:
        print('  last actions:')
        for line in g.log[-40:]:
            print('   ', line)
    print(f'  replay: SPORE_LANE={lane.LANE} python3 tools/tests/play_tribe.py --minutes {a.minutes} --seed {seed}')
    try:
        shot(host, 'play_tribe_host.png')
        shot(guest, 'play_tribe_guest.png')
        ck.faults(host=host, guest=guest)
    except (OSError, ValueError) as e:
        ck(False, f'the games answer after the play ({e!r})')
    return ck.result()


if __name__ == '__main__':
    sys.exit(main())
