#!/usr/bin/env python3
"""Make Dowser's test disk images.

    mkdisks.py OUTDIR

Writes the files for each image into OUTDIR/src and a script OUTDIR/make.sh of xdftool
commands; run that script in the toolchain image (it has xdftool) to make OUTDIR/*.adf.
Also writes OUTDIR/raw-*.adf directly (images that aren't AmigaDOS disks).
"""
import os
import random
import sys

out = sys.argv[1]
src = os.path.join(out, 'src')
os.makedirs(src, exist_ok=True)
r = random.Random(1988)
files = {}


def put(name, data):
    files[name] = data
    open(os.path.join(src, name), 'wb').write(data)


put('readme', b'Dowser test disk.\n' * 160)                        # 2880 bytes
put('small', b'x' * 100)
put('empty', b'')
put('a', b'level one\n' * 300)
put('b', bytes(range(256)) * 160)                                   # 40960
put('big', bytes(r.getrandbits(8) for _ in range(200000)))          # needs extension blocks
put('note', b'a note\n')
for i in range(60):
    put(f'm{i}', f'file {i}\n'.encode() * (i + 1))
put('fill1', bytes(r.getrandbits(8) for _ in range(30000)))
put('fill2', bytes(r.getrandbits(8) for _ in range(30000)))
put('fill3', bytes(r.getrandbits(8) for _ in range(30000)))
put('frag', bytes(r.getrandbits(8) for _ in range(60000)))
put('huge2', bytes(r.getrandbits(8) for _ in range(760000)))
put('huge', bytes(r.getrandbits(8) for _ in range(780000)))


def tree():
    """the standard tree: files at the top, nested drawers, a crowded drawer (hash chains)"""
    c = ['write src/readme readme', 'write src/small small', 'write src/empty empty.dat',
         'makedir sub', 'write src/a sub/a.txt', 'makedir sub/deeper', 'write src/b sub/deeper/b.bin',
         'write src/big big.bin', 'makedir many']
    c += [f'write src/m{i} many/file{i:02}' for i in range(60)]
    return c


images = {
    'ofs': ('ofs', tree()),
    'ffs': ('ffs', tree()),
    'ofsintl': ('ofs+intl', tree() + ['write src/note Café.txt', 'write src/note ÄÖÜ']),
    'ffsintl': ('ffs+intl', tree() + ['write src/note Café.txt']),
    'ofsdc': ('ofs+dircache', tree()),
    'ffsdc': ('ffs+dircache', tree()),
    # fragmented: nearly fill the disk, free a 60-block gap, then write a file bigger than the
    # gap and bigger than what is left at the end: it has to be split
    'frag': ('ffs', ['write src/fill1 fill1', 'write src/fill2 fill2', 'write src/fill3 fill3',
                     'write src/huge2 huge2.bin', 'delete fill2', 'write src/frag fragmented.bin']),
    'full': ('ffs', ['write src/huge huge.bin', 'write src/readme readme']),
    'blank': ('ofs', []),
}

with open(os.path.join(out, 'make.sh'), 'w', newline='\n') as f:
    f.write('set -e\ncd "$(dirname "$0")"\n')
    for name, (fstype, cmds) in images.items():
        f.write(f'rm -f {name}.adf\n')
        line = f'xdftool {name}.adf format "Test {name}" {fstype}'
        for c in cmds:
            parts = c.split(' ', 2)
            line += ' + ' + ' '.join([parts[0]] + [f'"{p}"' for p in parts[1:]])
        f.write(line + ' >/dev/null\n')
        f.write(f'xdftool {name}.adf list > {name}.list\n')

# not AmigaDOS: random bytes, and a disk whose bootblock says KICK (a Kickstart disk)
open(os.path.join(out, 'raw-random.adf'), 'wb').write(bytes(r.getrandbits(8) for _ in range(901120)))
open(os.path.join(out, 'raw-kick.adf'), 'wb').write(b'KICK' + bytes(901116))
print(' '.join(images))
