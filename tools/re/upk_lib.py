# Minimal reader for decompressed UE3 package images (MoH 2010, version 581).
import struct

class Package:
    def __init__(self, path):
        self.d = d = open(path, 'rb').read()
        o = 12
        _, o = self.fstring(o)
        (flags, name_count, name_off, exp_count, exp_off, imp_count, imp_off) = struct.unpack_from('<7I', d, o)
        self.names = []
        p = name_off
        for _ in range(name_count):
            s, p = self.fstring(p); p += 8
            self.names.append(s)
        self.imports = []
        p = imp_off
        for _ in range(imp_count):
            _, _, class_name, _, outer, obj_name, _ = struct.unpack_from('<iiiiiii', d, p); p += 28
            self.imports.append(dict(cls=self.names[class_name], name=self.names[obj_name], outer=outer))
        self.exports = []
        p = exp_off
        for _ in range(exp_count):
            cls, sup, outer, name, number, arch = struct.unpack_from('<iiiiii', d, p); p += 24
            obj_flags, size, offset, exp_flags, net_count = struct.unpack_from('<QiiIi', d, p); p += 24
            p += net_count * 4 + 16 + 4
            self.exports.append(dict(cls=cls, sup=sup, outer=outer, name=self.names[name], flags=obj_flags, size=size, offset=offset))

    def fstring(self, o):
        n = struct.unpack_from('<i', self.d, o)[0]; o += 4
        if n < 0:
            return self.d[o:o - n * 2].decode('utf-16-le').rstrip('\0'), o - n * 2
        return self.d[o:o + n].decode('latin-1').rstrip('\0'), o + n

    def ref(self, i):
        if i > 0: return self.exports[i - 1]['name']
        if i < 0: return self.imports[-i - 1]['name']
        return 'None'

    def cls_name(self, e):
        return self.ref(e['cls'])

    def path(self, i):
        parts = []
        while i > 0:
            parts.append(self.exports[i - 1]['name']); i = self.exports[i - 1]['outer']
        return '.'.join(reversed(parts))

    def index_of(self, dotted):
        for i in range(1, len(self.exports) + 1):
            if self.path(i) == dotted:
                return i
        return 0

    def children(self, outer_index):
        return [i for i, e in enumerate(self.exports, 1) if e['outer'] == outer_index]

    def data(self, i):
        e = self.exports[i - 1]
        return self.d[e['offset']:e['offset'] + e['size']]
