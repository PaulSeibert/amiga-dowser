/* Dowser command mode: "dowser CMD <command> <source>" looks at a disk without the window.
 * <source> is DF0:-DF3: or an .adf file. Used for scripting and for Dowser's own tests.
 * Written by Paul Seibert, (c) 2026 Desert Palms Software. */
#include <exec/memory.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include "util.h"
#include "dos13.h"
#include "disk.h"
#include "fs.h"
#include "boot.h"
#include "check.h"
#include "blockview.h"
#include "recover.h"
#include "scan.h"
#include "cmd.h"

static struct FS fs;             /* static: far too big for a stack */
static struct Disk disk;
static struct Check chk;         /* static: big */
static struct Recover rec;
static struct Surface surf;
static char line[600];

static void out(const char *s)
{
    Write(Output(), (APTR)s, slen(s));
}

static BOOL is(const char *a, const char *b)
{
    return !nocase_cmp(a, b);
}

static BOOL c_progress(void *ctx, LONG done, LONG total)
{
    if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) {
        out("***Break\n");
        return FALSE;
    }
    return TRUE;
}

/* open and read the source; FALSE (with a message) if that failed */
static BOOL load(const char *src)
{
    const char *why;
    LONG err = disk_open(&disk, src, &why);
    if (err) {
        fmt(line, "dowser: cannot open %s: %s\n", src, why);
        out(line);
        return FALSE;
    }
    err = fs_scan(&fs, &disk, c_progress, NULL, &why);
    if (err) {
        disk_close(&disk);
        fmt(line, "dowser: cannot read %s: %s\n", src, why ? why : disk_errstr(err));
        out(line);
        return FALSE;
    }
    return TRUE;
}

static void unload(void)
{
    disk_close(&disk);
    fs_free(&fs);
}

static void dostype(char *buf, ULONG t)
{
    static const char *const kind[] = { "OFS", "FFS", "OFS international", "FFS international",
                                        "OFS directory cache", "FFS directory cache" };
    if ((t & 0xFFFFFF00UL) == 0x444F5300UL && (t & 0xFF) <= 5)
        fmt(buf, "DOS\\%ld (%s)", (LONG)(t & 0xFF), kind[t & 0xFF]);
    else
        fmt(buf, "$%08lx (not an AmigaDOS disk)", t);
}

static int info(void)
{
    char a[40], b[20], c[20];
    int i, files = 0, dirs = 0, links = 0;
    dostype(a, fs.dostype);
    fmt(line, "source: %s\ntype: %s\n", disk.name, a);
    out(line);
    if (!fs.dos)
        return 0;
    for (i = 1; i < fs.nobj; i++) {
        if (fs.obj[i].kind == OK_DIR)
            dirs++;
        else if (fs.obj[i].kind == OK_FILE)
            files++;
        else
            links++;
    }
    dos_datestr(b, &fs.created);
    dos_datestr(c, &fs.altered);
    fmt(line, "name: %s\ncreated: %s\naltered: %s\nbitmap: %s\n", fs.volname, b, c,
        fs.bmflag == -1 ? "valid" : "not valid (needs validating)");
    out(line);
    fmt(line, "blocks: %ld, in use %ld, bitmap says %ld, unreadable %ld\nfiles: %ld, drawers: %ld, links: %ld\n",
        (LONG)NBLOCKS, fs.usedreal, fs.usedbm + 2, fs.readerrs, (LONG)files, (LONG)dirs, (LONG)links);
    out(line);
    return 0;
}

/* the block map: 22 rows (head 0 sectors 0-10, then head 1) of 80 cylinders */
static int map(void)
{
    int row, cyl;
    for (row = 0; row < 2 * SECTORS; row++) {
        for (cyl = 0; cyl < 80; cyl++) {
            LONG b = (LONG)(cyl * 2 + row / SECTORS) * SECTORS + row % SECTORS;
            line[cyl] = fs_kindchar(&fs, b);
        }
        line[80] = '\n';
        line[81] = 0;
        out(line);
    }
    return 0;
}

static int files(void)
{
    static const char kc[] = "dfsh";
    static char path[PATHMAX];
    int i;
    for (i = 1; i < fs.nobj; i++) {
        struct Obj *o = &fs.obj[i];
        fs_path(&fs, i, path, PATHMAX);
        fmt(line, "%s|%lc|%ld|%ld|%ld|%ld|%ld\n", path, (LONG)kc[o->kind], o->kind == OK_FILE ? o->size : 0L,
            (LONG)o->nblocks, (LONG)o->frags, (LONG)o->header, (LONG)o->status);
        out(line);
    }
    return 0;
}

static int block(LONG b)
{
    static char text[6000];
    LONG n = bv_text(&fs, &disk, b, text, sizeof(text));
    Write(Output(), text, n);
    return b >= 0 && b < NBLOCKS ? 0 : 10;
}

static int check(void)
{
    static char path[PATHMAX];
    int i;
    check_run(&fs, &disk, &chk);
    for (i = 0; i < chk.nf; i++) {
        check_line(&fs, &chk.f[i], line, sizeof(line));
        scat(line, "\n", sizeof(line));
        out(line);
    }
    if (chk.nf < chk.errors + chk.warnings) {
        fmt(line, "(and %ld more)\n", chk.errors + chk.warnings - chk.nf);
        out(line);
    }
    if (!chk.errors && !chk.warnings)
        fmt(line, "result: no problems found\n");
    else
        fmt(line, "result: %ld error%s, %ld warning%s\n", chk.errors, chk.errors == 1 ? "" : "s",
            chk.warnings, chk.warnings == 1 ? "" : "s");
    out(line);
    if (fs.dos) {
        path[0] = 0;
        if (chk.maxfragobj > 0)
            fs_path(&fs, chk.maxfragobj, path, PATHMAX);
        if (chk.fragfiles)
            fmt(line, "fragmented: %ld of %ld files (most pieces: %ld, \"%s\")\n", chk.fragfiles, chk.files,
                (LONG)chk.maxfrags, path);
        else
            fmt(line, "fragmented: 0 of %ld files\n", chk.files);
        out(line);
    }
    return chk.errors ? 10 : chk.warnings ? 5 : 0;
}

static int boot(void)
{
    static ULONG bl[256];
    struct BootInfo bi;
    char t[48];
    int i;
    disk_read(&disk, 0, (UBYTE *)bl);
    disk_read(&disk, 1, (UBYTE *)bl + BSIZE);
    boot_inspect((UBYTE *)bl, &bi);
    if ((bi.dostype & 0xFFFFFF00UL) == 0x444F5300UL)
        fmt(t, "DOS\\%ld", (LONG)(bi.dostype & 0xFF));
    else
        fmt(t, "$%08lx", bi.dostype);
    fmt(line, "type: %s\nchecksum: %s\ncode: %s\ncrc32: %08lx\n", t, bi.sumok ? "ok" : "wrong",
        boot_kindname(bi.kind), bi.crc);
    out(line);
    if (bi.kind == BOOT_NOCODE)
        out("bootable: no (fine for a data disk)\n");
    else if (!bi.sumok)
        out("bootable: no: the checksum is wrong, so Kickstart won't run the code\n");
    else
        out("bootable: yes\n");
    if (bi.kind == BOOT_CUSTOM)
        out("note: custom boot code can be a game loader, a utility or a virus; look up its crc32\n");
    for (i = 0; i < bi.nstr; i++) {
        fmt(line, "text: %s\n", bi.str[i]);
        out(line);
    }
    return 0;
}

/* ---- recovery ----------------------------------------------------------------------------- */

static int deleted(void)
{
    static char where[PATHMAX];
    int i, whole = 0;
    rec_scan(&fs, &disk, &rec);
    for (i = 0; i < rec.n; i++) {
        struct Found *f = &rec.f[i];
        rec_where(&fs, &rec, i, where, PATHMAX);
        fmt(line, "%s|%lc|%ld|%s|%s|%s|%ld\n", f->name, (LONG)(f->dir ? 'd' : 'f'), f->size, rec_status(f),
            f->orphan ? "orphan" : "deleted", where, (LONG)f->header);
        out(line);
        if (!f->dir && f->status == FD_WHOLE)
            whole++;
    }
    fmt(line, "found: %ld (recoverable files: %ld)\n", (LONG)rec.n, (LONG)whole);
    out(line);
    return 0;
}

static BOOL r_progress(void *ctx, const char *name)
{
    return c_progress(ctx, 0, 0);
}

/* RESCUE <src> ALL | <path> | DELETED <name> | DELETED ALL  TO <drawer> */
static int rescue(char **what, int n, const char *dest)
{
    static char path[PATHMAX];
    struct RescueStats st;
    char *z = (char *)&st;
    int i, k;
    BPTR l;
    for (i = 0; i < (int)sizeof(st); i++)
        z[i] = 0;
    (void)l;
    if (n == 1 && !nocase_cmp(what[0], "ALL"))
        rec_rescue(&fs, &disk, 0, dest, &st, r_progress, NULL);
    else if (n == 2 && !nocase_cmp(what[0], "DELETED")) {
        rec_scan(&fs, &disk, &rec);
        for (k = 0; k < rec.n && !st.aborted; k++)
            if (!rec.f[k].dir && (!nocase_cmp(what[1], "ALL") || !nocase_cmp(what[1], rec.f[k].name)))
                rec_rescue_found(&fs, &disk, &rec, k, dest, !nocase_cmp(what[1], "ALL"), &st, r_progress, NULL);
    } else if (n == 1) {
        for (k = 1; k < fs.nobj; k++) {
            fs_path(&fs, k, path, PATHMAX);
            if (!nocase_cmp(path, what[0]))
                break;
        }
        if (k == fs.nobj) {
            fmt(line, "dowser: no %s on the disk\n", what[0]);
            out(line);
            return 10;
        }
        rec_rescue(&fs, &disk, k, dest, &st, r_progress, NULL);
    } else {
        out("dowser: RESCUE <source> ALL | <path> | DELETED <name> | DELETED ALL TO <drawer>\n");
        return 10;
    }
    fmt(line, "Rescued %ld files and %ld drawers (%ld bytes).\n", st.files, st.dirs, st.bytes);
    out(line);
    if (st.badblocks) {
        fmt(line, "Blocks that could not be read, written as zeros: %ld.\n", st.badblocks);
        out(line);
    }
    if (st.err) {
        fmt(line, "dowser: could not write %s: %s\n", st.errpath, dos_errstr(st.err));
        out(line);
    }
    if (st.aborted)
        out("Stopped.\n");
    return st.err || st.aborted ? 10 : st.badblocks ? 5 : 0;
}

static BOOL s_progress(void *ctx, int t)
{
    return c_progress(ctx, 0, 0);
}

static int scan(void)
{
    int t;
    LONG err;
    if (disk.unit < 0) {
        out("an image file: every track can be read (the surface scan is for floppies)\n");
        return 0;
    }
    if ((err = scan_run(&disk, &surf, s_progress, NULL))) {
        fmt(line, "dowser: %s\n", disk_errstr(err));
        out(line);
        return 10;
    }
    for (t = 0; t < NTRACKS; t++)
        if (surf.track[t] != TRK_OK) {
            fmt(line, "track %s: %s\n", scan_trackname(t), disk_errstr(surf.err[t]));
            out(line);
        }
    fmt(line, "result: %ld tracks read, %ld fine, %ld without AmigaDOS sectors, %ld damaged%s\n", (LONG)surf.done,
        (LONG)(surf.done - surf.nodos - surf.damaged), (LONG)surf.nodos, (LONG)surf.damaged, surf.aborted ? " (stopped)" : "");
    out(line);
    return surf.nodos || surf.damaged ? 5 : 0;
}

int cmd_run(int argc, char **argv)
{
    int rc = 5;
    if (argc >= 3 && is(argv[2], "MEM")) {
        fmt(line, "chip %ld fast %ld total %ld\n", AvailMem(MEMF_CHIP), AvailMem(MEMF_FAST), AvailMem(0));
        out(line);
        return 0;
    }
    if (argc >= 4 && (is(argv[2], "INFO") || is(argv[2], "MAP") || is(argv[2], "FILES") || is(argv[2], "REPORT") ||
                      is(argv[2], "CHECK") || is(argv[2], "BOOT") || is(argv[2], "DELETED") || is(argv[2], "SCAN") ||
                      (is(argv[2], "BLOCK") && argc >= 5))) {
        if (!load(argv[3]))
            return 10;
        if (is(argv[2], "INFO"))
            rc = info();
        else if (is(argv[2], "MAP"))
            rc = map();
        else if (is(argv[2], "FILES"))
            rc = files();
        else if (is(argv[2], "REPORT")) {           /* all three, from one reading of the disk */
            out("[INFO]\n");
            info();
            out("[MAP]\n");
            map();
            out("[FILES]\n");
            files();
            out("[CHECK]\n");
            rc = check();
            out("[BOOT]\n");
            boot();
        } else if (is(argv[2], "CHECK"))
            rc = check();
        else if (is(argv[2], "DELETED"))
            rc = deleted();
        else if (is(argv[2], "SCAN"))
            rc = scan();
        else if (is(argv[2], "BOOT"))
            rc = boot();
        else {
            LONG b = 0;
            const char *c;
            for (c = argv[4]; *c >= '0' && *c <= '9'; c++)
                b = b * 10 + *c - '0';
            rc = block(b);
        }
        unload();
        return rc;
    }
    if (argc >= 7 && is(argv[2], "RESCUE") && is(argv[argc - 2], "TO")) {
        BPTR l;
        /* the destination first: once the drive is Dowser's, DOS can't answer about it
           (and no "insert disk" or "not a DOS disk" requester that would wait for a click) */
        dos_quiet(TRUE);
        l = Lock((STRPTR)argv[argc - 1], ACCESS_READ);
        dos_quiet(FALSE);
        if (!l) {
            fmt(line, "dowser: no drawer %s to rescue into\n", argv[argc - 1]);
            out(line);
            return 10;
        }
        UnLock(l);
        if (rec_samedisk(argv[3], argv[argc - 1])) {
            out("dowser: will not rescue onto the disk being read (it could overwrite what is rescued)\n");
            return 10;
        }
        if (!load(argv[3]))
            return 10;
        rc = rescue(argv + 4, argc - 6, argv[argc - 1]);
        unload();
        return rc;
    }
    out("Dowser command mode. Usage (source: DF0:-DF3: or an .adf file):\n"
        "  dowser CMD INFO <source>         the disk: type, name, dates, blocks, files\n"
        "  dowser CMD MAP <source>          the block map (80 cylinders x 22 blocks)\n"
        "  dowser CMD FILES <source>        every file and drawer: size, blocks, fragments\n"
        "  dowser CMD CHECK <source>        what is wrong with the disk, in plain English\n"
        "  dowser CMD BOOT <source>         the boot block: checksum, boot code, text in it\n"
        "  dowser CMD REPORT <source>       all of the above from one reading of the disk\n"
        "  dowser CMD DELETED <source>      deleted files (and files whose drawer is lost) still on it\n"
        "  dowser CMD RESCUE <source> ALL | <path> | DELETED <name> | DELETED ALL TO <drawer>\n"
        "                                   copy files off the disk, around any damage\n"
        "  dowser CMD SCAN <source>         read every track of a floppy afresh\n"
        "  dowser CMD BLOCK <source> <n>    one block: what it is, and its contents\n"
        "  dowser CMD MEM                   free memory\n");
    return rc;
}
