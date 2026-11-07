/* Dowser - the disk check. Written by Paul Seibert, (c) 2026 Desert Palms Software. */
#include <exec/memory.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include "util.h"
#include "dos13.h"
#include "disk.h"
#include "fs.h"
#include "boot.h"
#include "check.h"

static const char *const codes[F_COUNT] = {
    "", "NOTDOS", "BOOTSUM", "ROOT", "ROOTCSUM", "BMINVALID", "BMCSUM", "BMBAD", "HDRCSUM",
    "DATACSUM", "TWICE", "UNMARKED", "LOST", "CHAINRANGE", "CHAINTYPE", "CHAINLOOP", "PARENT",
    "HASHSLOT", "EXTBAD", "DATATYPE", "DATAOWNER", "DATASEQ", "DATASIZE", "SIZE", "UNREAD",
    "UNREADFREE", "DIRCACHE"
};

static const char *const texts[F_COUNT] = {
    "",
    "This is not an AmigaDOS disk (a game, a custom format, or blank).",
    "The boot block's checksum is wrong, so Kickstart won't boot this disk.",
    "The root block is damaged: the disk's name and top drawer can't be read.",
    "The root block's checksum is wrong.",
    "The bitmap is marked not valid: AmigaDOS will validate the disk when it is inserted.",
    "A bitmap block's checksum is wrong: AmigaDOS will treat the disk as not validated.",
    "The bitmap is missing or points outside the disk.",
    "Its header's checksum is wrong.",
    "A data block's checksum is wrong: the file may be damaged here.",
    "This block belongs to two files at once.",
    "In use, but the bitmap says this block is free: AmigaDOS may overwrite it.",
    "blocks are marked in use but belong to nothing (lost space).",
    "A drawer entry points outside the disk.",
    "A drawer entry points to a block that is not a file or drawer header.",
    "A drawer's entry chain loops back or reuses a block.",
    "Its parent pointer doesn't point to the drawer it is in.",
    "It is in the wrong hash slot: AmigaDOS won't find it by name.",
    "Its list of blocks is broken (a bad extension block).",
    "A block in its list is not a data block.",
    "A data block says it belongs to another file.",
    "A data block has the wrong sequence number.",
    "A data block holds the wrong number of bytes.",
    "Its size doesn't match its blocks.",
    "This block can't be read.",
    "free blocks can't be read.",
    "The drawer's directory cache doesn't match its contents."
};

static const UBYTE sev[F_COUNT] = {
    0, SEV_INFO, SEV_WARN, SEV_ERROR, SEV_ERROR, SEV_WARN, SEV_ERROR, SEV_ERROR, SEV_ERROR,
    SEV_ERROR, SEV_ERROR, SEV_ERROR, SEV_WARN, SEV_ERROR, SEV_ERROR, SEV_ERROR, SEV_WARN,
    SEV_ERROR, SEV_ERROR, SEV_ERROR, SEV_ERROR, SEV_ERROR, SEV_WARN, SEV_ERROR, SEV_ERROR,
    SEV_WARN, SEV_WARN
};

const char *check_code(int code)
{
    return code > 0 && code < F_COUNT ? codes[code] : "?";
}

static void add(struct Check *c, int code, LONG block, int obj, int n)
{
    if (sev[code] == SEV_ERROR)
        c->errors++;
    else if (sev[code] == SEV_WARN)
        c->warnings++;
    if (c->nf < MAXFIND) {
        struct Finding *f = &c->f[c->nf++];
        f->code = (UBYTE)code;
        f->sev = sev[code];
        f->block = (UWORD)block;
        f->obj = obj < 0 ? NOOBJ : (UWORD)obj;
        f->n = (UWORD)n;
    }
}

void check_line(struct FS *fs, const struct Finding *f, char *out, int size)
{
    static const char *const sname[] = { "note", "warning", "error" };
    static char path[PATHMAX];
    char what[PATHMAX + 16];
    if (f->obj != NOOBJ && f->obj < fs->nobj && f->obj > 0) {
        fs_path(fs, f->obj, path, PATHMAX);
        fmt(what, "\"%s\"", path);
    } else if (f->obj == 0)
        scopy(what, "the top drawer", sizeof(what));
    else
        scopy(what, "the disk", sizeof(what));
    if (f->code == F_LOST || f->code == F_UNREADFREE)
        fmt(out, "%s: %ld %s (first: block %ld) [%s]", sname[f->sev], (LONG)f->n, texts[f->code],
            (LONG)f->block, codes[f->code]);
    else
        fmt(out, "%s: %s, block %ld: %s [%s]", sname[f->sev], what, (LONG)f->block, texts[f->code],
            codes[f->code]);
    (void)size;
}

/* ---- OFS data blocks ---------------------------------------------------------------------- */

static void ofsdata(struct FS *fs, struct Disk *d, struct Check *c, int o)
{
    static UWORD list[NBLOCKS];
    struct Obj *ob = &fs->obj[o];
    int n = fs_datablocks(fs, d, o, list, NBLOCKS), i;
    BOOL csum = FALSE, type = FALSE, owner = FALSE, seq = FALSE, size = FALSE;
    LONG left = ob->size;
    if (n > NBLOCKS)
        n = NBLOCKS;
    for (i = 0; i < n; i++) {
        LONG b = list[i], want = left > 488 ? 488 : left;
        struct BSum *s;
        if (b < 2 || b >= NBLOCKS || (fs->flags[b] & BF_READERR))
            continue;
        s = &fs->sum[b];
        left -= want;
        if (s->type != T_DATA) {
            if (!type)
                add(c, F_DATATYPE, b, o, 0);
            type = TRUE;
            continue;
        }
        if (s->key != ob->header && !owner) {
            add(c, F_DATAOWNER, b, o, 0);
            owner = TRUE;
        }
        if (s->seq != i + 1 && !seq) {
            add(c, F_DATASEQ, b, o, 0);
            seq = TRUE;
        }
        if (s->size != want && !size) {
            add(c, F_DATASIZE, b, o, 0);
            size = TRUE;
        }
        if (!s->csumok && !csum) {
            add(c, F_DATACSUM, b, o, 0);
            csum = TRUE;
        }
    }
    if (type || owner || seq || size || csum)
        ob->status |= OS_DAMAGED;
}

/* ---- directory caches (DOS\4, DOS\5) ---------------------------------------------------------- */

static void dircache(struct FS *fs, struct Disk *d, struct Check *c, int dir)
{
    static ULONG bl[BSIZE / 4];
    static UWORD seen[NBLOCKS / 4];
    UBYTE *b = (UBYTE *)bl;
    int nseen = 0, i, children = 0, found = 0;
    LONG blk, steps = 0, first = 0;
    /* the drawer's first cache block is in its header's extension field */
    if (disk_read(d, fs->obj[dir].header, b))
        return;
    blk = (LONG)fs_long(b, 0x1F8);
    first = blk;
    while (blk >= 2 && blk < NBLOCKS && steps++ < 200) {
        LONG nrec, pos = 24, r;
        if (disk_read(d, blk, b) || fs_long(b, 0) != T_DIRC)
            break;
        nrec = (LONG)fs_long(b, 12);
        for (r = 0; r < nrec && pos + 25 < BSIZE; r++) {
            int namelen = b[pos + 23], commlen;
            if (nseen < NBLOCKS / 4)
                seen[nseen++] = (UWORD)fs_long(b, pos);     /* its entry's header block */
            commlen = (pos + 24 + namelen < BSIZE) ? b[pos + 24 + namelen] : 0;
            pos += 25 + namelen + commlen;
            pos = (pos + 1) & ~1;                           /* records are word-aligned */
        }
        blk = (LONG)fs_long(b, 16);
    }
    for (i = 1; i < fs->nobj; i++)
        if (fs->obj[i].parent == dir) {
            int k;
            children++;
            for (k = 0; k < nseen && seen[k] != fs->obj[i].header; k++)
                ;
            if (k < nseen)
                found++;
        }
    if (found != children || nseen != children)
        add(c, F_DIRCACHE, first ? first : fs->obj[dir].header, dir, 0);
}

/* ---- the whole check -------------------------------------------------------------------------- */

void check_run(struct FS *fs, struct Disk *d, struct Check *c)
{
    static ULONG bootl[256];
    LONG b, lost = 0, firstlost = 0, unfree = 0, firstunfree = 0;
    int i;
    c->nf = 0;
    c->errors = c->warnings = 0;
    c->files = c->fragfiles = 0;
    c->maxfrags = 0;
    c->maxfragobj = -1;

    /* the boot block */
    disk_read(d, 0, (UBYTE *)bootl);
    disk_read(d, 1, (UBYTE *)bootl + BSIZE);
    boot_inspect((UBYTE *)bootl, &c->boot);
    if (!fs->dos) {
        add(c, F_NOTDOS, 0, -1, 0);
        return;
    }
    if (c->boot.code && !c->boot.sumok)
        add(c, F_BOOTSUM, 0, -1, 0);

    /* the root and the bitmap */
    if (fs->rootok) {
        if (!fs->sum[fs->root].csumok)
            add(c, F_ROOTCSUM, fs->root, 0, 0);
        if (fs->bmflag != -1)
            add(c, F_BMINVALID, fs->root, 0, 0);
    }
    for (i = 0; i < 25 && fs->bmblocks[i]; i++)
        if (fs->bmblocks[i] >= 2 && fs->bmblocks[i] < NBLOCKS && (fs->flags[fs->bmblocks[i]] & BF_CSUM))
            add(c, F_BMCSUM, fs->bmblocks[i], 0, 0);

    /* what the walk ran into */
    for (i = 0; i < fs->nprob; i++)
        add(c, fs->prob[i].code, fs->prob[i].block, fs->prob[i].obj == NOOBJ ? -1 : fs->prob[i].obj, 0);

    /* every block */
    for (b = 2; b < NBLOCKS; b++) {
        int o = fs->owner[b] - 1;
        UBYTE fl = fs->flags[b];
        if (fl & BF_READERR) {
            if (o >= 0)
                add(c, F_UNREAD, b, o, 0);
            else if (!unfree++)
                firstunfree = b;
            continue;
        }
        if (fl & BF_TWICE)
            add(c, F_TWICE, b, o, 0);
        if (fl & BF_UNMARKED)
            add(c, F_UNMARKED, b, o, 0);
        if (fs->kind[b] == BK_LOST && !lost++)
            firstlost = b;
        if ((fl & BF_CSUM) && fs->kind[b] != BK_DATA && fs->kind[b] != BK_ROOT && fs->kind[b] != BK_BITMAP)
            add(c, F_HDRCSUM, b, o, 0);
    }
    if (lost)
        add(c, F_LOST, firstlost, -1, (int)lost);
    if (unfree)
        add(c, F_UNREADFREE, firstunfree, -1, (int)unfree);

    /* files: OFS data blocks, sizes against blocks, fragments */
    for (i = 1; i < fs->nobj; i++) {
        struct Obj *ob = &fs->obj[i];
        LONG per = fs->ffs ? 512 : 488, want;
        if (ob->kind != OK_FILE)
            continue;
        c->files++;
        want = (ob->size + per - 1) / per;
        if (ob->size < 0 || want != ob->nblocks) {
            add(c, F_SIZE, ob->header, i, 0);
            ob->status |= OS_DAMAGED;
        }
        if (!fs->ffs)
            ofsdata(fs, d, c, i);
        if (ob->frags > 1) {
            c->fragfiles++;
            if (ob->frags > c->maxfrags) {
                c->maxfrags = ob->frags;
                c->maxfragobj = i;
            }
        }
    }
    if (fs->dirc)
        for (i = 0; i < fs->nobj; i++)
            if (fs->obj[i].kind == OK_DIR)
                dircache(fs, d, c, i);
}
