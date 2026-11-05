# Finds exports by name in a decompressed UE3 package image (version 581) and prints their outer chain,
# class, flags and serial range. Usage: upk_find.py image.bin Name [Name...]
import struct, sys

d = open(sys.argv[1], 'rb').read()

def fstring(o):
    n = struct.unpack_from('<i', d, o)[0]; o += 4
    if n < 0:
        return d[o:o - n * 2].decode('utf-16-le').rstrip('\0'), o - n * 2
    return d[o:o + n].decode('latin-1').rstrip('\0'), o + n

o = 12
_, o = fstring(o)
flags, name_count, name_off, exp_count, exp_off, imp_count, imp_off = struct.unpack_from('<7I', d, o)
names = []
p = name_off
for _ in range(name_count):
    s, p = fstring(p); p += 8
    names.append(s)
imports = []
p = imp_off
for _ in range(imp_count):
    class_pkg, _, class_name, _, outer, obj_name, _ = struct.unpack_from('<iiiiiii', d, p); p += 28
    imports.append((names[class_name], names[obj_name], outer))
exports = []
p = exp_off
for _ in range(exp_count):
    cls, sup, outer, name, number, arch = struct.unpack_from('<iiiiii', d, p); p += 24
    obj_flags, size, offset, exp_flags, net_count = struct.unpack_from('<QiiIi', d, p); p += 24
    p += net_count * 4 + 16 + 4
    exports.append(dict(cls=cls, sup=sup, outer=outer, name=names[name], flags=obj_flags, size=size, offset=offset))

def ref(i):
    if i > 0: return exports[i - 1]['name']
    if i < 0: return imports[-i - 1][1]
    return 'None'

def path(i):
    parts = []
    while i > 0:
        parts.append(exports[i - 1]['name']); i = exports[i - 1]['outer']
    return '.'.join(reversed(parts))

for want in sys.argv[2:]:
    for idx, e in enumerate(exports, 1):
        if e['name'] == want:
            print(f"{path(idx)}  class={ref(e['cls'])}  flags=0x{e['flags']:016x}  serial={e['offset']}+{e['size']}")
