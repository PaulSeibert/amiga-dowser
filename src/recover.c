/* Dowser - recovery and rescue. Written by Paul Seibert, (c) 2026 Desert Palms Software.
 *
 * When AmigaDOS deletes a file it takes it out of its drawer's hash chain and marks its
 * blocks free, but leaves the header and the data where they were until something else is
 * written over them. So a free block that still holds a valid file header is a deleted file;
 * a header still marked in use that no drawer leads to is an orphan (its drawer is lost).
 * Its data blocks are whole if no living file has taken them since. */
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include "util.h"
#include "dos13.h"
#include "disk.h"
#include "fs.h"
#include "recover.h"

static ULONG blkl[BSIZE / 4];
static UBYTE *const blk = (UBYTE *)blkl;
static UWORD list[NBLOCKS];

static BOOL inrange(ULONG b)
{
    return b >= 2 && b < NBLOCKS;
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

/* the data blocks listed by header h and its extension blocks (no trust in anything) */
static int datalist(struct Disk *d, LONG h, UWORD *out, int max)
{
    LONG b = h, steps = 0;
    int n = 0, i;
    BOOL first = TRUE;
    while (b && inrange(b) && steps++ < 100) {
        LONG cnt;
        if (disk_read(d, b, blk))
            break;
        if (!first && (fs_long(blk, 0) != T_LIST || (LONG)fs_long(blk, 0x1F4) != h))
            break;
        cnt = (LONG)fs_long(blk, 8);
        if (cnt < 0 || cnt > 72)
            cnt = 72;
        for (i = 0; i < cnt; i++) {
            ULONG db = fs_long(blk, 0x134 - i * 4);
            if (db && n < max)
                out[n++] = (UWORD)db;
        }
        b = (LONG)fs_long(blk, 0x1F8);
        first = FALSE;
    }
    return n;
}

const char *rec_status(const struct Found *f)
{
    if (f->dir)
        return "drawer";
    return f->status == FD_WHOLE ? "recoverable" : f->status == FD_PART ? "partly overwritten" : "overwritten";
}

void rec_scan(struct FS *fs, struct Disk *d, struct Recover *r)
{
    LONG b;
    int i;
    r->n = 0;
    if (!fs->dos)
        return;
    for (b = 2; b < NBLOCKS && r->n < MAXFOUND; b++) {
        struct BSum *s = &fs->sum[b];
        struct Found *f;
        LONG per = fs->ffs ? 512 : 488, want;
        if (fs->owner[b] || s->type != T_HEADER || !s->csumok || s->key != b ||
            (s->sectype != SEC_FILE && s->sectype != SEC_DIR))
            continue;
        if (disk_read(d, b, blk))
            continue;
        f = &r->f[r->n++];
        bcpl(blk, 0x1B0, 30, f->name);
        f->dir = s->sectype == SEC_DIR;
        f->header = (UWORD)b;
        f->parent = (UWORD)fs_long(blk, 0x1F4);
        f->size = f->dir ? 0 : (LONG)fs_long(blk, 0x144);
        f->prot = (LONG)fs_long(blk, 0x140);
        f->date.ds_Days = fs_long(blk, 0x1A4);
        f->date.ds_Minute = fs_long(blk, 0x1A8);
        f->date.ds_Tick = fs_long(blk, 0x1AC);
        f->orphan = (fs->flags[b] & BF_USED) != 0 || !fs->rootok;   /* (no root: no bitmap to tell) */
        f->nblocks = f->lost = 0;
        f->status = FD_WHOLE;
        if (f->dir)
            continue;
        f->nblocks = (UWORD)datalist(d, b, list, NBLOCKS);
        for (i = 0; i < f->nblocks; i++) {
            LONG db = list[i];
            if (!inrange(db) || fs->owner[db] || (fs->flags[db] & BF_READERR) ||
                (!fs->ffs && (fs->sum[db].type != T_DATA || fs->sum[db].key != b)))
                f->lost++;
        }
        want = (f->size + per - 1) / per;
        if (f->size < 0 || f->nblocks < want)
            f->lost += (UWORD)(want - f->nblocks);
        f->status = !f->lost ? FD_WHOLE : f->lost < want ? FD_PART : FD_GONE;
    }
}

void rec_where(struct FS *fs, struct Recover *r, int i, char *out, int size)
{
    static char tmp[PATHMAX];
    LONG p = r->f[i].parent;
    int depth = 0, k;
    out[0] = 0;
    while (depth++ < 16) {
        if (!inrange(p)) {
            if (!out[0])
                scopy(out, "?", size);
            return;
        }
        if (fs->owner[p] && (fs->kind[p] == BK_DIR || fs->kind[p] == BK_ROOT)) {   /* a living drawer */
            fs_path(fs, fs->owner[p] - 1, tmp, PATHMAX);
            if (tmp[0] && out[0])
                scat(tmp, "/", PATHMAX);
            scat(tmp, out, PATHMAX);
            scopy(out, tmp, size);
            return;
        }
        if (p == fs->root)
            return;
        for (k = 0; k < r->n && !(r->f[k].dir && r->f[k].header == p); k++)
            ;
        if (k == r->n) {                              /* a drawer nobody knows any more */
            scopy(tmp, "?/", PATHMAX);
            scat(tmp, out, PATHMAX);
            scopy(out, tmp, size);
            return;
        }
        scopy(tmp, r->f[k].name, PATHMAX);            /* a lost or deleted drawer */
        if (out[0])
            scat(tmp, "/", PATHMAX);
        scat(tmp, out, PATHMAX);
        scopy(out, tmp, size);
        p = r->f[k].parent;
    }
}

BOOL rec_samedisk(const char *source, const char *dest)
{
    char dev[6];
    struct MsgPort *h;
    BPTR l;
    BOOL same = FALSE;
    int unit = disk_unit(source);
    if (unit < 0)
        return FALSE;                                 /* an image file isn't changed by copying out of it */
    fmt(dev, "DF%ld:", (LONG)unit);
    h = (struct MsgPort *)DeviceProc((STRPTR)dev);
    dos_quiet(TRUE);
    l = Lock((STRPTR)dest, ACCESS_READ);
    dos_quiet(FALSE);
    if (l) {
        same = h && ((struct FileLock *)BADDR(l))->fl_Task == h;
        UnLock(l);
    }
    return same;
}

/* ---- copying ------------------------------------------------------------------------------ */

static void fail(struct RescueStats *st, const char *path)
{
    if (!st->err) {
        st->err = IoErr() ? IoErr() : ERROR_OBJECT_NOT_FOUND;
        scopy(st->errpath, path, sizeof(st->errpath));
    }
}

/* write a file from its data blocks; unreadable blocks, and blocks that now belong to another
   file, become zeros. self: the file's object + 1 (0 for a found file: any owner is foreign) */
static void copyfile(struct FS *fs, struct Disk *d, LONG header, LONG size, int self, const char *path,
                     struct RescueStats *st, RecProgress p, void *ctx)
{
    static UBYTE zeros[BSIZE];
    LONG per = fs->ffs ? 512 : 488, done = 0;
    int n, i;
    BPTR out;
    if (p && !p(ctx, path)) {
        st->aborted = TRUE;
        return;
    }
    if (size < 0)
        size = 0;
    n = datalist(d, header, list, NBLOCKS);
    if (!(out = Open((STRPTR)path, MODE_NEWFILE))) {
        fail(st, path);
        return;
    }
    for (i = 0; done < size; i++) {
        LONG want = size - done < per ? size - done : per, b = i < n ? list[i] : 0;
        const UBYTE *src = zeros;
        BOOL good = FALSE;
        if (inrange(b) && fs->owner[b] == self && !disk_read(d, b, blk)) {
            if (fs->ffs) {
                src = blk;
                good = TRUE;
            } else if (fs_long(blk, 0) == T_DATA && (LONG)fs_long(blk, 4) == header) {
                src = blk + 24;
                good = TRUE;
            }
        }
        if (!good)
            st->badblocks++;
        if (Write(out, (APTR)src, want) != want) {
            fail(st, path);
            break;
        }
        done += want;
        st->bytes += want;
    }
    Close(out);
    st->files++;
}

static void setmeta(const char *path, LONG prot, const struct DateStamp *ds, const char *comment)
{
    dos_setdate(path, ds);
    if (comment && comment[0])
        SetComment((STRPTR)path, (STRPTR)comment);
    SetProtection((STRPTR)path, prot & ~0x0F);        /* rwed allowed, so the copy can be used */
}

/* the drawer walk shares one path buffer that each level extends and cuts back (1.3 stacks
   are 4 KB: no paths on the stack) */
static char cur[PATHMAX];
static int depth;

static void rescueobj(struct FS *fs, struct Disk *d, int o, struct RescueStats *st, RecProgress p, void *ctx)
{
    static char comment[80];
    struct Obj *ob = &fs->obj[o];
    int k, len = slen(cur);
    if (st->aborted || depth > 32)
        return;
    if (o) {
        if (len + slen(ob->name) + 2 > PATHMAX)
            return;
        path_add(cur, ob->name);
    }
    comment[0] = 0;
    if (o && !disk_read(d, ob->header, blk))
        bcpl(blk, 0x148, 79, comment);
    if (ob->kind == OK_FILE) {
        copyfile(fs, d, ob->header, ob->size, o + 1, cur, st, p, ctx);
        setmeta(cur, ob->prot, &ob->date, comment);
    } else if (ob->kind == OK_DIR) {
        BOOL made = TRUE;
        if (o) {
            BPTR l = CreateDir((STRPTR)cur);
            if (l)
                UnLock(l);
            else if (IoErr() != ERROR_OBJECT_EXISTS) {
                fail(st, cur);
                made = FALSE;
            }
            if (made)
                st->dirs++;
        }
        if (made) {
            depth++;
            for (k = 1; k < fs->nobj && !st->aborted; k++)
                if (fs->obj[k].parent == o && k != o)
                    rescueobj(fs, d, k, st, p, ctx);
            depth--;
            if (o)
                setmeta(cur, ob->prot, &ob->date, comment);
        }
    }
    cur[len] = 0;
}

void rec_rescue(struct FS *fs, struct Disk *d, int obj, const char *dest, struct RescueStats *st,
                RecProgress p, void *ctx)
{
    depth = 0;
    scopy(cur, dest, PATHMAX);
    if (obj >= 0 && obj < fs->nobj)
        rescueobj(fs, d, obj, st, p, ctx);
}

void rec_rescue_found(struct FS *fs, struct Disk *d, struct Recover *r, int i, const char *dest,
                      BOOL keeppath, struct RescueStats *st, RecProgress p, void *ctx)
{
    static char path[PATHMAX], base[PATHMAX], where[PATHMAX], part[32];
    struct Found *f = &r->f[i];
    BPTR l;
    int k, n;
    if (f->dir)
        return;
    scopy(base, dest, PATHMAX);
    if (keeppath) {                                   /* make the drawers it was in */
        rec_where(fs, r, i, where, PATHMAX);
        for (k = 0, n = 0; ; k++) {
            if (where[k] == '/' || !where[k]) {
                part[n] = 0;
                if (n) {
                    path_add(base, part[0] == '?' && !part[1] ? "lost drawer" : part);
                    if ((l = CreateDir((STRPTR)base)))
                        UnLock(l);
                }
                n = 0;
                if (!where[k])
                    break;
            } else if (n < 30)
                part[n++] = where[k];
        }
    }
    path_add(base, f->name);
    /* two found files of the same name: keep both (name, name.1, name.2, ...) */
    scopy(path, base, PATHMAX);
    dos_quiet(TRUE);
    for (k = 1; k < 100 && (l = Lock((STRPTR)path, ACCESS_READ)); k++) {
        char suffix[8];
        UnLock(l);
        fmt(suffix, ".%ld", (LONG)k);
        scopy(path, base, PATHMAX);
        scat(path, suffix, PATHMAX);
    }
    dos_quiet(FALSE);
    copyfile(fs, d, f->header, f->size, 0, path, st, p, ctx);
    setmeta(path, f->prot, &f->date, NULL);
}
