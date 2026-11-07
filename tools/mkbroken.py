#!/usr/bin/env python3
"""Make damaged copies of Dowser's test images, each broken in one known way.

    mkbroken.py DISKDIR [WB13_ADF]

Reads DISKDIR/ofs.adf, ffs.adf, ffsdc.adf (from mkdisks.py and xdftool), writes
DISKDIR/c-*.adf and DISKDIR/expect-check.json: for each image the findings that must appear
("require"), the ones that may appear as a consequence ("allow"), and the boot block report.
WB13_ADF (your own Workbench 1.3 disk) gives a standard 1.x boot block to compare with; the
test image made from it stays local, like every other test image.
"""
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from adfmodel import Model  # noqa: E402

d = sys.argv[1]
wb13 = sys.argv[2] if len(sys.argv) > 2 else None
M32 = 0xFFFFFFFF
expect = {}


def load(name):
    return bytearray(open(os.path.join(d, name + '.adf'), 'rb').read())


def save(name, data, require, allow=(), boot=None):
    assert len(data) == 901120, f'{name}: wrong size {len(data)}'   # (slice assignment can resize)
    open(os.path.join(d, 'c-' + name + '.adf'), 'wb').write(bytes(data))
    expect['c-' + name] = dict(require=list(require), allow=list(allow), boot=boot or {})


def u32(data, blk, off):
    return struct.unpack_from('>I', data, blk * 512 + off)[0]


def p32(data, blk, off, v):
    struct.pack_into('>I', data, blk * 512 + off, v & M32)


def fixsum(data, blk, at=20):
    """header-style checksum: all 128 longs add up to 0"""
    p32(data, blk, at, 0)
    s = sum(struct.unpack_from('>128I', data, blk * 512)) & M32
    p32(data, blk, at, -s)


def fixboot(data):
    p32(data, 0, 4, 0)
    s = 0
    for i in range(256):
        s += struct.unpack_from('>I', data, i * 4)[0]
        if s > M32:
            s = (s & M32) + 1
    p32(data, 0, 4, ~s)


def model(data, name='tmp'):
    p = os.path.join(d, '_tmp.adf')
    open(p, 'wb').write(bytes(data))
    m = Model(p)
    os.remove(p)
    return m


def header(m, path):
    for i, o in enumerate(m.objs):
        if i and m.path(i).lower() == path.lower():
            return o['header']
    raise KeyError(path)


def firstdata(data, hdr):
    return u32(data, hdr, 0x134)


def setbit(data, b, free):
    """the bitmap bit of block b (bitmap at block 881 on these images): set = free"""
    bm = u32(data, 880, 0x13C)
    w, bit = divmod(b - 2, 32)
    v = u32(data, bm, 4 + w * 4)
    v = v | (1 << bit) if free else v & ~(1 << bit)
    p32(data, bm, 4 + w * 4, v)
    fixsum(data, bm, 0)


ofs, ffs = load('ofs'), load('ffs')
mo, mf = model(ofs), model(ffs)

# 1. root checksum wrong (a changed byte in the disk name, checksum not fixed)
x = ofs[:]
x[880 * 512 + 0x1B2] ^= 0x01
save('rootcsum', x, ['ROOTCSUM'])

# 2. a file header's checksum wrong (a byte of its comment area)
x = ofs[:]
h = header(mo, 'readme')
x[h * 512 + 0x14A] ^= 0x40
save('hdrcsum', x, ['HDRCSUM'])

# 3. an OFS data block's checksum wrong (a data byte)
x = ofs[:]
db = firstdata(x, header(mo, 'readme'))
x[db * 512 + 40] ^= 0x20
save('datacsum', x, ['DATACSUM'])

# 4. a block in use but marked free in the bitmap
x = ffs[:]
setbit(x, firstdata(x, header(mf, 'readme')), True)
save('unmarked', x, ['UNMARKED'])

# 5. a free block marked in use (lost space)
x = ffs[:]
setbit(x, 1700, False)
save('lost', x, ['LOST'])

# 6. two files sharing a block: small's data pointer set to readme's first data block
x = ffs[:]
hs = header(mf, 'small')
p32(x, hs, 0x134, firstdata(x, header(mf, 'readme')))
fixsum(x, hs)
save('twice', x, ['TWICE'], ['LOST'])

# 7. a drawer entry pointing outside the disk
x = ofs[:]
slot = next(s for s in range(72) if u32(x, 880, 24 + s * 4) == 0)
p32(x, 880, 24 + slot * 4, 5000)
fixsum(x, 880)
save('chainrange', x, ['CHAINRANGE'])

# 8. a wrong parent pointer
x = ffs[:]
hs = header(mf, 'small')
p32(x, hs, 0x1F4, 881)
fixsum(x, hs)
save('parent', x, ['PARENT'])

# 9. an entry in the wrong hash slot (moved to an empty slot)
x = ffs[:]
src = next(s for s in range(72) if u32(x, 880, 24 + s * 4) and u32(x, u32(x, 880, 24 + s * 4), 0x1F0) == 0)
dst = next(s for s in range(72) if u32(x, 880, 24 + s * 4) == 0)
p32(x, 880, 24 + dst * 4, u32(x, 880, 24 + src * 4))
p32(x, 880, 24 + src * 4, 0)
fixsum(x, 880)
save('hashslot', x, ['HASHSLOT'])

# 10. the bitmap marked not valid
x = ffs[:]
p32(x, 880, 0x138, 0)
fixsum(x, 880)
save('bminvalid', x, ['BMINVALID'])

# 11. a bitmap block's checksum wrong (a bit past the end of the disk, so no block changes)
x = ffs[:]
bm = u32(x, 880, 0x13C)
x[bm * 512 + 4 + 100 * 4] ^= 0x01
save('bmcsum', x, ['BMCSUM'])

# 12. an OFS data block with the wrong sequence number (checksum fixed)
x = ofs[:]
hr = header(mo, 'readme')
second = u32(x, hr, 0x130)
p32(x, second, 8, 7)
fixsum(x, second)
save('seq', x, ['DATASEQ'])

# 13. a file size that doesn't match its blocks
x = ffs[:]
hr = header(mf, 'readme')
p32(x, hr, 0x144, 99999)
fixsum(x, hr)
save('size', x, ['SIZE'])

# 14. an entry pointing at a block that isn't a header (small's header zeroed)
x = ffs[:]
hs = header(mf, 'small')
x[hs * 512:(hs + 1) * 512] = bytes(512)
save('chaintype', x, ['CHAINTYPE'], ['LOST'])

# 15. a broken extension block (type changed, checksum fixed)
x = ofs[:]
hb = header(mo, 'big.bin')
ext = u32(x, hb, 0x1F8)
p32(x, ext, 0, 0)
fixsum(x, ext)
save('extbad', x, ['EXTBAD'], ['LOST', 'SIZE'])

# 16. a directory cache out of date (one record fewer counted)
x = load('ffsdc')
dc = u32(x, 880, 0x1F8)
p32(x, dc, 12, u32(x, dc, 12) - 1)
fixsum(x, dc)
save('dircache', x, ['DIRCACHE'])

# boot blocks: custom code with a wrong checksum, custom code that boots, the 1.x Install code
x = ofs[:]
x[12:12 + 34] = b'Hello from a custom boot block!!\0\0'
save('bootsum', x, ['BOOTSUM'], boot=dict(code='custom', checksum='wrong', text='Hello from a custom boot block!!'))
x = ffs[:]
code = b'\x4e\x75Dowser test boot block, harmless\0'      # (rts first: it does nothing)
x[12:12 + len(code)] = code
fixboot(x)
save('bootcustom', x, [], boot=dict(code='custom', checksum='ok', text='Dowser test boot block, harmless'))
if wb13:
    x = ofs[:]
    x[0:1024] = open(wb13, 'rb').read(1024)
    save('boot13', x, [], boot=dict(code='standard boot code (Kickstart 1.x Install)', checksum='ok'))

# the clean images: nothing to report (and the right boot block)
for n in ('ofs', 'ffs', 'ofsintl', 'ffsintl', 'ofsdc', 'ffsdc', 'frag', 'full', 'blank'):
    expect[n] = dict(require=[], allow=[], boot=dict(code='no boot code'))
expect['boot20'] = dict(require=[], allow=[], boot=dict(code='standard boot code (Kickstart 2.0+ Install)', checksum='ok'))
expect['raw-random'] = dict(require=['NOTDOS'], allow=[], boot={})
expect['raw-kick'] = dict(require=['NOTDOS'], allow=[], boot={})
json.dump(expect, open(os.path.join(d, 'expect-check.json'), 'w'), indent=1)
print(' '.join(sorted(k for k in expect)))
