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
#include "cmd.h"

static struct FS fs;             /* static: far too big for a stack */
static struct Disk disk;
static struct Check chk;         /* static: big */
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
    static ULONG bufl[BSIZE / 4];
    UBYTE *buf = (UBYTE *)bufl;
    static char path[PATHMAX];
    LONG err, i, k;
    if (b < 0 || b >= NBLOCKS) {
        out("dowser: no such block\n");
        return 10;
    }
    path[0] = 0;
    if (fs.owner[b])
        fs_path(&fs, fs.owner[b] - 1, path, PATHMAX);
    fmt(line, "block %ld: %s%s%s%s\n", b, fs_kindname(fs.kind[b]), fs.owner[b] ? " of \"" : "",
        path, fs.owner[b] ? "\"" : "");
    out(line);
    err = disk_read(&disk, b, buf);
    if (err) {
        fmt(line, "read: %s\n", disk_errstr(err));
        out(line);
        return 5;
    }
    fmt(line, "flags: %s%s%s%s\n", fs.flags[b] & BF_USED ? "used " : "free ",
        fs.flags[b] & BF_CSUM ? "checksum-bad " : "", fs.flags[b] & BF_TWICE ? "used-twice " : "",
        fs.flags[b] & BF_UNMARKED ? "not-marked-in-bitmap " : "");
    out(line);
    for (i = 0; i < BSIZE; i += 16) {                /* hex and ASCII */
        char *d = line;
        static const char hx[] = "0123456789ABCDEF";
        fmt(d, "%03lx: ", i);
        d += 5;
        for (k = 0; k < 16; k++) {
            *d++ = hx[buf[i + k] >> 4];
            *d++ = hx[buf[i + k] & 15];
            if (k % 4 == 3)
                *d++ = ' ';
        }
        *d++ = ' ';
        for (k = 0; k < 16; k++) {
            UBYTE c = buf[i + k];
            *d++ = (c >= 32 && c < 127) || c >= 0xA0 ? c : '.';
        }
        *d++ = '\n';
        *d = 0;
        out(line);
    }
    return 0;
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

int cmd_run(int argc, char **argv)
{
    int rc = 5;
    if (argc >= 3 && is(argv[2], "MEM")) {
        fmt(line, "chip %ld fast %ld total %ld\n", AvailMem(MEMF_CHIP), AvailMem(MEMF_FAST), AvailMem(0));
        out(line);
        return 0;
    }
    if (argc >= 4 && (is(argv[2], "INFO") || is(argv[2], "MAP") || is(argv[2], "FILES") || is(argv[2], "REPORT") ||
                      is(argv[2], "CHECK") || is(argv[2], "BOOT") ||
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
    out("Dowser command mode. Usage (source: DF0:-DF3: or an .adf file):\n"
        "  dowser CMD INFO <source>         the disk: type, name, dates, blocks, files\n"
        "  dowser CMD MAP <source>          the block map (80 cylinders x 22 blocks)\n"
        "  dowser CMD FILES <source>        every file and drawer: size, blocks, fragments\n"
        "  dowser CMD CHECK <source>        what is wrong with the disk, in plain English\n"
        "  dowser CMD BOOT <source>         the boot block: checksum, boot code, text in it\n"
        "  dowser CMD REPORT <source>       all of the above from one reading of the disk\n"
        "  dowser CMD BLOCK <source> <n>    one block: what it is, and its contents\n"
        "  dowser CMD MEM                   free memory\n");
    return rc;
}
