/* Dowser - where the blocks come from: a floppy in DF0:-DF3: (through trackdisk.device) or
 * an .adf image file. Read-only by design: there is no way to write a block. */
#ifndef DISK_H
#define DISK_H
#include <exec/types.h>
#include <dos/dos.h>

#define BSIZE      512
#define NBLOCKS    1760          /* DD floppy: 80 cylinders x 2 heads x 11 sectors */
#define SECTORS    11
#define NTRACKS    160
#define TRACKBYTES (SECTORS * BSIZE)
#define ADFSIZE    (NBLOCKS * BSIZE)

struct Disk {
    int   unit;                  /* 0-3 for DFn:, -1 for an image file */
    BPTR  fh;                    /* the image file */
    char  name[256];             /* "DF1:" or the image's path */
    LONG  cached;                /* track in the cache, -1 none */
    LONG  cachederr;             /* how reading that track went (0 = fine) */
    UBYTE *cache;                /* one track (chip RAM for a drive) */
    BOOL  inhibited;
    LONG  readerrs;              /* tracks that could not be read */
};

/* "DF1:" (or "df1") or a path to an .adf: 0, or a DOS / trackdisk error with *why set */
LONG disk_open(struct Disk *d, const char *src, const char **why);
/* one block into buf (512 bytes): 0, or the trackdisk error for its track */
LONG disk_read(struct Disk *d, LONG block, UBYTE *buf);
void disk_close(struct Disk *d);
/* how reading track t went as a whole: 0, or the trackdisk error (for the surface scan) */
LONG disk_trackerr(struct Disk *d, LONG t);

/* "DF1:" -> 1, anything else -1 */
int  disk_unit(const char *src);
const char *disk_errstr(LONG err);

#endif
