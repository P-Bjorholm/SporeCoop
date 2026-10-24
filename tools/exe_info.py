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
data_sha256: like code_sha256 without AddressOfEntryPoint and without the sections ".text" and
             ".bind": a packed exe (the Steam build's SteamStub DRM encrypts .text and moves the
             entry point into .bind) has the data of the build it was made from.
text_sha256: SHA-256 of the raw .text bytes [PointerToRawData, + min(VirtualSize, SizeOfRawData)):
             SporeKit compares it with the unpacked .text in memory (relocations undone) before it
             uses that build's symbols for a packed exe (sporekit/sha256.cpp, docs/05-re-notes.md).
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


def sections(d):
    pe = struct.unpack_from('<I', d, 0x3C)[0]
    nsec, optsz = struct.unpack_from('<H', d, pe + 6)[0], struct.unpack_from('<H', d, pe + 20)[0]
    opt = pe + 24
    for i in range(nsec):
        o = opt + optsz + i * 40
        vsize, va, rsize, raw = struct.unpack_from('<IIII', d, o + 8)
        yield d[o:o + 8], va, vsize, d[raw:raw + min(vsize, rsize)]


def data_sha256(d):
    pe = struct.unpack_from('<I', d, 0x3C)[0]
    h = hashlib.sha256(struct.pack('<I', struct.unpack_from('<I', d, pe + 24 + 28)[0]))
    for name, va, vsize, raw in sections(d):
        if name in (b'.rsrc\0\0\0', b'.text\0\0\0', b'.bind\0\0\0'):
            continue
        h.update(name + struct.pack('<II', va, vsize) + raw)
    return h.hexdigest()


def text_sha256(d):
    for name, va, vsize, raw in sections(d):
        if name == b'.text\0\0\0':
            return hashlib.sha256(raw).hexdigest()
    return None


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    d = open(sys.argv[1], 'rb').read()
    print(f'size         {len(d)} bytes')
    print(f'exe_sha256   {hashlib.sha256(d).hexdigest()}')
    print(f'code_sha256  {code_sha256(d)}')
    print(f'data_sha256  {data_sha256(d)}')
    print(f'text_sha256  {text_sha256(d)}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
