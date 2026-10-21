"""Heuristic function-entry discovery for MSVC x86 binaries.

Entries = targets of direct E8 calls + code pointers found in .rdata/.data
(vtables, callback tables), filtered to plausible function starts.
"""
import struct


def plausible_start(pe, va):
    b = pe.read(va - 1, 4)
    if not b or len(b) < 4:
        return False
    prev = b[0]
    # previous byte: int3 padding, ret, nop padding, or end of 'ret imm16'
    if prev in (0xCC, 0xC3, 0x90):
        return True
    p3 = pe.read(va - 3, 1)
    if p3 and p3[0] == 0xC2:
        return True
    return False


def find_function_starts(pe):
    text = pe.section('.text')
    lo, hi = text['va'], text['va'] + text['vsize']
    d = pe.data
    raw = text['raw']
    size = min(text['vsize'], text['rsize'])
    starts = {}
    # direct calls
    i = 0
    find = d.find
    end = raw + size - 5
    while True:
        i = find(b'\xE8', raw if i == 0 else i, end)
        if i < 0:
            break
        rel = struct.unpack_from('<i', d, i + 1)[0]
        src = text['va'] + (i - raw)
        tgt = (src + 5 + rel) & 0xFFFFFFFF
        if lo <= tgt < hi:
            starts[tgt] = starts.get(tgt, 0) + 1
        i += 1
    calls = {a: n for a, n in starts.items() if n >= 2 or plausible_start(pe, a)}
    # code pointers in data sections
    ptrs = set()
    for sname in ('.rdata', '.data'):
        s = pe.section(sname)
        if not s:
            continue
        buf = d[s['raw']:s['raw'] + min(s['vsize'], s['rsize'])]
        for off in range(0, len(buf) - 3, 4):
            v = struct.unpack_from('<I', buf, off)[0]
            if lo <= v < hi and plausible_start(pe, v):
                ptrs.add(v)
    allst = set(calls) | ptrs
    return sorted(allst), calls, ptrs
