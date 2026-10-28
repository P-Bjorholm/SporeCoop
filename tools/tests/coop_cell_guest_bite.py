#!/usr/bin/env python3
"""Cell stage: a guest's bite on the host's NPC is judged as the host's own bite (BUG-0078).

DamageCell 0xE7A7C0 does nothing when ShouldNotAttack(victim, attacker) is true (the victim's
species "won't attack the player", same species, friend group) or the victim has +0x17C / +0x111
set. The host applied a guest's bite (MSG_CELL_HIT) without these checks and asked ShouldNotAttack
with the arguments swapped, so the guest hurt cells the host's own bite cannot.

On one NPC creature (the one nearest the guest):
  1. a plain guest bite takes hp on the host (control);
  2. its species gets "won't attack the player" (`coop.pool npcflag c<key> spare 1`, host): the
     host's own bite leaves its hp as it was (the game's rule works), and so does the guest's;
  3. the same with the NPC's own +0x111 instead (`npcflag c<key> invuln 1`);
  4. both flags off: the guest's bite hurts it again.
The NPC's hp is read in the host's game (`coop.pool cell`).

SPORE_CELL_FIX=0 switches the fix off on the host (`coop.pool fix guestbite 0`): the run that must FAIL.
"""
import os
import re
import time

from twoinst import Checks, start


def hp(host, key):
    m = re.search(r'\bhp=(-?\d+)', host.call(f'coop.pool cell {key}')['out'])
    return int(m.group(1)) if m else None


def bite(con, key):
    return con.call(f'coop.pool kill {key} hit')['out']


def main():
    fix = os.environ.get('SPORE_CELL_FIX', '1')
    host, guest = start('cell')
    ck = Checks()
    key = None
    try:
        out = host.call(f'coop.pool fix guestbite {fix}')['out']
        ck(f'guestbite={fix}' in out, f'fix set to {fix}: {out.strip()}')
        time.sleep(3)
        # an NPC with enough hp for a few bites: the nearest creature of the guest
        for _ in range(10):
            r = guest.call('coop.pool kill - hit')['out']
            m = re.match(r'bit (c\d+) .* hp=(\d+)', r)
            if m and 'dying' not in r:
                key = m.group(1)
                break
            time.sleep(1)
        ck(key is not None, f'the guest found an NPC creature ({r})')
        if not key:
            return ck.result()

        def guest_bite(label, expect_hurt):
            host.call(f'coop.pool swimto {key} tp')  # it fled: both stay next to it (in the host's state area)
            guest.call(f'coop.pool swimto {key} tp')
            time.sleep(2.0)  # the hurt lock (+0x17C) of the last bite is over
            h0 = hp(host, key)
            r = bite(guest, key)
            time.sleep(1.5)
            h1 = hp(host, key)
            print(f'  {label}: guest {r}; host hp {h0} -> {h1}')
            if expect_hurt:
                ck(h0 is not None and h1 is not None and h1 < h0, f'{label}: the guest\'s bite took hp ({h0} -> {h1})')
            else:
                ck(h0 is not None and h1 is not None and h1 >= h0, f'{label}: the guest\'s bite took nothing ({h0} -> {h1})')

        def host_bite(label):
            time.sleep(2.0)
            h0 = hp(host, key)
            r = bite(host, key)
            time.sleep(0.5)
            h1 = hp(host, key)
            print(f'  {label}: host {r}; host hp {h0} -> {h1}')
            ck(h0 is not None and h1 is not None and h1 >= h0, f'{label}: the host\'s own bite took nothing ({h0} -> {h1})')

        print('  ' + host.call(f'coop.pool npcflag {key} hp 60')['out'])  # room for the bites (NPCs have 6 or so)
        # 1. control: a guest bite hurts it
        guest_bite('plain', True)
        # 2. the species won't attack the player
        print('  ' + host.call(f'coop.pool npcflag {key} spare 1')['out'])
        host_bite('spare')
        guest_bite('spare', False)
        print('  ' + host.call(f'coop.pool npcflag {key} spare 0')['out'])
        # 3. the NPC is invulnerable (+0x111)
        print('  ' + host.call(f'coop.pool npcflag {key} invuln 1')['out'])
        host_bite('invuln')
        guest_bite('invuln', False)
        print('  ' + host.call(f'coop.pool npcflag {key} invuln 0')['out'])
        # 4. back to normal
        guest_bite('again', True)
        print('  host: ' + ' '.join(re.findall(r'hits_(?:in|aggro|spared)=\d+', host.call('coop.pool')['out'])))
    finally:
        if key:
            host.call(f'coop.pool npcflag {key} spare 0')
            host.call(f'coop.pool npcflag {key} invuln 0')
        host.call('coop.pool fix guestbite 1')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
