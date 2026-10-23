#!/usr/bin/env python3
"""Print the hashes SporeKit uses to pick symbol files for a SporeApp.exe.

  exe_info.py path/to/SporeApp.exe

exe_sha256:  SHA-256 of the whole file (symbol files say "# exe_sha256=...").
code_sha256: SHA-256 over ImageBase (u32 LE), AddressOfEntryPoint (u32 LE), then for every section
             in table order except ".rsrc": Name (8 bytes), VirtualAddress (u32 LE), VirtualSize
             (u32 LE) and the raw bytes [PointerToRawData, + min(VirtualSize, SizeOfRawData)).
             Without the PE header, resources and appended data (signatures). When no symbol
             file names the exact exe, SporeKit loads the files whose "# code_sha256=" matches.
             Same definition as sporekit/sha256.cpp and installer/setup.cpp.
"""
import hashlib
import struct
import sys


def code_sha256(d):
    if d[:2] != b'MZ':
        return None
    pe = struct.unpack_from('<I', d, 0x3C)[0]
    if d[pe:pe + 4] != b'PE\0\0' or struct.unpack_from('<H', d, pe + 24)[0] != 0x10B:
        return None
    nsec, optsz = struct.unpack_from('<H', d, pe + 6)[0], struct.unpack_from('<H', d, pe + 20)[0]
    opt = pe + 24
    h = hashlib.sha256()
    h.update(struct.pack('<II', struct.unpack_from('<I', d, opt + 28)[0], struct.unpack_from('<I', d, opt + 16)[0]))
    for i in range(nsec):
        o = opt + optsz + i * 40
        name = d[o:o + 8]
        if name == b'.rsrc\0\0\0':
            continue
        vsize, va, rsize, raw = struct.unpack_from('<IIII', d, o + 8)
        n = min(vsize, rsize)
        h.update(name + struct.pack('<II', va, vsize) + d[raw:raw + n])
    return h.hexdigest()


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    d = open(sys.argv[1], 'rb').read()
    print(f'size         {len(d)} bytes')
    print(f'exe_sha256   {hashlib.sha256(d).hexdigest()}')
    print(f'code_sha256  {code_sha256(d)}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
