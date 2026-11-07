/* Dowser - block decoding. Written by Paul Seibert, (c) 2026 Desert Palms Software. */
#include <exec/memory.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include "util.h"
#include "dos13.h"
#include "disk.h"
#include "fs.h"
#include "boot.h"
#include "blockview.h"

static char *op;                 /* where the text goes */
static LONG left;
static char ln[160];

static void put(const char *s)
{
    while (*s && left > 1) {
        *op++ = *s++;
        left--;
    }
    *op = 0;
}

static void line(void)
{
    put(ln);
    put("\n");
}

static const char *objname(struct FS *fs, ULONG b, char *buf, int size)
{
    buf[0] = 0;
    if (b < NBLOCKS && fs->owner[b]) {
        fs_path(fs, fs->owner[b] - 1, buf, size);
        if (!buf[0])
            scopy(buf, "(the top drawer)", size);
    }
    return buf;
}

static void datestr(const UBYTE *b, int off, char *out)
{
    struct DateStamp ds;
    ds.ds_Days = fs_long(b, off);
    ds.ds_Minute = fs_long(b, off + 4);
    ds.ds_Tick = fs_long(b, off + 8);
    dos_datestr(out, &ds);
}

static void bcpl(const UBYTE *b, int off, int max, char *out)
{
    int n = b[off], i;
    if (n > max)
        n = max;
    for (i = 0; i < n; i++)
        out[i] = b[off + 1 + i];
    out[n] = 0;
}

static void pointer(struct FS *fs, const char *what, ULONG v)
{
    static char nm[PATHMAX];
    if (!v)
        fmt(ln, "%s: none", what);
    else if (v >= NBLOCKS)
        fmt(ln, "%s: %ld (outside the disk!)", what, v);
    else {
        objname(fs, v, nm, PATHMAX);
        fmt(ln, "%s: block %ld (%s%s%s%s)", what, v, fs_kindname(fs->kind[v]), nm[0] ? " of \"" : "", nm,
            nm[0] ? "\"" : "");
    }
    line();
}

static void header(struct FS *fs, const UBYTE *b, LONG blk)
{
    static char name[40], comment[84], date[20], prot[10], nm[PATHMAX];
    LONG st = (LONG)fs_long(b, 0x1FC), i, n = 0;
    bcpl(b, 0x1B0, 30, name);
    fmt(ln, "secondary type: %ld (%s)", st, st == SEC_ROOT ? "root" : st == SEC_DIR ? "drawer" : st == SEC_FILE ? "file"
        : st == SEC_SOFT ? "soft link" : st == SEC_LDIR || st == SEC_LFILE ? "hard link" : "unknown");
    line();
    fmt(ln, "name: \"%s\"", name);
    line();
    if (st == SEC_ROOT) {
        datestr(b, 0x1A4, date);
        fmt(ln, "last changed: %s", date);
        line();
        datestr(b, 0x1E4, date);
        fmt(ln, "created: %s", date);
        line();
        fmt(ln, "bitmap: %s, first bitmap block %ld", (LONG)fs_long(b, 0x138) == -1 ? "valid" : "NOT valid",
            fs_long(b, 0x13C));
        line();
    } else {
        datestr(b, 0x1A4, date);
        dos_protstr(prot, (LONG)fs_long(b, 0x140));
        fmt(ln, "date: %s, protection: %s", date, prot);
        line();
        bcpl(b, 0x148, 79, comment);
        if (comment[0]) {
            fmt(ln, "comment: \"%s\"", comment);
            line();
        }
    }
    if (st == SEC_FILE) {
        LONG cnt = (LONG)fs_long(b, 8), first = fs_long(b, 0x134);
        fmt(ln, "size: %ld bytes; this block lists %ld data blocks", (LONG)fs_long(b, 0x144), cnt);
        line();
        pointer(fs, "first data block", first);
        pointer(fs, "extension (more data blocks)", fs_long(b, 0x1F8));
    } else if (st == SEC_ROOT || st == SEC_DIR) {
        for (i = 0; i < 72; i++)
            if (fs_long(b, 24 + i * 4))
                n++;
        fmt(ln, "hash table: %ld of 72 slots used", n);
        line();
        for (i = 0; i < 72; i++) {
            ULONG e = fs_long(b, 24 + i * 4);
            if (!e)
                continue;
            objname(fs, e, nm, PATHMAX);
            fmt(ln, "  slot %2ld -> block %ld %s%s%s", i, e, nm[0] ? "\"" : "", nm, nm[0] ? "\"" : "");
            line();
        }
        if (fs->dirc)
            pointer(fs, "directory cache", fs_long(b, 0x1F8));
    }
    if (st != SEC_ROOT) {
        pointer(fs, "next in hash chain", fs_long(b, 0x1F0));
        pointer(fs, "parent", fs_long(b, 0x1F4));
    }
    (void)blk;
}

LONG bv_text(struct FS *fs, struct Disk *d, LONG b, char *out, LONG size)
{
    static ULONG bl[256];
    static char path[PATHMAX];
    static const char hx[] = "0123456789ABCDEF";
    UBYTE *buf = (UBYTE *)bl;
    LONG err, i, k, type;
    op = out;
    left = size;
    out[0] = 0;
    if (b < 0 || b >= NBLOCKS) {
        put("No such block.\n");
        return op - out;
    }
    path[0] = 0;
    if (fs->owner[b] > 1)                            /* (object 0 is the disk itself) */
        fs_path(fs, fs->owner[b] - 1, path, PATHMAX);
    fmt(ln, "block %ld: %s%s%s%s", b, fs_kindname(fs->kind[b]), path[0] ? " of \"" : "", path, path[0] ? "\"" : "");
    line();
    fmt(ln, "where: cylinder %ld, head %ld, sector %ld", b / 22, (b / 11) & 1, b % 11);
    line();
    err = disk_read(d, b, buf);
    if (err) {
        fmt(ln, "read: %s", disk_errstr(err));
        line();
        return op - out;
    }
    fmt(ln, "flags: %s%s%s%s", fs->flags[b] & BF_USED ? "in use" : "free", fs->flags[b] & BF_CSUM ? ", checksum wrong" : "",
        fs->flags[b] & BF_TWICE ? ", used by two files" : "", fs->flags[b] & BF_UNMARKED ? ", in use but marked free" : "");
    line();
    type = (LONG)fs_long(buf, 0);
    if (b < 2) {                                     /* the boot block (both halves) */
        struct BootInfo bi;
        static ULONG bb[256];
        disk_read(d, 0, (UBYTE *)bb);
        disk_read(d, 1, (UBYTE *)bb + BSIZE);
        boot_inspect((UBYTE *)bb, &bi);
        fmt(ln, "boot block: %s, checksum %s, crc32 %08lx", boot_kindname(bi.kind), bi.sumok ? "ok" : "wrong", bi.crc);
        line();
    } else if (fs->kind[b] == BK_BITMAP) {
        LONG freebits = 0;
        for (i = 1; i < 128; i++) {
            ULONG v = fs_long(buf, i * 4);
            for (k = 0; k < 32; k++)
                if (v & (1UL << k))
                    freebits++;
        }
        fmt(ln, "bitmap block: checksum %s, %ld blocks marked free", fs_csumok(buf, 0) ? "ok" : "wrong", freebits);
        line();
    } else if (fs->kind[b] == BK_DATA && fs->ffs) {
        put("FFS data block: 512 bytes of file contents, no header.\n");
    } else if (type == T_HEADER || type == T_DATA || type == T_LIST || type == T_DIRC) {
        fmt(ln, "type: %ld (%s), checksum %s", type, type == T_HEADER ? "header" : type == T_DATA ? "OFS data"
            : type == T_LIST ? "file extension" : "directory cache", fs_csumok(buf, 20) ? "ok" : "WRONG");
        line();
        if (type != T_DATA && !(type == T_HEADER && (LONG)fs_long(buf, 0x1FC) == SEC_ROOT)) {
            fmt(ln, "own block number: %ld%s", fs_long(buf, 4), (LONG)fs_long(buf, 4) == b ? "" : " (wrong!)");
            line();
        }
        if (type == T_HEADER)
            header(fs, buf, b);
        else if (type == T_DATA) {
            fmt(ln, "sequence number: %ld, bytes used: %ld", fs_long(buf, 8), fs_long(buf, 12));
            line();
            pointer(fs, "belongs to (file header)", fs_long(buf, 4));
            pointer(fs, "next data block", fs_long(buf, 16));
        } else if (type == T_LIST) {
            fmt(ln, "lists %ld more data blocks", fs_long(buf, 8));
            line();
            pointer(fs, "belongs to (file header)", fs_long(buf, 0x1F4));
            pointer(fs, "next extension", fs_long(buf, 0x1F8));
        } else {
            fmt(ln, "%ld entries cached", fs_long(buf, 12));
            line();
            pointer(fs, "drawer", fs_long(buf, 8));
            pointer(fs, "next cache block", fs_long(buf, 16));
        }
    } else
        put("no AmigaDOS block structure (file contents, free space or another format)\n");
    put("\n");
    for (i = 0; i < BSIZE; i += 16) {                /* hex and ASCII */
        char *p = ln;
        fmt(p, "%03lx: ", i);
        p += 5;
        for (k = 0; k < 16; k++) {
            *p++ = hx[buf[i + k] >> 4];
            *p++ = hx[buf[i + k] & 15];
            if (k % 4 == 3)
                *p++ = ' ';
        }
        *p++ = ' ';
        for (k = 0; k < 16; k++) {
            UBYTE c = buf[i + k];
            *p++ = (c >= 32 && c < 127) || c >= 0xA0 ? c : '.';
        }
        *p = 0;
        line();
    }
    return op - out;
}
