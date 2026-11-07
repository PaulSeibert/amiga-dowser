/* Dowser - the disk check: what is wrong with a disk, in plain English. */
#ifndef CHECK_H
#define CHECK_H
#include <exec/types.h>
#include "fs.h"
#include "boot.h"

enum { SEV_INFO, SEV_WARN, SEV_ERROR };

struct Finding {
    UBYTE code;                  /* F_... */
    UBYTE sev;                   /* SEV_... */
    UWORD block;
    UWORD obj;                   /* object index, or NOOBJ */
    UWORD n;                     /* a count, for the ones that sum up many blocks */
};

#define MAXFIND 200
struct Check {
    struct Finding f[MAXFIND];
    int  nf;
    LONG errors, warnings;       /* all of them, also those past MAXFIND */
    LONG files, fragfiles;       /* files, and how many are in more than one piece */
    int  maxfrags, maxfragobj;   /* the most pieces, and which file */
    struct BootInfo boot;
};

/* check the disk (the model must be read already); d is used to read the boot block,
   OFS data block lists and directory-cache blocks */
void check_run(struct FS *fs, struct Disk *d, struct Check *c);
/* one finding as a line of text (no newline) */
void check_line(struct FS *fs, const struct Finding *f, char *out, int size);
const char *check_code(int code);            /* the short name, e.g. "HDRCSUM" */

#endif
