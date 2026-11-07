/* Dowser - getting files back: deleted files (and files whose drawer is lost) found from
 * the headers still on the disk, and copying files off a disk, around any damage. Dowser
 * never writes to the disk it is reading: everything is copied to another drawer. */
#ifndef RECOVER_H
#define RECOVER_H
#include <exec/types.h>
#include <dos/dos.h>
#include "fs.h"

#define MAXFOUND 200

/* status of a found file */
enum { FD_WHOLE, FD_PART, FD_GONE };

struct Found {
    char  name[31];
    UBYTE dir;                   /* a drawer header (its files are found on their own) */
    UBYTE status;                /* FD_... */
    UBYTE orphan;                /* still marked in use: not deleted, its drawer is lost */
    UWORD header, parent;        /* blocks */
    UWORD nblocks, lost;         /* data blocks, and how many of them are now someone else's */
    LONG  size, prot;
    struct DateStamp date;
};

struct Recover {
    struct Found f[MAXFOUND];
    int n;
};

/* find the file and drawer headers that no drawer leads to (needs the disk open) */
void rec_scan(struct FS *fs, struct Disk *d, struct Recover *r);
/* where found file i was: "Work/sub" ("" = the top drawer, "?" = unknown) */
void rec_where(struct FS *fs, struct Recover *r, int i, char *out, int size);
const char *rec_status(const struct Found *f);

struct RescueStats {
    LONG  files, dirs, bytes, badblocks;
    BOOL  aborted;
    LONG  err;                   /* the first error writing, 0 if none */
    char  errpath[128];
};
typedef BOOL (*RecProgress)(void *ctx, const char *name);

/* copy object obj (a file, or a drawer with everything in it; 0 = the whole disk, its
   contents straight into dest) into drawer dest */
void rec_rescue(struct FS *fs, struct Disk *d, int obj, const char *dest, struct RescueStats *st,
                RecProgress p, void *ctx);
/* copy found file i into dest; with keeppath, into the drawers it was in (made under dest) */
void rec_rescue_found(struct FS *fs, struct Disk *d, struct Recover *r, int i, const char *dest,
                      BOOL keeppath, struct RescueStats *st, RecProgress p, void *ctx);
/* is drawer dest on the disk in drive source (DFn:)? Rescuing onto it could overwrite what is
   being rescued. Ask before opening the drive: while Dowser has it, DOS can't answer. */
BOOL rec_samedisk(const char *source, const char *dest);

#endif
