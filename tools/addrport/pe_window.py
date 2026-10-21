#!/usr/bin/env python3
"""Build a "windowed" copy of SporeApp.exe for fast static analysis.

The full exe has ~16 MB of code which takes Ghidra longer than REA's default
analysis timeout. This tool keeps the PE headers, imports and data sections
(.rdata/.data, so strings, vtables and globals still resolve) but truncates
.text to a code window [lo, hi). Virtual addresses are unchanged, so every
address seen in the analysis matches the real game.

usage: pe_window.py SporeApp.exe 0xC00000 0xC40000 out.exe
"""
import struct
import sys


def main():
    src, lo, hi, out = sys.argv[1], int(sys.argv[2], 16), int(sys.argv[3], 16), sys.argv[4]
    d = bytearray(open(src, 'rb').read())
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    nsec = struct.unpack_from('<H', d, pe + 6)[0]
    optsz = struct.unpack_from('<H', d, pe + 20)[0]
    opt = pe + 24
    image_base = struct.unpack_from('<I', d, opt + 28)[0]
    falign = struct.unpack_from('<I', d, opt + 36)[0]
    sec_tab = opt + optsz
    secs = []
    for i in range(nsec):
        o = sec_tab + i * 40
        name = bytes(d[o:o + 8]).rstrip(b'\0').decode('latin1')
        vsz, rva, rsz, raw = struct.unpack_from('<IIII', d, o + 8)
        chars = struct.unpack_from('<I', d, o + 36)[0]
        secs.append([name, vsz, rva, rsz, raw, chars])

    keep = []
    for s in secs:
        name, vsz, rva, rsz, raw, chars = s
        if name in ('.rsrc', '.reloc'):
            continue
        if name == '.text':
            lo_rva, hi_rva = lo - image_base, hi - image_base
            assert rva <= lo_rva < hi_rva <= rva + vsz, 'window outside .text'
            data = bytes(d[raw + (lo_rva - rva): raw + (hi_rva - rva)])
            keep.append([name, hi_rva - lo_rva, lo_rva, data, chars])
        else:
            keep.append([name, vsz, rva, bytes(d[raw:raw + rsz]), chars])

    hdr_end = 0x1000 if falign <= 0x1000 else falign
    headers = bytearray(d[:hdr_end])
    # clear relocation + resource data directories (indices 2 and 5)
    ddir = opt + 96
    for idx in (2, 5):
        struct.pack_into('<II', headers, ddir + idx * 8, 0, 0)
    struct.pack_into('<H', headers, pe + 6, len(keep))
    body = bytearray()
    pos = hdr_end
    for i, (name, vsz, rva, data, chars) in enumerate(keep):
        o = sec_tab + i * 40
        nm = name.encode('latin1')[:8].ljust(8, b'\0')
        rsz = (len(data) + falign - 1) // falign * falign
        headers[o:o + 8] = nm
        struct.pack_into('<IIII', headers, o + 8, vsz, rva, rsz, pos)
        struct.pack_into('<IIHHI', headers, o + 24, 0, 0, 0, 0, chars)
        body += data + b'\0' * (rsz - len(data))
        pos += rsz
    for i in range(len(keep), nsec):
        o = sec_tab + i * 40
        headers[o:o + 40] = b'\0' * 40
    # drop the DYNAMIC_BASE flag so analysis tools keep the preferred base
    dllch = struct.unpack_from('<H', headers, opt + 70)[0]
    struct.pack_into('<H', headers, opt + 70, dllch & ~0x40)
    open(out, 'wb').write(bytes(headers) + bytes(body))
    print('wrote %s: .text window 0x%X-0x%X (%d KB), %d sections'
          % (out, lo, hi, (hi - lo) // 1024, len(keep)))


if __name__ == '__main__':
    main()
