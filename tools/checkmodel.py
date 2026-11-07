#!/usr/bin/env python3
"""Check the results of testmodel.sh (Dowser milestone 1: the disk model).

    checkmodel.py [SHARE] [PLAY]

Every image is checked three ways: Dowser's output against tools/adfmodel.py (an independent
Python reader), and that reader's file list against xdftool's own listing.
"""
import hashlib
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from adfmodel import Model  # noqa: E402

share = sys.argv[1] if len(sys.argv) > 1 else '/mnt/j/amiga/share'
play = sys.argv[2] if len(sys.argv) > 2 else '/mnt/j/amiga/play'
dm = os.path.join(share, 'dm')
fails = 0


def read(name):
    p = os.path.join(share, name)
    if not os.path.exists(p):
        # dm-X-info.txt etc. may be a section of dm-X-report.txt (CMD REPORT)
        base, _, sec = name[:-4].rpartition('-')
        rp = os.path.join(share, base + '-report.txt')
        if sec in ('info', 'map', 'files') and os.path.exists(rp):
            text = open(rp, encoding='latin-1').read()
            want = '[' + sec.upper() + ']'
            out, on = [], False
            for l in text.split('\n'):
                if len(l) > 2 and l[0] == '[' and l[-1] == ']' and l[1:-1].isupper():   # any section
                    on = l == want
                elif on:
                    out.append(l)
            return '\n'.join(out)
        return None
    return open(p, encoding='latin-1').read()


def lines(name):
    return [l for l in (read(name) or '').splitlines() if l.strip()]


def check(ok, what, detail=''):
    global fails
    print(('PASS ' if ok else 'FAIL ') + what + (('  -- ' + str(detail)) if detail and not ok else ''))
    if not ok:
        fails += 1


def info_fields(step):
    d = {}
    for l in lines(step):
        if ': ' in l:
            k, v = l.split(': ', 1)
            d[k] = v
    return d


def xdflist(path):
    """xdftool's listing -> {path: size or 'DIR'}"""
    out, stack = {}, []
    for l in open(path, encoding='utf-8', errors='replace').read().splitlines()[1:]:
        if not l.strip() or l.startswith('sum:') or l.startswith('data:') or l.startswith('fs:'):
            continue
        depth = (len(l) - len(l.lstrip(' '))) // 2 - 1
        parts = l.split()
        name, size = parts[0], parts[1]
        stack = stack[:depth] + [name]
        out['/'.join(stack)] = size
    return out


def compare(label, src_prefix, img):
    m = Model(os.path.join(dm, img + '.adf'))
    inf = info_fields(f'dm-{src_prefix}-info.txt')
    want = m.info()
    if not want['dos']:
        check('not an AmigaDOS disk' in inf.get('type', ''), f'{label}: INFO says not AmigaDOS', inf)
    else:
        got = (inf.get('name'), inf.get('blocks'), inf.get('files'))
        exp = (want['name'], f"1760, in use {want['used']}, bitmap says {want['usedbm']}, unreadable 0",
               f"{want['files']}, drawers: {want['dirs']}, links: {want['links']}")
        check(got == exp and inf.get('type', '').startswith(f"DOS\\{want['dostype'] & 0xFF}"),
              f"{label}: INFO (DOS\\{want['dostype'] & 0xFF}, {want['files']} files, {want['used']} blocks used)",
              f'{got} != {exp}')
    mp = lines(f'dm-{src_prefix}-map.txt') if want['dos'] else (read(f'dm-{src_prefix}-map.txt') or '').split('\n')[:22]
    check(mp == m.maplines() or (not want['dos'] and len(mp) == 22),
          f'{label}: MAP matches block for block',
          next((f'row {i}: {a!r} != {b!r}' for i, (a, b) in enumerate(zip(mp, m.maplines())) if a != b), len(mp)))
    got = sorted(l.rsplit('|', 1)[0] for l in lines(f'dm-{src_prefix}-files.txt'))
    status = {l.rsplit('|', 1)[1] for l in lines(f'dm-{src_prefix}-files.txt')}
    exp = sorted(m.files())
    check(got == exp and status <= {'0'}, f'{label}: FILES ({len(exp)} entries: sizes, blocks, fragments, headers)',
          [x for x in set(got) ^ set(exp)][:4] or status)
    return m


models = {}
images = [n[:-4] for n in sorted(os.listdir(dm)) if n.endswith('.adf')]
for img in images:
    models[img] = compare(img, img, img)
    lst = os.path.join(dm, img + '.list')
    if os.path.exists(lst):
        xl = xdflist(lst)
        mine = {m_path: m_size for m_path, m_size in
                ((m.split('|')[0], (m.split('|')[2] if m.split('|')[1] == 'f' else 'DIR')) for m in models[img].files())}
        check({k.lower(): v for k, v in xl.items()} == {k.lower(): v for k, v in mine.items()},
              f'{img}: the oracle agrees with xdftool\'s own listing', set(xl.items()) ^ set(mine.items()))

check(models['frag'].files() and any(f.split('|')[0] == 'fragmented.bin' and int(f.split('|')[4]) > 1
                                     for f in models['frag'].files()), 'frag image really has a fragmented file')
compare('DF1: (FFS image in a drive)', 'df1', 'ffs')
inf2 = info_fields('dm-df2-info.txt')
check(inf2.get('type', '').startswith('DOS\\2') and inf2.get('name') == 'Test ofsintl',
      'DF2: write-protected drive read (international OFS)', inf2)
got2 = sorted(l.rsplit('|', 1)[0] for l in lines('dm-df2-files.txt'))
check(got2 == sorted(models['ofsintl'].files()), 'DF2: files match the image (Latin-1 names)',
      set(got2) ^ set(models['ofsintl'].files()))
check('no disk' in (read('dm-df3-info.txt') or ''), 'DF3: no disk is reported', lines('dm-df3-info.txt'))
check('cannot open' in (read('dm-none-info.txt') or ''), 'a missing image is reported', lines('dm-none-info.txt'))
check('901120' in (read('dm-short-info.txt') or ''), 'a file that is not an 880K image is refused',
      lines('dm-short-info.txt'))
b0 = lines('dm-b0.txt')
check(b0[:1] == ['block 0: boot block'] and any(l.startswith('000: 444F5300') for l in b0) and
      any('boot block: no boot code' in l for l in b0), 'BLOCK 0: boot block, DOS\\0, decoded', b0[:4])
b880 = lines('dm-b880.txt')
check(b880[:1] == ['block 880: root block'] and 'name: "Test ofs"' in b880 and 'bitmap: valid, first bitmap block 881' in b880
      and any(l.startswith('hash table:') for l in b880), 'BLOCK 880: the root block decoded (name, bitmap, hash table)', b880[:8])
check(lines('dm-df1-b880.txt')[:1] == ['block 880: root block'], 'BLOCK through a drive')
for n in ('ofs', 'ffsdc', 'frag'):
    check(sorted(l.rsplit('|', 1)[0] for l in lines(f'dm-r2-{n}.txt')) == sorted(models[n].files()),
          f'second round: {n} the same')

# read-only: every image exactly as it was
for l in open(os.path.join(dm, 'hashes.txt')).read().splitlines():
    h, name = l.split()
    p = os.path.join(dm, name) if not name.startswith('dowser-') else os.path.join(play, name)
    check(os.path.exists(p) and hashlib.sha1(open(p, 'rb').read()).hexdigest() == h,
          f'unchanged: {name} (Dowser wrote nothing)')


def mem(name):
    t = read(name)
    return dict(zip(t.split()[::2], map(int, t.split()[1::2]))) if t else None


m1, m2, m3 = mem('dm-mem1.txt'), mem('dm-mem2.txt'), mem('dm-mem3.txt')
md = [mem(f'dm-memd{i}.txt') for i in (1, 2, 3, 4)]
if m1 and m2 and m3:
    print(f"INFO free memory: start {m1['total']}, after round 1 {m2['total']}, after a second round of images {m3['total']}")
    # round 2 is three commands: a leak in Dowser would repeat in each (3 x 512 at least);
    # Kickstart 3.1 sometimes keeps a single 512-byte block of its own, sometimes not
    check(m2['total'] - m3['total'] < 1024 and m1['chip'] - m3['chip'] < 512,
          'no memory lost on a second round of images (no per-command loss)',
          (m1['total'] - m2['total'], m2['total'] - m3['total']))
if m3 and None not in md:
    print('INFO free memory after each of four reads of DF1: ' + ', '.join(str(x['total']) for x in md) +
          ' (1.3 DOS takes a one-off few K when it first gets a drive back)')
    check(md[1]['total'] - md[3]['total'] < 256 and md[1]['chip'] - md[3]['chip'] < 256,
          'repeated drive reads lose no memory (reads 3 and 4 cost nothing)',
          [m3['total'] - x['total'] for x in md])
for i in (1, 2, 3, 4):
    check(sorted(l.rsplit('|', 1)[0] for l in lines(f'dm-r2-df1-{i}.txt')) == sorted(models['ffs'].files()),
          f'DF1: read {i} the same')
check(read('dm-done.txt') is not None, 'test script ran to the end')
print('ALL PASSED' if not fails else f'{fails} FAILED')
sys.exit(1 if fails else 0)
