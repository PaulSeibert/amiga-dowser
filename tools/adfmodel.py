#!/usr/bin/env python3
"""An independent reader for 880K Amiga disk images: what Dowser should report.

    adfmodel.py IMAGE.adf [info|map|files]

Written separately from Dowser's C code (src/fs.c) so the two can be checked against each
other. Prints the same formats as "dowser CMD INFO / MAP / FILES".
"""
import struct
import sys

NB = 1760
KINDS = {'unseen': ' ', 'free': '.', 'boot': 'B', 'root': 'R', 'bitmap': 'M', 'dir': 'D', 'file': 'H',
         'ext': 'E', 'data': 'd', 'dirc': 'C', 'link': 'L', 'lost': '?', 'unread': 'X'}


def u32(b, o):
    return struct.unpack_from('>I', b, o)[0]


def s32(b, o):
    return struct.unpack_from('>i', b, o)[0]


def csum_ok(b):
    return sum(struct.unpack('>128I', b)) & 0xFFFFFFFF == 0


class Model:
    def __init__(self, path, bad=()):
        self.bad = set(bad)      # blocks to treat as unreadable (a damaged floppy)
        data = open(path, 'rb').read()
        assert len(data) == NB * 512
        self.blk = [data[i * 512:(i + 1) * 512] for i in range(NB)]
        self.dostype = u32(self.blk[0], 0)
        self.dos = (self.dostype >> 8) == 0x444F53 and (self.dostype & 0xFF) <= 5
        self.ffs = self.dos and bool(self.dostype & 1)
        self.dirc = self.dos and bool(self.dostype & 4)
        self.kind = ['unseen'] * NB
        self.owner = [None] * NB
        self.twice = set()
        self.used = [False] * NB
        self.objs = []          # dicts: name, kind, parent, header, size, nblocks, frags
        if self.dos:
            self.read()

    def name(self, b):
        n = b[0x1B0]
        return b[0x1B1:0x1B1 + min(n, 30)].decode('latin-1')

    def claim(self, b, kind, o):
        if not 2 <= b < NB:
            return
        if self.owner[b] is not None and self.owner[b] != o:
            self.twice.add(b)
        else:
            self.owner[b] = o
            self.kind[b] = kind

    def read(self):
        root = NB // 2
        rb = self.blk[root]
        self.kind[0] = self.kind[1] = 'boot'
        self.volname = self.name(rb)
        self.bmflag = s32(rb, 0x138)
        self.kind[root] = 'root'
        self.owner[root] = 0
        self.objs.append(dict(name=self.volname, kind='d', parent=None, header=root, size=0, nblocks=0, frags=0))
        for i in range(25):
            bm = u32(rb, 0x13C + i * 4)
            if not bm:
                break
            self.kind[bm] = 'bitmap'
            self.owner[bm] = 0
            b = self.blk[bm]
            for n in range(2, NB):
                word, bit = divmod(n - 2 - i * 127 * 32, 32)
                if 0 <= word < 127 and not (u32(b, 4 + word * 4) >> bit) & 1:
                    self.used[n] = True
        queue = [0]
        while queue:
            d = queue.pop(0)
            db = self.blk[self.objs[d]['header']]
            if self.dirc:
                c, steps = u32(db, 0x1F8), 0
                while c and 2 <= c < NB and steps < 200:
                    self.claim(c, 'dirc', d)
                    if u32(self.blk[c], 0) != 33:
                        break
                    c, steps = u32(self.blk[c], 16), steps + 1
            for slot in range(72):
                e = u32(db, 24 + slot * 4)
                steps = 0
                while e and steps < NB:
                    steps += 1
                    if not 2 <= e < NB or self.owner[e] is not None or e in self.bad:
                        break
                    h = self.blk[e]
                    if u32(h, 0) != 2:
                        break
                    st = s32(h, 0x1FC)
                    kind = {2: 'd', -3: 'f', 3: 's'}.get(st, 'h')
                    o = len(self.objs)
                    self.objs.append(dict(name=self.name(h), kind=kind, parent=d, header=e,
                                          size=u32(h, 0x144) if kind == 'f' else 0, nblocks=0, frags=0))
                    self.claim(e, {'d': 'dir', 'f': 'file'}.get(kind, 'link'), o)
                    nxt = u32(h, 0x1F0)
                    if kind == 'd':
                        queue.append(o)
                    elif kind == 'f':
                        self.datablocks(o)
                    e = nxt
        for b in range(2, NB):
            if self.owner[b] is None:
                self.kind[b] = 'lost' if self.used[b] else 'free'

    def datablocks(self, o):
        hdr = self.objs[o]['header']
        b, first, lst, steps = hdr, True, [], 0
        while b and steps < NB:
            steps += 1
            if not 2 <= b < NB:
                break
            if not first:
                self.claim(b, 'ext', o)
            if b in self.bad:
                break
            x = self.blk[b]
            if not first and (u32(x, 0) != 16 or u32(x, 0x1F4) != hdr):
                break
            cnt = s32(x, 8)
            if cnt < 0 or cnt > 72:
                cnt = 72
            for i in range(cnt):
                p = u32(x, 0x134 - i * 4)
                if p:
                    lst.append(p)
                    self.claim(p, 'data', o)
            b, first = u32(x, 0x1F8), False
        runs = 1 if lst else 0
        for i in range(1, len(lst)):
            if lst[i] != lst[i - 1] + 1:
                runs += 1
        self.objs[o]['blocks'] = lst
        self.objs[o]['nblocks'] = len(lst)
        self.objs[o]['frags'] = runs

    def path(self, o):
        parts = []
        while o:
            parts.append(self.objs[o]['name'])
            o = self.objs[o]['parent']
        return '/'.join(reversed(parts))

    def mapchar(self, b):
        unmarked = self.dos and b >= 2 and self.owner[b] is not None and not self.used[b]
        if b in self.twice or unmarked:
            return '!'
        return KINDS[self.kind[b]]

    def maplines(self):
        return [''.join(self.mapchar((cyl * 2 + row // 11) * 11 + row % 11) for cyl in range(80))
                for row in range(22)]

    def files(self):
        return [f"{self.path(i)}|{o['kind']}|{o['size']}|{o['nblocks']}|{o['frags']}|{o['header']}"
                for i, o in enumerate(self.objs) if i]

    def info(self):
        used = sum(1 for b in range(2, NB) if self.owner[b] is not None) + 2
        usedbm = sum(self.used[2:]) + 2
        return dict(dos=self.dos, dostype=self.dostype, name=getattr(self, 'volname', ''), used=used, usedbm=usedbm,
                    files=sum(o['kind'] == 'f' for o in self.objs[1:]),
                    dirs=sum(o['kind'] == 'd' for o in self.objs[1:]),
                    links=sum(o['kind'] in 'sh' for o in self.objs[1:]),
                    valid=getattr(self, 'bmflag', 0) == -1)


if __name__ == '__main__':
    m = Model(sys.argv[1])
    what = sys.argv[2] if len(sys.argv) > 2 else 'info'
    if what == 'map':
        print('\n'.join(m.maplines()))
    elif what == 'files':
        print('\n'.join(m.files()))
    else:
        print(m.info())
