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
| `check.c` | The check: findings with a severity, a block and an owner, as plain-English lines ending in a short code (`[HDRCSUM]`). Root and bitmap, what the walk ran into, every block's flags, OFS data blocks (type, owner, sequence, size, checksum), file sizes against block counts, directory caches against the drawers' contents, fragmentation. |
| `boot.c` | The boot block: DOS type, checksum (add with carry, complement), standard install code recognised by the CRC32 of bytes 12-1023, readable text in custom code. Viruses are not named: without a maintained signature list that would be guesswork. |
| `recover.c` | Deleted and orphaned files: unowned blocks holding a valid file or drawer header (`key` = its own block, checksum right). Deleted = marked free; orphan = still marked in use, or no bitmap at all (lost root). Whole if no living file owns its data blocks (OFS: and they still say they are its). Rescue copies files, drawers, the whole disk, or found files (into the drawers they were in), with dates, protection and comments; unreadable or foreign blocks become zeros and are counted. One shared path buffer for the drawer walk (4 KB stacks). |
| `scan.c` | The surface scan: every track read afresh (`disk_forget` first), each one ok, without AmigaDOS sectors (no sector headers: unformatted or a custom format), or damaged. |
| `blockview.c` | One block as text: what it is and who owns it, its fields decoded for its type (header, OFS data, extension, directory cache, bitmap, boot), pointers resolved to names, then hex and ASCII. Shared by the block viewer and `CMD BLOCK`. |
| `ui.c` | The window: drive and Image buttons, the 80 x 22 block map with its legend, the info line, Check / Files / Boot tabs over a scrolling list, the block viewer (`view.c` from Oasis with a key hook: n/p for the next/previous block). Start-up arguments (`dowser source [CHECK|FILES|BOOT] [BLOCK=n] [FILE=path]`) also drive the screenshot tests. |
| `cmd.c` | `INFO`, `MAP`, `FILES`, `BLOCK`, `CHECK`, `BOOT`, `REPORT` (all of them from one read), `MEM` |

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
- Standard boot code, CRC32 of bytes 12-1023: Kickstart 1.x Install `2C9F6D94` (opens
  dos.library), Kickstart 2.0+ Install `9942406E` (also opens expansion.library; it is also what
  `xdftool boot install` writes). Non-boot Workbench/Extras disks often carry leftover data in
  the code area with a wrong checksum: they simply don't boot.
- Directory cache records (type 33 blocks, count at 12, records from 24): header, size,
  protection (longs), uid, gid, days, minutes, ticks (words), type, name length, name, comment
  length, comment, padded to an even length.
- Name hash: h = length; h = (h x 13 + upper(c)) & 0x7FF for each character; slot = h % 72.
  International mode also upper-cases Latin-1 letters 0xE0-0xFE (except 0xF7).

## Kickstart 1.3 lessons from Dowser

- Read longs from block buffers directly: the 68000 is big-endian like the disk. That made the
  scan several times faster than assembling bytes, but every block buffer must then be
  long-aligned (`static ULONG buf[128]`), because the 68000 faults on longs at odd addresses.
- A full scan of an 880K image takes about 7 s on an emulated 7 MHz 68000: test scripts use
  `CMD REPORT` (one read, three outputs) instead of three separate commands.
- Drawing 1760 map cells one by one (pen changes, a fill and a frame each) took about a minute
  on a 7 MHz 68000. Now free cells are left as the background, runs of same-style cells down a
  column are one fill, and the gaps between cells are drawn back with one line per column and
  per row: a few seconds. A big selection change redraws the map that way instead of cell by cell.
- Four Workbench colours aren't enough for fourteen block types: pen plus a 2-line fill pattern
  (`SetAfPt`, patterns in chip RAM, `JAM2`: pen A where the pattern is set, pen B elsewhere).
- Releasing an inhibited drive makes DOS report a disk change to every window: Dowser ignores
  disk-change messages for a few seconds after it has used a drive itself.
- `RawDoFmt` has no `*` width: pad columns by hand.
- A failing track read is slow (trackdisk retries internally; WinUAE took over a minute per raw
  track). Dowser retries a track twice, reads sector by sector only for checksum errors (with no
  sector headers every sector fails, slowly), and remembers bad tracks, so later reads of their
  sectors answer at once and only the good sectors of a damaged track are read again, singly.
- Ask DOS about a destination **before** inhibiting a drive: a Lock on an inhibited volume
  doesn't come back. And use `dos_quiet` (pr_WindowPtr = -1) for it: on Kickstart 1.3 an FFS
  floppy puts up a "not a DOS disk" requester that waits for a click and stalls a test.
- When the boot block doesn't say DOS but the root block is intact, the disk is treated as
  AmigaDOS (OFS if there are OFS data blocks, else FFS), with a warning, so its files can be
  rescued.
- After Dowser releases a drive, 1.3's DOS takes back (and later frees) a few K of chip RAM
  for its own buffers: memory checks compare repeated reads, not the first.

## Tests

- `tools/mkdisks.py` makes the test images with xdftool: OFS, FFS, both international and
  both directory-cache variants (xdftool writes real dircache blocks), a fragmented disk
  (nearly fill it, free a gap, write a file bigger than the gap and than the space left), a
  full disk, a blank disk, and two non-AmigaDOS images.
- `tools/adfmodel.py` is an independent Python reader of the same format: the expected
  INFO / MAP / FILES. Its file lists are in turn checked against xdftool's own listing.
- `tools/mkbroken.py` damages copies of the images in sixteen known ways (root, header, data and
  bitmap checksums; a block marked free while in use, lost space, a shared block, an entry
  outside the disk, a wrong parent, a wrong hash slot, an invalid bitmap, a wrong sequence
  number, a wrong size, an entry to a non-header, a broken extension chain, an out-of-date
  directory cache) plus boot blocks (custom with and without a valid checksum, 1.x and 2.0+
  Install), fixing the checksums it doesn't mean to break.
- `testcheck.sh` + `tools/checkcheck.py`: each damaged image must report its finding and nothing
  beyond its listed side effects; clean images must report nothing; boot blocks classified;
  fragmentation figures against the Python reader; CHECK and BOOT through emulated drives.
- `tools/mkdeleted.py` makes the recovery images: files deleted by xdftool (OFS and FFS), a
  deleted file with one block reused, a lost root, a lost boot block, a destroyed OFS data
  block, and an extended ADF (`UAE-1ADF`) with two raw tracks without sector headers, which
  trackdisk can't read, like a damaged floppy.
- `testrescue.sh` + `tools/checkrescue.py`: DELETED lists, undelete one and all, rescue from
  good and damaged disks and through drives, compared byte for byte with what
  `tools/adfmodel.py` decodes from the original image (which can treat given blocks as
  unreadable), with zeros exactly where blocks were reused, destroyed or unreadable; SCAN of
  the damaged, a good and an empty drive; rescuing onto the disk being read refused.
- `testmodel.sh` + `tools/checkmodel.py`: every image from Work: and through emulated drives
  (DF1 writable, DF2 write-protected, DF3 empty), block maps compared character by character,
  images hashed before and after, memory over repeated rounds. Passes on KS1.3 and KS3.1.
