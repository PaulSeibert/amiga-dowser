/* Oasis - AmigaDOS helpers that work on Kickstart 1.3 (V33): device list, paths, dates,
 * error texts, free space, and switching DOS requesters off while probing drives. */
#ifndef DOS13_H
#define DOS13_H
#include <exec/types.h>
#include <dos/dos.h>

#define PATHMAX 256
#define DEVNAME 32

enum { DK_DEVICE, DK_VOLUME, DK_ASSIGN };

struct DevEntry {
    char  name[DEVNAME];     /* without the colon */
    UBYTE kind;              /* DK_... */
};

int  dos_devices(struct DevEntry *out, int max);   /* filesystem devices, volumes, assigns */
void dos_quiet(BOOL on);                            /* TRUE: no "insert disk" requesters */
void dos_setwindow(APTR win);                       /* where DOS requesters appear (restored at exit) */
const char *dos_errstr(LONG err);
void dos_datestr(char *buf, const struct DateStamp *ds);  /* "03-Nov-26 01:10" */
void dos_protstr(char *buf, LONG prot);                   /* "hsparwed", '-' when unset */
BOOL dos_space(BPTR lock, const char *path, ULONG *freek, char *volname);   /* free KB */

/* path editing in place (PATHMAX buffers) */
void path_add(char *path, const char *name);
BOOL path_parent(char *path);       /* FALSE when already at the root */
void path_root(char *path);
BOOL path_isroot(const char *path);

/* send a DOS packet to a handler and wait for the reply: returns dp_Res1, *res2 = dp_Res2 */
LONG dos_packet(struct MsgPort *port, LONG type, LONG a1, LONG a2, LONG a3, LONG a4, LONG *res2);
/* set a file's date with ACTION_SET_DATE (1.3 has no SetFileDate): 0 or a DOS error */
LONG dos_setdate(const char *path, const struct DateStamp *ds);

#endif
