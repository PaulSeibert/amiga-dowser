/* Dowser - one block as text: what it is, its fields decoded for its type, and a hex dump.
 * Used by the block viewer window and by "dowser CMD BLOCK". */
#ifndef BLOCKVIEW_H
#define BLOCKVIEW_H
#include <exec/types.h>
#include "fs.h"

/* the text for block b (lines ending in \n, NUL-terminated): its length */
LONG bv_text(struct FS *fs, struct Disk *d, LONG b, char *out, LONG size);

#endif
