# Dowser: development notes

How Dowser is put together, what we learned writing it for Kickstart 1.3, and how it is tested.
(Updated at the end of every milestone.)

## Design rules

- **Read-only.** Dowser 1.0 never writes to the disk it inspects: `disk.c` has no write
  function at all. Recovered files go to another drive. The tests prove it by hashing every
  image and drive before and after a run.
- **Trust nothing on the disk.** Every block number is range-checked, every chain has a step
  limit and a visited mark, the tree is walked with a queue (no recursion: 1.3 stacks are 4 KB).
- **One engine, two front ends**: the window and `dowser CMD ...` use the same modules.
- **V33 only**, built against the 1.3 includes.

## Modules (so far)

| File | What it does |
|---|---|
| `disk.c` | Block source: trackdisk with a one-track chip RAM cache (whole track, then sector by sector when a track has errors, so good sectors of a damaged track are kept), or an .adf file. DOS is kept off a drive with ACTION_INHIBIT while Dowser reads it. |
| `fs.c` | The model: one sequential pass summarising every block (type, checksum, keys), then the root, the bitmap and a queue-driven walk of the tree. Builds the ownership map (kind, owner, flags per block) and the object table (files, drawers, links with sizes, block counts and fragments). |
| `cmd.c` | `INFO`, `MAP`, `FILES`, `BLOCK`, `REPORT` (all three from one read), `MEM` |

## The on-disk format (as used)

- Blocks are 512 bytes, 1760 on a DD floppy; block = (cylinder x 2 + head) x 11 + sector.
- Boot block: `DOS\n` (n bit 0 FFS, bit 1 international, bit 2 directory cache), checksum.
- Root block 880; headers: type 2 at 0, own block at 4, checksum at 20 (all 128 longs sum to
  0), hash table at 24 (72 slots), bitmap pages at 0x13C, name (BCPL) at 0x1B0, hash chain at
  0x1F0, parent at 0x1F4, extension at 0x1F8, secondary type at 0x1FC (1 root, 2 drawer,
  -3 file, 3 soft link, 4/-4 hard links).
- File headers list data blocks from the end of the table (0x134 downwards), the count at 8;
  extension blocks (type 16) continue the list. OFS data blocks have a 24-byte header
  (type 8, file header, sequence number, size, next); FFS data blocks are raw.
- Bitmap: long 0 is the checksum, then one bit per block from block 2, set = free.
- Directory cache (DOS\4/5): type 33 blocks chained from the drawer's extension field.
- Name hash: h = length; h = (h x 13 + upper(c)) & 0x7FF for each character; slot = h % 72.
  International mode also upper-cases Latin-1 letters 0xE0-0xFE (except 0xF7).

## Kickstart 1.3 lessons from Dowser

- Read longs from block buffers directly: the 68000 is big-endian like the disk. That made the
  scan several times faster than assembling bytes, but every block buffer must then be
  long-aligned (`static ULONG buf[128]`), because the 68000 faults on longs at odd addresses.
- A full scan of an 880K image takes about 7 s on an emulated 7 MHz 68000: test scripts use
  `CMD REPORT` (one read, three outputs) instead of three separate commands.
- After Dowser releases a drive, 1.3's DOS takes back (and later frees) a few K of chip RAM
  for its own buffers: memory checks compare repeated reads, not the first.

## Tests

- `tools/mkdisks.py` makes the test images with xdftool: OFS, FFS, both international and
  both directory-cache variants (xdftool writes real dircache blocks), a fragmented disk
  (nearly fill it, free a gap, write a file bigger than the gap and than the space left), a
  full disk, a blank disk, and two non-AmigaDOS images.
- `tools/adfmodel.py` is an independent Python reader of the same format: the expected
  INFO / MAP / FILES. Its file lists are in turn checked against xdftool's own listing.
- `testmodel.sh` + `tools/checkmodel.py`: every image from Work: and through emulated drives
  (DF1 writable, DF2 write-protected, DF3 empty), block maps compared character by character,
  images hashed before and after, memory over repeated rounds. Passes on KS1.3 and KS3.1.
