#!/usr/bin/env python3
"""Check the results of testcheck.sh (Dowser milestone 2: the disk check and the boot block).

    checkcheck.py [SHARE] [PLAY]

Each damaged image must report the finding it was damaged for, and nothing beyond its
expected side effects; clean images must report nothing. Boot blocks must be classified as
expected, and every image must be unchanged afterwards.
"""
import hashlib
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from adfmodel import Model  # noqa: E402

share = sys.argv[1] if len(sys.argv) > 1 else '/mnt/j/amiga/share'
play = sys.argv[2] if len(sys.argv) > 2 else '/mnt/j/amiga/play'
dc = os.path.join(share, 'dc')
fails = 0


def read(name):
    p = os.path.join(share, name)
    return open(p, encoding='latin-1').read() if os.path.exists(p) else None


def section(name, sec):
    text = read(name)
    if text is None:
        return None
    out, on = [], False
    for l in text.split('\n'):
        if re.fullmatch(r'\[[A-Z]+\]', l):
            on = l == '[' + sec + ']'
        elif on and l.strip():
            out.append(l)
    return out


def check(ok, what, detail=''):
    global fails
    print(('PASS ' if ok else 'FAIL ') + what + (('  -- ' + str(detail)) if detail and not ok else ''))
    if not ok:
        fails += 1


def codes(lines):
    return {m.group(1) for l in lines for m in [re.search(r'\[([A-Z]+)\]$', l)] if m}


def bootinfo(lines):
    d = {}
    for l in lines:
        k, _, v = l.partition(': ')
        if k == 'text':
            d.setdefault('texts', []).append(v)
        else:
            d[k] = v
    return d


expect = json.load(open(os.path.join(dc, 'expect-check.json')))
for img in sorted(expect):
    e = expect[img]
    rep = f'dc-{img}-report.txt'
    chk = section(rep, 'CHECK')
    if chk is None:
        check(False, f'{img}: report written')
        continue
    got = codes(chk)
    need, allow = set(e['require']), set(e['require']) | set(e['allow'])
    result = [l for l in chk if l.startswith('result:')]
    if not need:
        check(not got and result == ['result: no problems found'], f'{img}: no problems found', chk[:3])
    else:
        check(need <= got and got <= allow, f'{img}: reports {", ".join(sorted(need))}'
              + (f' (and may report {", ".join(sorted(allow - need))})' if allow - need else ''),
              f'got {sorted(got)}')
        # every finding line must be plain English with a block number
        lines_ok = all(re.match(r'(error|warning|note): .+ \[[A-Z]+\]$', l) for l in chk
                       if not l.startswith(('result:', 'fragmented:', '(and')))
        check(lines_ok, f'{img}: findings are readable lines', chk[:2])
    if e['boot']:
        bi = bootinfo(section(rep, 'BOOT') or [])
        b = e['boot']
        ok = bi.get('code') == b.get('code', bi.get('code')) if b.get('code') != 'custom' else \
            bi.get('code') == 'custom boot code'
        ok = ok and ('checksum' not in b or bi.get('checksum') == b['checksum'])
        # (code bytes can be printable too: 'rts' is "Nu", so look for the text inside)
        ok = ok and ('text' not in b or any(b['text'] in t for t in bi.get('texts', [])))
        check(ok, f'{img}: boot block: {b.get("code")}' + (f', checksum {b["checksum"]}' if 'checksum' in b else ''), bi)

# fragmentation figures on the fragmented disk, against the independent reader
m = Model(os.path.join(dc, 'frag.adf'))
nfrag = sum(1 for o in m.objs[1:] if o['kind'] == 'f' and o['frags'] > 1)
nfiles = sum(1 for o in m.objs[1:] if o['kind'] == 'f')
fr = [l for l in section('dc-frag-report.txt', 'CHECK') or [] if l.startswith('fragmented:')]
check(fr and fr[0].startswith(f'fragmented: {nfrag} of {nfiles} files'), f'frag: "{nfrag} of {nfiles} files fragmented"', fr)

# through the drives
check('[TWICE]' in (read('dc-df1-check.txt') or ''), 'DF1: the check finds the shared block', (read('dc-df1-check.txt') or '')[:200])
check('Kickstart 1.x Install' in (read('dc-df2-boot.txt') or ''), 'DF2: the 1.x boot block is recognised (write-protected drive)',
      read('dc-df2-boot.txt'))
check('bootable: no' in (read('dc-df1-boot.txt') or ''), 'DF1: a disk without boot code is not bootable', read('dc-df1-boot.txt'))
for n in ('c-twice', 'c-extbad', 'c-dircache'):
    r2 = codes(read(f'dc-r2-{n}.txt').splitlines()) if read(f'dc-r2-{n}.txt') else None
    check(r2 == codes(section(f'dc-{n}-report.txt', 'CHECK') or []), f'second round: {n} the same')

for l in open(os.path.join(dc, 'hashes.txt')).read().splitlines():
    h, name = l.split()
    p = os.path.join(dc, name) if not name.startswith('dowser-') else os.path.join(play, name)
    check(os.path.exists(p) and hashlib.sha1(open(p, 'rb').read()).hexdigest() == h, f'unchanged: {name}')


def mem(name):
    t = read(name)
    return dict(zip(t.split()[::2], map(int, t.split()[1::2]))) if t else None


m1, m2, m3 = mem('dc-mem1.txt'), mem('dc-mem2.txt'), mem('dc-mem3.txt')
if m1 and m2 and m3:
    print(f"INFO free memory: start {m1['total']}, after round 1 {m2['total']}, after round 2 {m3['total']}")
    check(m2['total'] - m3['total'] < 1024 and m2['chip'] - m3['chip'] < 256,
          'no memory lost on a second round (no per-command loss)', (m1['total'] - m2['total'], m2['total'] - m3['total']))
check(read('dc-done.txt') is not None, 'test script ran to the end')
print('ALL PASSED' if not fails else f'{fails} FAILED')
sys.exit(1 if fails else 0)
