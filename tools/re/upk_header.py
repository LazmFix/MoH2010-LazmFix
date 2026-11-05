# Prints the header of a UE3 package (MoH 2010: version 581) and its compressed chunk table.
import struct, sys

def fstring(d, o):
    n = struct.unpack_from('<i', d, o)[0]; o += 4
    if n < 0:
        s = d[o:o - n * 2].decode('utf-16-le', 'replace'); o += -n * 2
    else:
        s = d[o:o + n].decode('latin-1'); o += n
    return s.rstrip('\0'), o

d = open(sys.argv[1], 'rb').read(4096)
o = 0
tag, ver, header_size = struct.unpack_from('<IIi', d, o); o += 12
folder, o = fstring(d, o)
flags, name_count, name_off, exp_count, exp_off, imp_count, imp_off, depends_off = struct.unpack_from('<8I', d, o); o += 32
print(f'version {ver & 0xffff} licensee {ver >> 16} header {header_size} folder {folder!r} flags 0x{flags:08x}')
print(f'names {name_count}@{name_off} exports {exp_count}@{exp_off} imports {imp_count}@{imp_off}')
rest = d[o:o + 96]
print('next ints:', [struct.unpack_from('<I', rest, i)[0] for i in range(0, 96, 4)])
