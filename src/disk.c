/* Dowser - block source. Written by Paul Seibert, (c) 2026 Desert Palms Software.
 *
 * A drive is read a whole track at a time into a chip RAM buffer (1.3's trackdisk needs one),
 * with retries, and DOS is kept off the drive (ACTION_INHIBIT) while Dowser uses it. */
#include <exec/memory.h>
#include <exec/io.h>
#include <devices/trackdisk.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include "util.h"
#include "dos13.h"
#include "disk.h"

static struct MsgPort *port;
static struct IOStdReq io;
static BOOL opened;
static struct MsgPort *handler;

static struct MsgPort *mkport(void)
{
    struct MsgPort *p = AllocMem(sizeof(*p), MEMF_PUBLIC | MEMF_CLEAR);
    BYTE sig;
    if (!p)
        return NULL;
    if ((sig = AllocSignal(-1)) < 0) {
        FreeMem(p, sizeof(*p));
        return NULL;
    }
    p->mp_Node.ln_Type = NT_MSGPORT;
    p->mp_Flags = PA_SIGNAL;
    p->mp_SigBit = sig;
    p->mp_SigTask = FindTask(NULL);
    p->mp_MsgList.lh_Head = (struct Node *)&p->mp_MsgList.lh_Tail;
    p->mp_MsgList.lh_TailPred = (struct Node *)&p->mp_MsgList.lh_Head;
    return p;
}

static void rmport(void)
{
    if (port) {
        FreeSignal(port->mp_SigBit);
        FreeMem(port, sizeof(*port));
    }
    port = NULL;
}

static LONG td(UWORD cmd, APTR data, ULONG len, ULONG off)
{
    io.io_Command = cmd;
    io.io_Data = data;
    io.io_Length = len;
    io.io_Offset = off;
    io.io_Flags = 0;
    return DoIO((struct IORequest *)&io);
}

BOOL disk_present(int unit)
{
    struct IOStdReq t = { 0 };
    BOOL there = OpenDevice((STRPTR)TD_NAME, unit, (struct IORequest *)&t, 0) == 0;
    if (there)
        CloseDevice((struct IORequest *)&t);
    return there;
}

int disk_unit(const char *s)
{
    if ((s[0] | 0x20) == 'd' && (s[1] | 0x20) == 'f' && s[2] >= '0' && s[2] <= '3' && (!s[3] || (s[3] == ':' && !s[4])))
        return s[2] - '0';
    return -1;
}

const char *disk_errstr(LONG e)
{
    switch (e) {
    case 0:                  return "fine";
    case TDERR_WriteProt:    return "write-protected";
    case TDERR_DiskChanged:  return "no disk in the drive";
    case TDERR_SeekError:    return "the drive could not find the track";
    case TDERR_NoMem:        return "not enough memory";
    case TDERR_BadUnitNum:   return "no such drive";
    case TDERR_BadDriveType: return "not a DD floppy drive";
    case TDERR_DriveInUse:   return "the drive is in use";
    case TDERR_NoSecHdr:     return "no sector headers (unformatted, blank or not an AmigaDOS track)";
    case TDERR_TooFewSecs:   return "too few sectors (damaged or a custom format)";
    case TDERR_BadSecPreamble: case TDERR_BadSecID:
                             return "damaged sector headers";
    case TDERR_BadHdrSum:    return "sector header checksum error";
    case TDERR_BadSecSum:    return "sector data checksum error";
    default:                 return e < 0 ? "can't be read" : "the drive reported an error";
    }
}

static void inhibit(struct Disk *d, BOOL on)
{
    LONG res2;
    if (on) {
        char dev[8];
        fmt(dev, "DF%ld:", (LONG)d->unit);
        handler = (struct MsgPort *)DeviceProc((STRPTR)dev);
        if (handler && dos_packet(handler, ACTION_INHIBIT, DOSTRUE, 0, 0, 0, &res2))
            d->inhibited = TRUE;
    } else if (d->inhibited && handler) {
        dos_packet(handler, ACTION_INHIBIT, DOSFALSE, 0, 0, 0, &res2);
        d->inhibited = FALSE;
    }
}

LONG disk_open(struct Disk *d, const char *src, const char **why)
{
    struct IOStdReq z = { 0 };
    LONG err;
    d->unit = disk_unit(src);
    d->fh = 0;
    d->cached = -1;
    d->cachederr = 0;
    d->inhibited = FALSE;
    d->readerrs = 0;
    scopy(d->name, src, sizeof(d->name));
    *why = NULL;
    if (d->unit < 0) {                          /* an image file */
        LONG size;
        if (!(d->cache = AllocMem(TRACKBYTES, MEMF_PUBLIC))) {
            *why = "not enough memory";
            return ERROR_NO_FREE_STORE;
        }
        dos_quiet(TRUE);
        d->fh = Open((STRPTR)src, MODE_OLDFILE);
        dos_quiet(FALSE);
        if (!d->fh) {
            err = IoErr();
            FreeMem(d->cache, TRACKBYTES);
            d->cache = NULL;
            *why = dos_errstr(err);
            return err;
        }
        Seek(d->fh, 0, OFFSET_END);
        size = Seek(d->fh, 0, OFFSET_BEGINNING);
        if (size != ADFSIZE) {
            disk_close(d);
            *why = "not an 880K disk image (it must be 901120 bytes)";
            return ERROR_OBJECT_WRONG_TYPE;
        }
        return 0;
    }
    if (!(d->cache = AllocMem(TRACKBYTES, MEMF_CHIP))) {
        *why = "not enough chip memory";
        return ERROR_NO_FREE_STORE;
    }
    io = z;
    if (!(port = mkport())) {
        disk_close(d);
        *why = "not enough memory";
        return ERROR_NO_FREE_STORE;
    }
    io.io_Message.mn_ReplyPort = port;
    io.io_Message.mn_Length = sizeof(io);
    if (OpenDevice((STRPTR)TD_NAME, d->unit, (struct IORequest *)&io, 0)) {
        rmport();
        disk_close(d);
        *why = "there is no such drive";
        return TDERR_BadUnitNum;
    }
    opened = TRUE;
    td(TD_CHANGESTATE, NULL, 0, 0);
    if (io.io_Actual) {
        disk_close(d);
        *why = "there is no disk in the drive";
        return TDERR_DiskChanged;
    }
    inhibit(d, TRUE);
    td(CMD_CLEAR, NULL, 0, 0);                  /* read the disk, not trackdisk's old buffer */
    td(TD_MOTOR, NULL, 1, 0);
    return 0;
}

static LONG secterr[SECTORS];               /* per sector of the cached track: 0 = readable */

/* the whole track; if that fails, sector by sector, so the readable sectors of a damaged
   track can still be used */
static LONG readtrack(struct Disk *d, LONG t)
{
    LONG e = 0;
    int tries, s, bad = 0;
    for (s = 0; s < SECTORS; s++)
        secterr[s] = 0;
    if (d->unit < 0) {
        if (Seek(d->fh, t * TRACKBYTES, OFFSET_BEGINNING) < 0 || Read(d->fh, d->cache, TRACKBYTES) != TRACKBYTES)
            e = IoErr() ? IoErr() : ERROR_SEEK_ERROR;
        for (s = 0; s < SECTORS; s++)
            secterr[s] = e;
        return e;
    }
    for (tries = 0; tries < 3; tries++) {
        if (tries)
            td(CMD_CLEAR, NULL, 0, 0);
        if (!(e = td(CMD_READ, d->cache, TRACKBYTES, (ULONG)t * TRACKBYTES)) || e == TDERR_DiskChanged)
            break;
    }
    if (!e)
        return 0;
    for (s = 0; s < SECTORS; s++) {
        if (e == TDERR_DiskChanged)
            secterr[s] = e;
        else {
            td(CMD_CLEAR, NULL, 0, 0);
            secterr[s] = td(CMD_READ, d->cache + s * BSIZE, BSIZE, ((ULONG)t * SECTORS + s) * BSIZE);
        }
        if (secterr[s])
            bad++;
    }
    return bad ? e : 0;
}

LONG disk_read(struct Disk *d, LONG block, UBYTE *buf)
{
    LONG t = block / SECTORS, e;
    if (block < 0 || block >= NBLOCKS)
        return ERROR_SEEK_ERROR;
    if (t != d->cached) {
        d->cachederr = readtrack(d, t);
        d->cached = t;
        if (d->cachederr)
            d->readerrs++;
    }
    if ((e = secterr[block % SECTORS])) {
        LONG k;
        for (k = 0; k < BSIZE; k++)
            buf[k] = 0;
        return e;
    }
    CopyMem(d->cache + (block % SECTORS) * BSIZE, buf, BSIZE);
    return 0;
}

/* how reading track t went: 0, or the error (for the surface scan) */
LONG disk_trackerr(struct Disk *d, LONG t)
{
    static ULONG b[BSIZE / 4];
    if (t != d->cached)
        disk_read(d, t * SECTORS, (UBYTE *)b);
    return d->cachederr;
}

void disk_close(struct Disk *d)
{
    if (d->fh)
        Close(d->fh);
    d->fh = 0;
    if (opened) {
        td(TD_MOTOR, NULL, 0, 0);
        CloseDevice((struct IORequest *)&io);
        opened = FALSE;
    }
    rmport();
    if (d->unit >= 0)
        inhibit(d, FALSE);
    if (d->cache)
        FreeMem(d->cache, TRACKBYTES);
    d->cache = NULL;
    d->cached = -1;
}
