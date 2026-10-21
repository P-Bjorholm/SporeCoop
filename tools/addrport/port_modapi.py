#!/usr/bin/env python3
"""Port ModAPI (March2017) function addresses to another build of SporeApp.exe.

The GOG 2016 build shares code layout with the Steam March2017 build except for
small insertions/removals, so functions are shifted by a piecewise-constant delta.
We align the sorted ModAPI function list against discovered function entries of the
target exe with a Viterbi pass (states = candidate deltas, switching costs 1).

Output: a symbol file `<name>=0xADDR  ; conf delta` usable by the SporeKit loader.
Confidence:
  high   - delta agrees with both neighbours and target is a discovered entry
  medium - target is a discovered entry, delta agrees with one neighbour
  low    - isolated guess
"""
import argparse
import bisect
import re
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pe import PE  # noqa: E402
from funcstarts import find_function_starts  # noqa: E402

WINDOW = 0x300


def load_modapi_functions(xml_path):
    txt = open(xml_path, encoding='utf-8', errors='replace').read()
    out = {}
    for m in re.finditer(r'<FUNCTION ADDRESS="([0-9a-fA-F]+)" NAME="([^"]+)"', txt):
        out.setdefault(int(m.group(1), 16), m.group(2))
    return out


def load_additional(path):
    out = {}
    if not os.path.exists(path):
        return out
    for line in open(path, encoding='utf-8', errors='replace'):
        line = line.strip()
        if '=' not in line or line.startswith('#'):
            continue
        a, n = line.split('=', 1)
        try:
            out.setdefault(int(a, 16), n.strip())
        except ValueError:
            pass
    return out


def load_define_address(sdk):
    """Parse ModAPI SourceCode/DLL/Addresses*.cpp: DefineAddress(Name, SelectAddress(disk, m17))."""
    out = {}
    d = os.path.join(sdk, 'Spore ModAPI', 'SourceCode', 'DLL')
    tok = re.compile(r'namespace\s+Addresses\((\w+)\)\s*\{|namespace\s+(\w+)\s*\{|\{|\}|'
                     r'DefineAddress\(\s*(\w+)\s*,\s*SelectAddress\(\s*(0x[0-9A-Fa-f]+)\s*,\s*(0x[0-9A-Fa-f]+)')
    for fn in sorted(os.listdir(d)):
        if not (fn.startswith('Addresses') and fn.endswith('.cpp')):
            continue
        txt = open(os.path.join(d, fn), encoding='utf-8', errors='replace').read()
        txt = re.sub(r'//[^\n]*', '', txt)
        stack = []
        for m in tok.finditer(txt):
            g = m.group(0)
            if m.group(1) or m.group(2):
                stack.append(m.group(1) or m.group(2))
            elif g == '{':
                stack.append(None)
            elif g == '}':
                if stack:
                    stack.pop()
            elif m.group(3):
                name = '::'.join([x for x in stack if x] + [m.group(3)])
                out.setdefault(int(m.group(5), 16), name)
    return out


def port(pe, src_funcs):
    starts, _, _ = find_function_starts(pe)
    addrs = sorted(a for a in src_funcs if pe.in_text(a))
    cands = []
    for a in addrs:
        lo = bisect.bisect_left(starts, a - WINDOW)
        hi = bisect.bisect_right(starts, a + WINDOW)
        cands.append([s - a for s in starts[lo:hi]] or [None])
    # Viterbi: cost(delta change)=1, cost(None)=1.5, small bias to |delta|
    INF = float('inf')
    prev_cost = {d: (1.5 if d is None else abs(d) / 1e5) for d in cands[0]}
    back = []
    for i in range(1, len(addrs)):
        cur = {}
        bp = {}
        best_prev = min(prev_cost, key=prev_cost.get)
        for d in cands[i]:
            emit = 1.5 if d is None else abs(d) / 1e5
            c_same = prev_cost.get(d, INF)
            c_switch = prev_cost[best_prev] + 1.0
            if c_same <= c_switch:
                cur[d], bp[d] = c_same + emit, d
            else:
                cur[d], bp[d] = c_switch + emit, best_prev
        back.append(bp)
        prev_cost = cur
    d = min(prev_cost, key=prev_cost.get)
    path = [d]
    for bp in reversed(back):
        d = bp[d]
        path.append(d)
    path.reverse()
    result = []
    for i, a in enumerate(addrs):
        d = path[i]
        if d is None:
            result.append((a, None, 'none'))
            continue
        agree = int(i > 0 and path[i - 1] == d) + int(i + 1 < len(addrs) and path[i + 1] == d)
        conf = 'high' if agree == 2 else ('medium' if agree == 1 else 'low')
        result.append((a, a + d, conf))
    return result


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('exe')
    ap.add_argument('modapi_sdk', help='path to Spore-ModAPI checkout')
    ap.add_argument('-o', '--out', required=True)
    args = ap.parse_args()
    pe = PE(args.exe)
    g = os.path.join(args.modapi_sdk, 'SDKtoGhidra')
    funcs = load_modapi_functions(os.path.join(g, 'SporeGhidra_march2017.xml'))
    for a, n in load_additional(os.path.join(g, 'additional_march2017.txt')).items():
        funcs.setdefault(a, n)
    extra = 0
    for a, n in load_define_address(args.modapi_sdk).items():
        if a not in funcs:
            funcs[a] = n
            extra += 1
    print('DefineAddress entries added:', extra)
    res = port(pe, funcs)
    stats = {}
    with open(args.out, 'w') as f:
        f.write('# SporeKit symbol database\n')
        f.write('# exe_sha256=%s\n' % pe.sha256)
        f.write('# source=ModAPI March2017 symbols ported by tools/addrport/port_modapi.py\n')
        f.write('# format: name=0xVA ; confidence march2017_va\n')
        for a, t, conf in sorted(res, key=lambda r: funcs[r[0]]):
            stats[conf] = stats.get(conf, 0) + 1
            if t is None:
                continue
            f.write('%s=0x%08X ; %s m17=0x%08X\n' % (funcs[a], t, conf, a))
    print('ported', len(res), stats, '->', args.out)


if __name__ == '__main__':
    main()
