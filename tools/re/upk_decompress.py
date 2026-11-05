# Decompresses a zlib-compressed UE3 package (MoH 2010, version 581) into a flat image:
# every compressed chunk is written at its uncompressed offset. Usage: upk_decompress.py in.upk out.bin
import struct, sys, zlib

def fstring_skip(d, o):
    n = struct.unpack_from('<i', d, o)[0]
    return o + 4 + (-n * 2 if n < 0 else n)

data = open(sys.argv[1], 'rb').read()
o = 12
o = fstring_skip(data, o)
o += 32                                   # flags, name/export/import counts+offsets, depends
o += 16                                   # guid
gen_count = struct.unpack_from('<i', data, o)[0]; o += 4 + gen_count * 12
o += 8                                    # engine version, cooker version
comp_flags, chunk_count = struct.unpack_from('<Ii', data, o); o += 8
chunks = [struct.unpack_from('<4I', data, o + i * 16) for i in range(chunk_count)]
if comp_flags != 1:
    sys.exit(f'compression flags {comp_flags} (only zlib=1 supported)')
size = max(u_off + u_size for u_off, u_size, _, _ in chunks)
out = bytearray(size)
out[:min(len(data), chunks[0][0])] = data[:chunks[0][0]]
for u_off, u_size, c_off, c_size in chunks:
    p = c_off
    tag, block_size, total_c, total_u = struct.unpack_from('<4I', data, p); p += 16
    blocks = []
    remaining = total_u
    while remaining > 0:
        cs, us = struct.unpack_from('<II', data, p); p += 8
        blocks.append((cs, us)); remaining -= us
    w = u_off
    for cs, us in blocks:
        chunk = zlib.decompress(data[p:p + cs]); p += cs
        out[w:w + len(chunk)] = chunk; w += len(chunk)
open(sys.argv[2], 'wb').write(out)
print(f'{sys.argv[1]}: {chunk_count} chunks -> {size} bytes')
