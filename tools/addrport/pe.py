"""Minimal PE32 reader used by the address tools (no external deps)."""
import hashlib
import struct


class PE:
    def __init__(self, path):
        self.path = path
        self.data = open(path, 'rb').read()
        self.sha256 = hashlib.sha256(self.data).hexdigest()
        d = self.data
        pe = struct.unpack_from('<I', d, 0x3c)[0]
        assert d[pe:pe + 4] == b'PE\0\0'
        nsec = struct.unpack_from('<H', d, pe + 6)[0]
        optsz = struct.unpack_from('<H', d, pe + 20)[0]
        opt = pe + 24
        self.image_base = struct.unpack_from('<I', d, opt + 28)[0]
        self.sections = []
        for i in range(nsec):
            o = opt + optsz + i * 40
            name = d[o:o + 8].rstrip(b'\0').decode('latin1')
            vsz, rva, rsz, raw = struct.unpack_from('<IIII', d, o + 8)
            chars = struct.unpack_from('<I', d, o + 36)[0]
            self.sections.append(dict(name=name, va=self.image_base + rva, vsize=vsz,
                                      raw=raw, rsize=rsz, chars=chars))

    def section(self, name):
        for s in self.sections:
            if s['name'] == name:
                return s
        return None

    def va2off(self, va):
        for s in self.sections:
            if s['va'] <= va < s['va'] + min(s['vsize'], s['rsize']):
                return s['raw'] + (va - s['va'])
        return None

    def read(self, va, n):
        o = self.va2off(va)
        if o is None:
            return None
        return self.data[o:o + n]

    def u32(self, va):
        b = self.read(va, 4)
        return None if b is None or len(b) < 4 else struct.unpack('<I', b)[0]

    def in_text(self, va):
        t = self.section('.text')
        return t['va'] <= va < t['va'] + t['vsize']
