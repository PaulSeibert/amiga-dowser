#!/usr/bin/env python3
"""Make Dowser's recovery test images from the ones mkdisks.py and xdftool made.

    mkdeleted.py DISKDIR

Writes into DISKDIR:
  r-delofs.adf, r-delffs.adf  readme, sub/a.txt and big.bin deleted (by xdftool, in make.sh)
  r-over.adf     r-delffs with one of big.bin's blocks taken by "small" (partly overwritten)
  r-noroot.adf   ffs with the root block zeroed: every file is an orphan
  r-noboot.adf   ofs with the boot block zeroed: Dowser must still see an OFS disk
  r-dmg.adf      ofs with readme's second data block zeroed
  r-tracks.adf   ffs as an extended ADF (UAE-1ADF) whose tracks 100 and 101 hold raw data with
                 no sector headers: unreadable through trackdisk, like a damaged floppy
  expect-rescue.json  what the checker needs: zeroed ranges, blocks taken, bad tracks
"""
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from adfmodel import Model  # noqa: E402

d = sys.argv[1]
M32 = 0xFFFFFFFF
expect = {}


def load(n):
    return bytearray(open(os.path.join(d, n + '.adf'), 'rb').read())


def save(n, data):
    assert len(data) == 901120
    open(os.path.join(d, n + '.adf'), 'wb').write(bytes(data))


def u32(x, b, o):
    return struct.unpack_from('>I', x, b * 512 + o)[0]


def p32(x, b, o, v):
    struct.pack_into('>I', x, b * 512 + o, v & M32)


def fixsum(x, b, at=20):
    p32(x, b, at, 0)
    p32(x, b, at, -(sum(struct.unpack_from('>128I', x, b * 512)) & M32))


def model(x):
    p = os.path.join(d, '_tmp.adf')
    open(p, 'wb').write(bytes(x))
    m = Model(p)
    os.remove(p)
    return m


def obj(m, path):
    for i, o in enumerate(m.objs):
        if i and m.path(i).lower() == path.lower():
            return o
    raise KeyError(path)


def datablocks(x, hdr):
    """big.bin's data blocks, read from its (deleted, but intact) header and extensions"""
    out, b, first = [], hdr, True
    while b:
        cnt = u32(x, b, 8)
        out += [u32(x, b, 0x134 - i * 4) for i in range(min(cnt, 72))]
        b = u32(x, b, 0x1F8)
    return [v for v in out if v]


# r-over: one of deleted big.bin's blocks given to "small"
base = load('ffs')
bighdr = obj(model(base), 'big.bin')['header']
x = load('r-delffs')
taken = datablocks(x, bighdr)[2]                   # big.bin's third data block
hs = obj(model(x), 'small')['header']
p32(x, hs, 0x134, taken)
fixsum(x, hs)
bm = u32(x, 880, 0x13C)
w, bit = divmod(taken - 2, 32)
p32(x, bm, 4 + w * 4, u32(x, bm, 4 + w * 4) & ~(1 << bit))   # now in use
fixsum(x, bm, 0)
save('r-over', x)
expect['r-over'] = dict(zero=[[2 * 512, 512]])     # big.bin: bytes 1024-1535 come back as zeros

# r-noroot: the root block gone
x = load('ffs')
x[880 * 512:881 * 512] = bytes(512)
save('r-noroot', x)

# r-noboot: the boot block gone
x = load('ofs')
x[0:1024] = bytes(1024)
save('r-noboot', x)

# r-dmg: readme's second OFS data block zeroed
x = load('ofs')
second = u32(x, obj(model(x), 'readme')['header'], 0x130)
x[second * 512:(second + 1) * 512] = bytes(512)
save('r-dmg', x)
expect['r-dmg'] = dict(zero=[[488, 488]])          # readme: its second 488 bytes become zeros

# r-tracks: an extended ADF with two raw tracks that have no sector headers
x = load('ffs')
bad = [100, 101]
heads, datas = [], []
for t in range(160):
    if t in bad:
        raw = b'\xaa' * 12668                         # MFM for zeros, and never the sync word
        heads.append(struct.pack('>HHII', 0, 1, len(raw), len(raw) * 8))
        datas.append(raw)
    else:
        trk = bytes(x[t * 5632:(t + 1) * 5632])
        heads.append(struct.pack('>HHII', 0, 0, 5632, 5632 * 8))
        datas.append(trk)
open(os.path.join(d, 'r-tracks.adf'), 'wb').write(b'UAE-1ADF' + struct.pack('>HH', 0, 160) + b''.join(heads) + b''.join(datas))
expect['r-tracks'] = dict(badtracks=bad)
json.dump(expect, open(os.path.join(d, 'expect-rescue.json'), 'w'), indent=1)
print('ok')
