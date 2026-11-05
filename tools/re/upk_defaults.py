# Prints the tagged properties of an export (e.g. Default__MOHAHUDBase) in a decompressed package.
# Usage: upk_defaults.py image.bin ExportName [filter]
import struct, sys
sys.path.insert(0, __import__('os').path.dirname(__file__))
from upk_lib import Package

p = Package(sys.argv[1])
want = sys.argv[2]
flt = sys.argv[3].lower() if len(sys.argv) > 3 else ''
for i, e in enumerate(p.exports, 1):
    if e['name'] != want:
        continue
    d = p.data(i)
    o = 4  # NetIndex
    while o + 8 <= len(d):
        name = p.names[struct.unpack_from('<i', d, o)[0]]; o += 8
        if name == 'None':
            break
        typ = p.names[struct.unpack_from('<i', d, o)[0]]; o += 8
        size, idx = struct.unpack_from('<ii', d, o); o += 8
        extra = ''
        if typ == 'BoolProperty':
            val = d[o]; o += 1
        elif typ == 'StructProperty':
            extra = p.names[struct.unpack_from('<i', d, o)[0]]; o += 8
            val = d[o:o + size].hex(); o += size
        elif typ == 'ByteProperty':
            extra = p.names[struct.unpack_from('<i', d, o)[0]]; o += 8
            val = d[o:o + size].hex(); o += size
        else:
            raw = d[o:o + size]; o += size
            if typ == 'FloatProperty': val = struct.unpack('<f', raw)[0]
            elif typ == 'IntProperty': val = struct.unpack('<i', raw)[0]
            elif typ == 'NameProperty': val = p.names[struct.unpack_from('<i', raw)[0]]
            elif typ == 'ObjectProperty': val = p.ref(struct.unpack_from('<i', raw)[0])
            elif typ == 'StrProperty': val = raw[4:].decode('latin-1', 'replace').rstrip('\0')
            else: val = raw.hex()
        if not flt or flt in name.lower():
            print(f'{name}[{idx}] {typ} {extra} = {val}')
    break
