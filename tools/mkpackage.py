#!/usr/bin/env python3
"""Build a Spore .package (DBPF 3.0, uncompressed entries) from files.

usage: mkpackage.py out.package <group>!<name>.<type> =path [...]
  each resource: "group!name.type=path", names are hashed like Spore's id() (FNV-1,
  lower case) unless given as 0x... hex; type may be a known extension (png, prop,
  spui, locale) or 0x hex.
"""
import struct
import sys

TYPES = {'png': 0x2F7D0004, 'prop': 0x00B1B104, 'spui': 0x0250FE9A, 'locale': 0x02FABF01,
         'rw4': 0x2F4E681B, 'dds': 0x00B2D882}


def fnv(s):
    h = 0x811C9DC5
    for c in s.lower().encode():
        h = (h * 0x1000193) & 0xFFFFFFFF
        h ^= c
    return h


def ident(s):
    return int(s, 16) if s.lower().startswith('0x') else fnv(s)


def build(out, specs):
    entries = []
    for spec in specs:
        key, path = spec.split('=', 1)
        group, rest = key.split('!', 1)
        name, typ = rest.rsplit('.', 1)
        t = TYPES.get(typ.lower()) or int(typ, 16)
        entries.append((t, ident(group), ident(name), open(path, 'rb').read()))
    body = bytearray(96)
    offs = []
    for t, g, i, data in entries:
        offs.append(len(body))
        body += data
    index = bytearray(struct.pack('<II', 4, 0))  # constant "unknown" field = 0
    for (t, g, i, data), o in zip(entries, offs):
        index += struct.pack('<IIIIIIHH', t, g, i, o, len(data) | 0x80000000, len(data), 0, 1)
    index_off = len(body)
    body += index
    hdr = bytearray(96)
    struct.pack_into('<4sII', hdr, 0, b'DBPF', 3, 0)
    struct.pack_into('<I', hdr, 0x24, len(entries))
    struct.pack_into('<I', hdr, 0x2C, len(index))
    struct.pack_into('<I', hdr, 0x3C, 3)  # index minor version
    struct.pack_into('<I', hdr, 0x40, index_off)
    body[:96] = hdr
    open(out, 'wb').write(body)
    for t, g, i, data in entries:
        print(f'  {g:08X}!{i:08X}.{t:08X} {len(data)} bytes')


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    build(sys.argv[1], sys.argv[2:])
