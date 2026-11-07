/* Dowser - surface scan. Written by Paul Seibert, (c) 2026 Desert Palms Software. */
#include <exec/types.h>
#include <devices/trackdisk.h>
#include "util.h"
#include "disk.h"
#include "fs.h"
#include "scan.h"

const char *scan_trackname(int t)
{
    static char buf[24];
    fmt(buf, "%ld (cyl %ld, head %ld)", (LONG)t, (LONG)(t / 2), (LONG)(t & 1));
    return buf;
}

LONG scan_run(struct Disk *d, struct Surface *s, ScanProgress p, void *ctx)
{
    int t;
    s->done = s->nodos = s->damaged = 0;
    s->aborted = FALSE;
    s->valid = FALSE;
    for (t = 0; t < NTRACKS; t++) {
        LONG e;
        if (p && !p(ctx, t)) {
            s->aborted = TRUE;
            break;
        }
        if (t == 0)
            disk_forget(d);                          /* read every track afresh */
        d->cached = -1;
        e = disk_trackerr(d, t);
        if (e == TDERR_DiskChanged)
            return e;
        s->err[t] = e;
        s->track[t] = !e ? TRK_OK : (e == TDERR_NoSecHdr || e == TDERR_TooFewSecs || e == TDERR_BadSecPreamble ||
                      e == TDERR_BadSecID) ? TRK_NODOS : TRK_DAMAGED;
        if (s->track[t] == TRK_NODOS)
            s->nodos++;
        else if (s->track[t] == TRK_DAMAGED)
            s->damaged++;
        s->done++;
    }
    s->valid = !s->aborted;
    return 0;
}
