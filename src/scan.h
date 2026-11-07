/* Dowser - the surface scan: every track of a floppy read afresh, and how it went. */
#ifndef SCAN_H
#define SCAN_H
#include <exec/types.h>
#include "disk.h"

struct Surface {
    UBYTE track[NTRACKS];        /* TRK_OK, TRK_NODOS, TRK_DAMAGED (fs.h) */
    LONG  err[NTRACKS];          /* the trackdisk error */
    int   done, nodos, damaged;
    BOOL  aborted, valid;
};

typedef BOOL (*ScanProgress)(void *ctx, int track);
/* read every track again (not from trackdisk's buffer): 0, or why it couldn't start */
LONG scan_run(struct Disk *d, struct Surface *s, ScanProgress p, void *ctx);
const char *scan_trackname(int t);

#endif
