#!/usr/bin/env python3
"""Rejoins over a bad network: the guest leaves and joins again several times; each round measures
how long it takes until the host sees the guest in its world (`together 1 1` in the host's
coop.status), then until the world the host sends again has arrived and is loaded, and, from the
game logs, the parts: handshake (host "joined from"), world transfer
(host "world: sharing" -> guest "world: received") and the load. Meant for SPORE_NET (netem.py):

  SPORE_LANE=2 SPORE_NET=awful python3 tools/tests/coop_rejoin_net.py [--rounds 4] [--fixture tribe]
                                                                     [--limit 90] [--gap 1:4]

Even rounds lose the guest's goodbye (an outage of the proxy over the leave). The proxy, like a NAT
that keeps its mapping, sends the new connection from the old address: before protocol 6 the host's
old peer swallowed the new HELLO and the guest never got back (2026-10-22, SPORE_NET=awful chaos).
"""
import argparse
import os
import random
import re
import sys
import time

import lane
import netem
from twoinst import Checks, start, wait_guest_in

LOGS = None


def log_path(inst):
    """the lane's game log (tools/spore.sh: lane 1 = the main game, lane N = SPORE-lab/laneN next to it)"""
    global LOGS
    if LOGS is None:
        import subprocess
        common = subprocess.run(['git', 'rev-parse', '--path-format=absolute', '--git-common-dir'],
                                cwd=lane.ROOT, capture_output=True, text=True).stdout.strip()
        game = os.path.dirname(os.path.dirname(common))
        if lane.LANE > 1:
            game = os.path.join(os.path.dirname(game), 'SPORE-lab', f'lane{lane.LANE}')
        LOGS = os.path.join(game, 'SporebinEP1', 'mods', 'logs')
    return os.path.join(LOGS, 'sporekit.log' if inst == 1 else f'sporekit.{inst}.log')


def log_times(inst, since, patterns):
    """first time (s since `since`, wall clock of today) each regex matched in the game log"""
    found = {}
    try:
        path = log_path(inst)
        lines = open(path, errors='replace').read().splitlines()
        mtime = os.path.getmtime(path)
    except OSError:
        return found
    stamps = []
    for line in lines:
        m = re.match(r'(\d\d):(\d\d):(\d\d\.\d+) ', line)
        if m:
            stamps.append((int(m.group(1)) * 3600 + int(m.group(2)) * 60 + float(m.group(3)), line))
    if not stamps:
        return found
    # the game logs in Wine's time zone: map the last line to the file's mtime (whole quarter hours)
    lt = time.localtime(mtime)
    local_sec = lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec + (mtime % 1)
    shift = round((local_sec - stamps[-1][0]) / 900) * 900
    day0 = mtime - local_sec
    for sec, line in stamps:
        t = day0 + sec + shift
        if t > mtime + 3600:
            t -= 86400
        elif t < mtime - 86400 + 3600:
            t += 86400
        if t < since - 0.5:
            continue
        for name, pat in patterns.items():
            if name not in found and re.search(pat, line):
                found[name] = t - since
    return found


def net_line(con):
    out = con.call('coop.status').get('out', '')
    lines = [x.strip() for x in out.splitlines() if x.strip().startswith('[')]
    world = [x.strip() for x in out.splitlines() if 'world' in x and ('sending' in x or 'receiving' in x)]
    return ' | '.join(lines + world)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--fixture', default='tribe')
    ap.add_argument('--rounds', type=int, default=4)
    ap.add_argument('--limit', type=float, default=90, help='seconds a rejoin may take')
    ap.add_argument('--gap', default='1:4', help='seconds between leave and join (min:max)')
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--lose-bye', choices=['even', 'all', 'none'], default='even',
                    help='rounds whose goodbye the bad network loses (an outage over the leave)')
    a = ap.parse_args()
    rng = random.Random(a.seed)
    print(f'network: {os.environ.get("SPORE_NET") or "plain (no proxy)"}', flush=True)
    t0 = time.time()
    host, guest = start(a.fixture)
    print(f'  first join (start of both games included): {time.time() - t0:.0f} s', flush=True)
    ck = Checks()
    times = []
    for rnd in range(1, a.rounds + 1):
        lose = netem.proxy() and (a.lose_bye == 'all' or (a.lose_bye == 'even' and rnd % 2 == 0))
        if lose:
            netem.proxy().outage(0.8)  # the goodbye packets are lost
        guest.call('coop.leave')
        time.sleep(rng.uniform(*map(float, a.gap.split(':'))))
        t = time.time()
        guest.call(lane.JOIN)
        stats = []

        def close(h):
            h.call('key enter')

        ok = False
        deadline = t + a.limit
        next_stat = t
        while time.time() < deadline:
            if time.time() >= next_stat:
                stats.append(f'    {time.time() - t:5.1f}s host: {net_line(host)}\n'
                             f'           guest: {net_line(guest)}')
                next_stat += 10
            if wait_guest_in(host, guest, timeout=2, close=close):
                ok = True
                break
        took = time.time() - t
        # the host sends its current world to every guest that joins; the guest (still in its old copy,
        # so `together` comes at once) loads it when it has arrived: wait for that and the reunion
        reloaded = None
        while ok and time.time() < deadline:
            p = log_times(lane.HOST, t, {'share': r'world: sharing'})
            p.update(log_times(lane.GUEST, t, {'loading': r'world: loading'}))
            if 'share' not in p and time.time() - t > 20:
                break  # no world sent (a stage whose guests keep their own worlds)
            if 'loading' in p and wait_guest_in(host, guest, timeout=2, close=close):
                reloaded = time.time() - t
                break
            wait_guest_in(host, guest, timeout=1, close=close)
        if ok and reloaded is None and 'share' in log_times(lane.HOST, t, {'share': r'world: sharing'}):
            ok = False  # the world was sent but never arrived / loaded
        parts = log_times(lane.HOST, t, {'hello': r"player \d+ '.*' joined from", 'share': r'world: sharing',
                                          'queued': r'world: transfer to \d+ queued completely'})
        parts.update(log_times(lane.GUEST, t, {'welcome': r'joined as player', 'received': r'world: received',
                                                'loading': r'world: loading'}))
        desc = ', '.join(f'{k} {v:.1f}' for k, v in sorted(parts.items(), key=lambda kv: kv[1]))
        rl = f', with the world sent again and loaded in {reloaded:.1f} s' if reloaded else ''
        ck(ok, f'round {rnd}{" (goodbye lost)" if lose else ""}: guest back in the host world in {took:.1f} s{rl} '
               f'(limit {a.limit:.0f}) [{desc}]')
        if 'share' in parts and 'received' in parts:
            print(f'    world transfer {parts["received"] - parts["share"]:.1f} s', flush=True)
        if not ok:
            print('\n'.join(stats), flush=True)
            print(f'    host: {net_line(host)}\n    guest: {net_line(guest)}', flush=True)
        times.append(reloaded or took)
        if netem.proxy():
            print('   ', netem.proxy().describe(), flush=True)
    if times:
        print(f'  rejoin times: {" ".join(f"{x:.1f}" for x in times)} (max {max(times):.1f} s)', flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    sys.exit(main())
