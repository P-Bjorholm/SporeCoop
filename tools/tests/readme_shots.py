#!/usr/bin/env python3
"""Screenshots for the README (docs/media/): every stage with two players, a co-op mechanic
triggered so the picture shows it (knockout, pincer toast, city alarm, a disabled ship with its
repair beacon). Full-size captures, saved as 960x720 JPEG.

  SPORE_LANE=2 python3 tools/tests/readme_shots.py [stage ...]
"""
import os
import sys
import time

import lane
from twoinst import ROOT, start

OUT = os.path.join(ROOT, 'docs', 'media')


def grab(con, name):
    con.call('shot ' + os.path.join(lane.out_dir('shots'), name + '.png'))
    time.sleep(1.5)
    save(name)


def save(name):
    from PIL import Image
    raw = os.path.join(lane.out_dir('shots'), name + '.png')
    im = Image.open(raw).convert('RGB')
    im.thumbnail((960, 720))
    os.makedirs(OUT, exist_ok=True)
    im.save(os.path.join(OUT, name + '.jpg'), quality=86, optimize=True, progressive=True)
    print('  ', name)


def tribe_ids(con):
    import re
    out = {}
    for line in con.call('coop.tribe')['out'].splitlines():
        m = re.match(r'\s+#(\d+) \S+ owner=(-?\d+)', line)
        if m:
            out[int(m.group(1))] = int(m.group(2))
    return out


def stage_cell(h, g):
    h.call('coop.pool hurt 1 999')  # the guest is knocked out: translucent, countdown, arrow
    time.sleep(2.5)
    grab(h, 'cell_host')
    grab(g, 'cell_guest')


def stage_creature(h, g):
    time.sleep(2)
    grab(h, 'creature_host')
    g.call('coop.uistate 1')  # the co-op window in game (F9)
    time.sleep(2)
    grab(g, 'creature_guest_window')
    g.call('coop.uistate 0')


def stage_tribe(h, g):
    ids = tribe_ids(h)
    mine = [i for i, o in ids.items() if o == 0]
    theirs = [i for i, o in ids.items() if o == 1]
    enemy = [i for i, o in ids.items() if o >= 254]
    if enemy and mine and theirs:
        h.call(f'coop.tribe pincer {enemy[0]} {mine[-1]} 1')
        h.call(f'coop.tribe pincer {enemy[0]} {theirs[-1]} 1')
    time.sleep(2.5)
    grab(h, 'tribe_host')
    grab(g, 'tribe_guest')


def stage_civ(h, g):
    import re
    m = re.search(r'city (\d+)', h.call('coop.civ')['out'])
    if m:
        h.call(f'coop.civ alarm {m.group(1)}')
    time.sleep(2.5)
    grab(h, 'civ_host')
    grab(g, 'civ_guest')


def stage_space(h, g):
    g.call('coop.space hit 100000')  # disabled: countdown on the guest, REPAIR beacon on the host
    time.sleep(0.7)  # ships start 12 m apart: the repair begins at once and takes 2 s
    grab(h, 'space_host')
    grab(g, 'space_guest')


STAGES = {'creature': stage_creature, 'cell': stage_cell, 'tribe': stage_tribe, 'civ': stage_civ,
          'space': stage_space}


def main():
    for st in sys.argv[1:] or list(STAGES):
        print(st, flush=True)
        h, g = start(st)
        time.sleep(4)
        STAGES[st](h, g)
    lane.sh('kill')


if __name__ == '__main__':
    main()
