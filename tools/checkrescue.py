#!/usr/bin/env python3
"""Check the results of testrescue.sh (Dowser milestone 4: recovery, rescue, surface scan).

    checkrescue.py [SHARE] [PLAY]

Every rescued file is compared byte for byte with what an independent reader
(tools/adfmodel.py) decodes from the original image, with zeros exactly where blocks were
destroyed, overwritten or unreadable.
"""
import hashlib
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from adfmodel import Model  # noqa: E402

share = sys.argv[1] if len(sys.argv) > 1 else '/mnt/j/amiga/share'
play = sys.argv[2] if len(sys.argv) > 2 else '/mnt/j/amiga/play'
rs = os.path.join(share, 'rs')
fails = 0
expect = json.load(open(os.path.join(rs, 'expect-rescue.json')))


def read(name):
    p = os.path.join(share, name)
    return open(p, encoding='latin-1').read() if os.path.exists(p) else None


def lines(name):
    return [l for l in (read(name) or '').splitlines() if l.strip()]


def check(ok, what, detail=''):
    global fails
    print(('PASS ' if ok else 'FAIL ') + what + (('  -- ' + str(detail)) if detail and not ok else ''))
    if not ok:
        fails += 1


def filedata(m, img, o, bad=(), zero=()):
    """a file's contents from the image; zeros for blocks in bad, and for the byte ranges in zero"""
    per = 512 if m.ffs else 488
    out = bytearray()
    for b in o['blocks']:
        want = min(per, o['size'] - len(out))
        if want <= 0:
            break
        blk = img[b * 512:(b + 1) * 512]
        out += bytes(want) if b in bad else (blk[:want] if m.ffs else blk[24:24 + want])
    out += bytes(o['size'] - len(out))
    for start, n in zero:
        out[start:start + n] = bytes(n)
    return bytes(out)


def expected_tree(image, bad=(), only=None):
    """{path: bytes or None for drawers} for the whole disk (or below the drawer 'only')"""
    m = Model(os.path.join(rs, image + '.adf'), bad)
    img = open(os.path.join(rs, image + '.adf'), 'rb').read()
    out = {}
    for i, o in enumerate(m.objs):
        if not i:
            continue
        p = m.path(i)
        if only:
            if not (p.lower() == only.lower() or p.lower().startswith(only.lower() + '/')):
                continue
        out[p.lower()] = None if o['kind'] == 'd' else filedata(m, img, o, bad)
    return out, m, img


def host_tree(d):
    out = {}
    root = os.path.join(rs, d)
    for dp, dirs, files in os.walk(root):
        for n in dirs:
            out[os.path.relpath(os.path.join(dp, n), root).replace(os.sep, '/').lower()] = None
        for n in files:
            if n.startswith('_UAEFSDB'):
                continue
            out[os.path.relpath(os.path.join(dp, n), root).replace(os.sep, '/').lower()] = open(os.path.join(dp, n), 'rb').read()
    return out


def same_tree(got, want, what):
    names = set(got) ^ set(want)
    diff = [k for k in set(got) & set(want) if got[k] != want[k]]
    check(not names and not diff and want, what, (sorted(names)[:5], sorted(diff)[:5]))


def found(name):
    """DELETED output -> {name: (kind, size, status, how, where)}"""
    out = {}
    for l in lines(name):
        p = l.split('|')
        if len(p) == 7:
            out[p[0].lower()] = (p[1], int(p[2]), p[3], p[4], p[5])
    return out


# ---- deleted files ----
for img, step in (('ofs', 'delofs'), ('ffs', 'delffs')):
    f = found(f'rs-{step}-del.txt')
    want = {'readme': ('f', 2880, 'recoverable', 'deleted', ''), 'a.txt': ('f', 3000, 'recoverable', 'deleted', 'sub'),
            'big.bin': ('f', 200000, 'recoverable', 'deleted', '')}
    check(all(f.get(k) == v for k, v in want.items()), f'DELETED {img}: readme, sub/a.txt, big.bin, recoverable, where they were',
          {k: f.get(k) for k in want})
base = {n: expected_tree(n) for n in ('ofs', 'ffs')}
same_tree(host_tree('o-d1'), {'readme': base['ofs'][0]['readme']}, 'undelete one file (OFS): byte for byte')
want3 = {k: v for k, v in base['ofs'][0].items() if k in ('readme', 'sub', 'sub/a.txt', 'big.bin')}
same_tree(host_tree('o-d2'), want3, 'undelete all (OFS), into the drawers they were in')
want3 = {k: v for k, v in base['ffs'][0].items() if k in ('readme', 'sub', 'sub/a.txt', 'big.bin')}
same_tree(host_tree('o-d3'), want3, 'undelete all (FFS), into the drawers they were in')
same_tree(host_tree('o-d2b'), host_tree('o-d2'), 'second round: undelete all again, the same')

# ---- partly overwritten ----
f = found('rs-over-del.txt')
check(f.get('big.bin', ('',) * 5)[2] == 'partly overwritten', 'a deleted file whose block was reused is "partly overwritten"', f.get('big.bin'))
_, mf, imgf = base['ffs']
big = next(o for i, o in enumerate(mf.objs) if i and mf.path(i) == 'big.bin')
same_tree(host_tree('o-d4'), {'big.bin': filedata(mf, imgf, big, zero=expect['r-over']['zero'])},
          'rescue of the partly overwritten file: zeros just where the block was reused')
check('could not be read, written as zeros: 1' in (read('rs-s4.txt') or ''), 'it reports the block', read('rs-s4.txt'))

# ---- lost root, lost boot block, damaged data block ----
f = found('rs-noroot-del.txt')
check(len(f) >= 60 and all(v[3] == 'orphan' for v in f.values()), f'lost root: {len(f)} files and drawers found as orphans', list(f.items())[:3])
same_tree(host_tree('o-noroot'), base['ffs'][0], 'lost root: the whole tree rebuilt from the headers, byte for byte')
rep = read('rs-noboot.txt') or ''
check('type: DOS\\0 (OFS)' in rep and '[BOOTDOS]' in rep, 'lost boot block: still seen as OFS, with a warning', rep[:300])
same_tree(host_tree('o-noboot'), base['ofs'][0], 'lost boot block: everything rescued')
_, mo, imgo = base['ofs']
readme = next(o for i, o in enumerate(mo.objs) if i and mo.path(i) == 'readme')
same_tree(host_tree('o-dmg'), {'readme': filedata(mo, imgo, readme, zero=expect['r-dmg']['zero'])},
          'damaged OFS data block: zeros just there')

# ---- rescuing from good disks ----
same_tree(host_tree('o-ffs'), base['ffs'][0], 'rescue everything (FFS), byte for byte')
intl, _, _ = expected_tree('ofsintl')
got = host_tree('o-intl')
check(len(got) == len(intl) and all(got.get(k) == v for k, v in intl.items() if k.isascii()),
      'rescue everything (international OFS, Latin-1 names)', sorted(set(got) ^ set(intl))[:4])
sub, _, _ = expected_tree('ffs', only='sub')
same_tree(host_tree('o-sub'), sub, 'rescue one drawer with everything in it')
check('no drawer' in (read('rs-s11.txt') or ''), 'a destination that does not exist is refused', read('rs-s11.txt'))

# ---- surface scan and unreadable tracks ----
bad = expect['r-tracks']['badtracks']
check('every track can be read' in (read('rs-scanimg.txt') or ''), 'SCAN of an image file explains itself', read('rs-scanimg.txt'))
s1 = lines('rs-scan1.txt')
check(any(l.startswith('track 100 ') for l in s1) and any(l.startswith('track 101 ') for l in s1)
      and s1[-1].startswith('result: 160 tracks read, 158 fine'), 'SCAN finds the two unreadable tracks (100, 101)', s1)
badblocks = {t * 11 + s for t in bad for s in range(11)}
wantt, mt, _ = expected_tree('ffs', bad=badblocks)
unread = [l for l in lines('rs-files1.txt') if l.endswith('|1') or l.endswith('|3')]
check(unread, f'FILES through the drive marks {len(unread)} files with unreadable blocks', lines('rs-files1.txt')[:3])
same_tree(host_tree('o-tracks'), wantt, 'rescue from the damaged floppy: zeros exactly in the unreadable blocks')
same_tree(host_tree('o-tracksb'), wantt, 'second round: the same again')
check('158 fine' not in (read('rs-scan2.txt') or '') and 'result: 160 tracks read, 160 fine' in (read('rs-scan2.txt') or ''),
      'SCAN of a good floppy: all 160 fine', read('rs-scan2.txt'))
check('no disk' in (read('rs-scan3.txt') or ''), 'SCAN of an empty drive', read('rs-scan3.txt'))
check('will not rescue onto the disk being read' in (read('rs-s13.txt') or ''), 'rescuing onto the disk being read is refused',
      read('rs-s13.txt'))

for l in open(os.path.join(rs, 'hashes.txt')).read().splitlines():
    h, name = l.split()
    p = os.path.join(rs, name) if not name.startswith('dowser-') else os.path.join(play, name)
    check(os.path.exists(p) and hashlib.sha1(open(p, 'rb').read()).hexdigest() == h, f'unchanged: {name}')


def mem(name):
    t = read(name)
    return dict(zip(t.split()[::2], map(int, t.split()[1::2]))) if t else None


m1, m2, m3 = mem('rs-mem1.txt'), mem('rs-mem2.txt'), mem('rs-mem3.txt')
if m1 and m2 and m3:
    print(f"INFO free memory: start {m1['total']}, after round 1 {m2['total']}, after round 2 {m3['total']}")
    check(m2['total'] - m3['total'] < 1024 and m2['chip'] - m3['chip'] < 256,
          'no memory lost on a second round (no per-command loss)', (m1['total'] - m2['total'], m2['total'] - m3['total']))
check(read('rs-done.txt') is not None, 'test script ran to the end')
print('ALL PASSED' if not fails else f'{fails} FAILED')
sys.exit(1 if fails else 0)
