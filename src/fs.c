/* Dowser - the disk model. Written by Paul Seibert, (c) 2026 Desert Palms Software.
 *
 * Nothing on the disk is trusted: every block number is range-checked, every chain has a
 * step limit and a visited mark, and the tree is walked with a queue instead of recursion
 * (1.3 stacks are 4 KB). */
#include <exec/memory.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include "util.h"
#include <devices/trackdisk.h>
#include "disk.h"
#include "fs.h"

static ULONG blkl[BSIZE / 4];     /* the block being looked at (long-aligned: the 68000 */
static UBYTE *const blk = (UBYTE *)blkl;   /* faults on longs at odd addresses) */

/* the 68000 is big-endian like the disk: a long is read as it is (o is always a multiple of
   4 and every block buffer is long-aligned) */
ULONG fs_long(const UBYTE *b, int o)
{
    return *(const ULONG *)(b + o);
}

BOOL fs_csumok(const UBYTE *b, int at)
{
    const ULONG *p = (const ULONG *)b;
    ULONG s = 0;
    int i;
    (void)at;
    for (i = 0; i < BSIZE / 4; i++)
        s += p[i];
    return s == 0;
}

static int upper(int c, BOOL intl)
{
    c &= 0xFF;
    if (c >= 'a' && c <= 'z')
        return c - 32;
    if (intl && c >= 0xE0 && c <= 0xFE && c != 0xF7)
        return c - 32;
    return c;
}

int fs_hash(const char *name, BOOL intl)
{
    ULONG h = slen(name);
    for (; *name; name++)
        h = (h * 13 + upper((UBYTE)*name, intl)) & 0x7FF;
    return (int)(h % 72);
}

static const char *const kindnames[BK_COUNT] = {
    "not looked at", "free", "boot block", "root block", "bitmap", "bitmap extension",
    "drawer header", "file header", "file extension", "data", "directory cache", "link",
    "lost (allocated but unused)", "unreadable"
};

const char *fs_kindname(int bk)
{
    return bk >= 0 && bk < BK_COUNT ? kindnames[bk] : "?";
}

char fs_kindchar(struct FS *fs, LONG b)
{
    static const char ch[BK_COUNT] = { ' ', '.', 'B', 'R', 'M', 'm', 'D', 'H', 'E', 'd', 'C', 'L', '?', 'X' };
    if (fs->flags[b] & (BF_TWICE | BF_UNMARKED))
        return '!';
    return ch[fs->kind[b]];
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

static void datestamp(const UBYTE *b, int off, struct DateStamp *ds)
{
    ds->ds_Days = fs_long(b, off);
    ds->ds_Minute = fs_long(b, off + 4);
    ds->ds_Tick = fs_long(b, off + 8);
}

static BOOL inrange(ULONG b)
{
    return b >= 2 && b < NBLOCKS;
}

/* ---- objects ------------------------------------------------------------------------------ */

static int newobj(struct FS *fs)
{
    if (fs->nobj == fs->objcap) {
        int nc = fs->objcap + 64;
        struct Obj *n = AllocMem(nc * sizeof(struct Obj), MEMF_PUBLIC | MEMF_CLEAR);
        if (!n)
            return -1;
        if (fs->obj) {
            CopyMem(fs->obj, n, fs->nobj * sizeof(struct Obj));
            FreeMem(fs->obj, fs->objcap * sizeof(struct Obj));
        }
        fs->obj = n;
        fs->objcap = nc;
    }
    return fs->nobj++;
}

/* note something the walk ran into (the check reports it) */
static void prob(struct FS *fs, int code, LONG block, int obj)
{
    if (fs->nprob < MAXPROB) {
        struct Prob *p = &fs->prob[fs->nprob++];
        p->code = (UBYTE)code;
        p->block = (UWORD)block;
        p->obj = obj < 0 ? NOOBJ : (UWORD)obj;
    }
}

void fs_free(struct FS *fs)
{
    if (fs->obj)
        FreeMem(fs->obj, fs->objcap * sizeof(struct Obj));
    fs->obj = NULL;
    fs->nobj = fs->objcap = 0;
}

void fs_path(struct FS *fs, int i, char *out, int size)
{
    int chain[64], n = 0, k;
    out[0] = 0;
    while (i > 0 && i < fs->nobj && n < 64) {
        chain[n++] = i;
        i = fs->obj[i].parent;
    }
    for (k = n - 1; k >= 0; k--) {
        if (out[0])
            scat(out, "/", size);
        scat(out, fs->obj[chain[k]].name, size);
    }
}

/* claim block b for object o as kind k */
static void claim(struct FS *fs, LONG b, int k, int o)
{
    if (!inrange(b))
        return;
    if (fs->owner[b] && fs->owner[b] != o + 1)
        fs->flags[b] |= BF_TWICE;
    else {
        fs->owner[b] = o + 1;
        fs->kind[b] = k;
    }
}

/* ---- the data blocks of a file --------------------------------------------------------------- */

/* walk a file's header and extension blocks; calls back for each data block pointer */
static int datalist(struct FS *fs, struct Disk *d, int o, UWORD *out, int max, BOOL mark)
{
    LONG b = fs->obj[o].header, steps = 0;
    int n = 0, i;
    BOOL first = TRUE;
    while (b && steps++ < NBLOCKS) {
        LONG cnt;
        if (!inrange(b)) {
            if (mark) {
                prob(fs, F_EXTBAD, fs->obj[o].header, o);
                fs->obj[o].status |= OS_DAMAGED;
            }
            break;
        }
        if (!first && mark)
            claim(fs, b, BK_EXT, o);
        if (disk_read(d, b, blk)) {
            fs->obj[o].status |= OS_UNREADABLE;
            break;
        }
        if (!first && (fs_long(blk, 0) != T_LIST || (LONG)fs_long(blk, 0x1F4) != fs->obj[o].header)) {
            if (mark) {                              /* not one of this file's extension blocks */
                prob(fs, F_EXTBAD, b, o);
                fs->obj[o].status |= OS_DAMAGED;
            }
            break;
        }
        cnt = (LONG)fs_long(blk, 8);
        if (cnt < 0 || cnt > 72)
            cnt = 72;
        for (i = 0; i < cnt; i++) {
            ULONG db = fs_long(blk, 0x134 - i * 4);  /* the table is filled from the end */
            if (!db)
                continue;
            if (out && n < max)
                out[n] = (UWORD)db;
            n++;
            if (mark && inrange(db))
                claim(fs, db, BK_DATA, o);
        }
        b = (LONG)fs_long(blk, 0x1F8);               /* the next extension block */
        first = FALSE;
    }
    return n;
}

int fs_datablocks(struct FS *fs, struct Disk *d, int i, UWORD *out, int max)
{
    return datalist(fs, d, i, out, max, FALSE);
}

/* fragments: how many runs of consecutive blocks the file is in */
static void fragments(struct FS *fs, struct Disk *d, int o)
{
    static UWORD list[NBLOCKS];
    int n = datalist(fs, d, o, list, NBLOCKS, TRUE), i, runs = n ? 1 : 0;
    if (n > NBLOCKS)
        n = NBLOCKS;
    for (i = 1; i < n; i++)
        if (list[i] != list[i - 1] + 1)
            runs++;
    fs->obj[o].nblocks = (UWORD)n;
    fs->obj[o].frags = (UWORD)runs;
}

/* ---- the directory-cache blocks of a drawer (DOS\4, DOS\5) -------------------------------- */

static void dircache(struct FS *fs, struct Disk *d, int o, ULONG b)
{
    LONG steps = 0;
    while (b && steps++ < 200) {
        if (!inrange(b))
            break;
        claim(fs, b, BK_DIRC, o);
        if (disk_read(d, b, blk) || fs_long(blk, 0) != T_DIRC)
            break;
        b = fs_long(blk, 16);                        /* next dircache block */
    }
}

/* ---- the scan ----------------------------------------------------------------------------------- */

static void summarise(struct FS *fs, LONG b, const UBYTE *p, LONG err)
{
    struct BSum *s = &fs->sum[b];
    ULONG type = fs_long(p, 0);
    s->type = (type == T_HEADER || type == T_DATA || type == T_LIST || type == T_DIRC) ? (UBYTE)type : 0;
    s->sectype = (BYTE)fs_long(p, 0x1FC);
    s->csumok = fs_csumok(p, 20);
    s->key = (UWORD)fs_long(p, 4);
    s->seq = (UWORD)fs_long(p, 8);
    s->size = (UWORD)fs_long(p, 12);
    s->parent = (UWORD)fs_long(p, 0x1F4);
    if (err) {
        fs->flags[b] |= BF_READERR;
        fs->readerrs++;
        s->type = 0;
    }
}

LONG fs_scan(struct FS *fs, struct Disk *d, FsProgress p, void *ctx, const char **why)
{
    static UWORD queue[NBLOCKS];
    LONG b, err, i;
    int qh = 0, qt = 0;
    char *z = (char *)fs;
    fs_free(fs);
    for (i = 0; i < (LONG)sizeof(*fs); i++)       /* (too big to clear with a struct copy) */
        z[i] = 0;
    *why = NULL;

    /* 1. every block, in order: a summary of each (and the read errors) */
    for (b = 0; b < NBLOCKS; b++) {
        if (b % SECTORS == 0 && p && !p(ctx, b, NBLOCKS)) {
            fs->aborted = TRUE;
            *why = "stopped";
            return FS_STOPPED;
        }
        err = disk_read(d, b, blk);
        if (err == TDERR_DiskChanged) {
            *why = "the disk was taken out";
            return err;
        }
        summarise(fs, b, blk, err);
        if (b % SECTORS == SECTORS - 1)                   /* the track is done: how did it go */
            fs->track[b / SECTORS] = !d->cachederr ? TRK_OK : (d->cachederr == TDERR_NoSecHdr ||
                d->cachederr == TDERR_TooFewSecs || d->cachederr == TDERR_BadSecPreamble ||
                d->cachederr == TDERR_BadSecID) ? TRK_NODOS : TRK_DAMAGED;
        if (b == 0) {
            fs->dostype = fs_long(blk, 0);
            fs->dos = (fs->dostype & 0xFFFFFF00UL) == 0x444F5300UL && (fs->dostype & 0xFF) <= 5;
            fs->ffs = fs->dos && (fs->dostype & 1);
            fs->intl = fs->dos && (fs->dostype & 6);
            fs->dirc = fs->dos && (fs->dostype & 4);
        }
    }
    fs->kind[0] = fs->kind[1] = BK_BOOT;
    fs->root = NBLOCKS / 2;
    if (!fs->dos && fs->sum[fs->root].type == T_HEADER && fs->sum[fs->root].sectype == SEC_ROOT &&
        fs->sum[fs->root].csumok) {
        /* the boot block is damaged, but there is a root block: AmigaDOS after all. OFS if
           there are OFS data blocks, FFS if not */
        fs->dos = fs->bootbad = TRUE;
        fs->ffs = TRUE;
        for (b = 2; b < NBLOCKS; b++)
            if (fs->sum[b].type == T_DATA && fs->sum[b].csumok) {
                fs->ffs = FALSE;
                break;
            }
        fs->dostype = fs->ffs ? 0x444F5301UL : 0x444F5300UL;
    }
    if (!fs->dos) {
        for (b = 2; b < NBLOCKS; b++)
            fs->kind[b] = (fs->flags[b] & BF_READERR) ? BK_UNREAD : BK_UNSEEN;
        return 0;
    }

    /* 2. the root block and the bitmap */
    if ((err = disk_read(d, fs->root, blk)) == 0 && fs_long(blk, 0) == T_HEADER && (LONG)fs_long(blk, 0x1FC) == SEC_ROOT) {
        bcpl(blk, 0x1B0, 30, fs->volname);
        datestamp(blk, 0x1A4, &fs->altered);
        datestamp(blk, 0x1E4, &fs->created);
        fs->bmflag = (LONG)fs_long(blk, 0x138);
        for (i = 0; i < 25; i++)
            fs->bmblocks[i] = (LONG)fs_long(blk, 0x13C + i * 4);
        fs->rootok = TRUE;
    } else
        prob(fs, err ? F_UNREAD : F_ROOT, fs->root, 0);
    i = newobj(fs);                                  /* object 0: the root */
    if (i < 0) {
        *why = "not enough memory";
        return ERROR_NO_FREE_STORE;
    }
    scopy(fs->obj[0].name, fs->volname, 31);
    fs->obj[0].kind = OK_DIR;
    fs->obj[0].header = (UWORD)fs->root;
    fs->obj[0].date = fs->altered;
    fs->kind[fs->root] = BK_ROOT;
    fs->owner[fs->root] = 1;
    if (fs->rootok && !fs->bmblocks[0])
        prob(fs, F_BMBAD, fs->root, 0);
    for (i = 0; i < 25 && fs->bmblocks[i]; i++) {
        LONG bm = fs->bmblocks[i], w, bit;
        if (!inrange(bm)) {
            prob(fs, F_BMBAD, fs->root, 0);
            continue;
        }
        fs->kind[bm] = BK_BITMAP;
        fs->owner[bm] = 1;
        if (disk_read(d, bm, blk))
            continue;
        if (!fs_csumok(blk, 0))
            fs->flags[bm] |= BF_CSUM;
        /* bit set = free; bit 0 of the first long after the checksum is block 2 */
        for (w = 0; w < 127; w++) {
            ULONG v = fs_long(blk, 4 + w * 4);
            for (bit = 0; bit < 32; bit++) {
                LONG blkno = 2 + (i * 127 + w) * 32 + bit;
                if (blkno >= NBLOCKS)
                    break;
                if (!(v & (1UL << bit)))
                    fs->flags[blkno] |= BF_USED;
            }
        }
    }

    /* 3. the tree: drawers in a queue, entries along their hash chains */
    queue[qt++] = 0;
    while (qh < qt) {
        int dir = queue[qh++], slot;
        LONG dhb = fs->obj[dir].header;
        static ULONG table[72];
        if (disk_read(d, dhb, blk)) {
            fs->obj[dir].status |= OS_UNREADABLE;
            continue;
        }
        for (slot = 0; slot < 72; slot++)
            table[slot] = fs_long(blk, 24 + slot * 4);
        if (fs->dirc)
            dircache(fs, d, dir, fs_long(blk, 0x1F8));
        for (slot = 0; slot < 72; slot++) {
            ULONG e = table[slot];
            LONG steps = 0;
            while (e && steps++ < NBLOCKS) {
                int o, st;
                LONG rerr;
                if (!inrange(e)) {
                    prob(fs, F_CHAINRANGE, dhb, dir);
                    fs->obj[dir].status |= OS_DAMAGED;
                    break;
                }
                if (fs->owner[e]) {                      /* already used: a loop or a cross-link */
                    prob(fs, F_CHAINLOOP, e, dir);
                    fs->obj[dir].status |= OS_DAMAGED;
                    break;
                }
                if ((rerr = disk_read(d, e, blk)) || fs_long(blk, 0) != T_HEADER) {
                    prob(fs, rerr ? F_UNREAD : F_CHAINTYPE, e, dir);
                    fs->obj[dir].status |= OS_DAMAGED;
                    break;
                }
                st = (int)(LONG)fs_long(blk, 0x1FC);
                if ((o = newobj(fs)) < 0) {
                    *why = "not enough memory for so many files";
                    return ERROR_NO_FREE_STORE;
                }
                bcpl(blk, 0x1B0, 30, fs->obj[o].name);
                fs->obj[o].parent = (UWORD)dir;
                fs->obj[o].header = (UWORD)e;
                fs->obj[o].size = (LONG)fs_long(blk, 0x144);
                fs->obj[o].prot = (LONG)fs_long(blk, 0x140);
                datestamp(blk, 0x1A4, &fs->obj[o].date);
                fs->obj[o].kind = st == SEC_DIR ? OK_DIR : st == SEC_FILE ? OK_FILE
                                : st == SEC_SOFT ? OK_SOFTLINK : OK_HARDLINK;
                if ((LONG)fs_long(blk, 0x1F4) != dhb) {  /* its parent isn't this drawer */
                    prob(fs, F_PARENT, e, o);
                    fs->obj[o].status |= OS_DAMAGED;
                }
                if (fs_hash(fs->obj[o].name, fs->intl) != slot) {   /* AmigaDOS won't find it */
                    prob(fs, F_HASHSLOT, e, o);
                    fs->obj[o].status |= OS_DAMAGED;
                }
                claim(fs, e, st == SEC_DIR ? BK_DIR : st == SEC_FILE ? BK_FILE : BK_LINK, o);
                e = fs_long(blk, 0x1F0);                 /* next in the hash chain */
                if (st == SEC_DIR) {
                    fs->obj[o].size = 0;
                    if (qt < NBLOCKS)
                        queue[qt++] = (UWORD)o;
                } else if (st == SEC_FILE)
                    fragments(fs, d, o);                 /* (reads other blocks: e is saved) */
            }
        }
    }

    /* 4. the bitmap against what is really in use */
    for (b = 2; b < NBLOCKS; b++) {
        BOOL used = fs->owner[b] != 0;
        if (fs->flags[b] & BF_READERR) {
            if (!used)
                fs->kind[b] = BK_UNREAD;
        } else if (!used)
            fs->kind[b] = (fs->flags[b] & BF_USED) ? BK_LOST : BK_FREE;
        else if (!(fs->flags[b] & BF_USED))
            fs->flags[b] |= BF_UNMARKED;
        if (used) {
            fs->usedreal++;
            if ((fs->flags[b] & BF_READERR) && fs->owner[b] - 1 < fs->nobj)
                fs->obj[fs->owner[b] - 1].status |= OS_UNREADABLE;
        }
        if (fs->flags[b] & BF_USED)
            fs->usedbm++;
        if (used && fs->kind[b] != BK_DATA && fs->kind[b] != BK_UNREAD && !fs->sum[b].csumok &&
            !(fs->flags[b] & BF_READERR) && fs->kind[b] != BK_BITMAP)
            fs->flags[b] |= BF_CSUM;                      /* headers, lists, dircache */
        if (used && fs->kind[b] == BK_DATA && !fs->ffs && !fs->sum[b].csumok)
            fs->flags[b] |= BF_CSUM;                      /* OFS data blocks have one too */
    }
    fs->usedreal += 2;                                    /* the boot blocks */
    return 0;
}
