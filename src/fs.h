/* Dowser - the disk model: what every block is, who owns it, and the tree of files.
 * Read from a Disk in one sequential pass (a summary of every block) plus a walk of the
 * directory tree. OFS and FFS, with the international and directory-cache variants. */
#ifndef FS_H
#define FS_H
#include <exec/types.h>
#include <dos/dos.h>
#include "disk.h"

/* block types (on-disk) */
#define T_HEADER 2
#define T_DATA   8
#define T_LIST   16
#define T_DIRC   33
#define FS_STOPPED 304          /* the scan was stopped (ERROR_BREAK is V36) */
#define SEC_ROOT  1
#define SEC_DIR   2
#define SEC_SOFT  3
#define SEC_LDIR  4
#define SEC_FILE  (-3)
#define SEC_LFILE (-4)

/* what a block is, in the map */
enum { BK_UNSEEN, BK_FREE, BK_BOOT, BK_ROOT, BK_BITMAP, BK_BMEXT, BK_DIR, BK_FILE, BK_EXT,
       BK_DATA, BK_DIRC, BK_LINK, BK_LOST, BK_UNREAD, BK_COUNT };

/* flags per block */
#define BF_USED     0x01         /* the bitmap says it is in use */
#define BF_CSUM     0x02         /* its checksum is wrong */
#define BF_TWICE    0x04         /* claimed by two owners */
#define BF_READERR  0x08         /* could not be read */
#define BF_UNMARKED 0x10         /* in use, but the bitmap says free */
#define BF_SEL      0x80         /* (the window's highlight) */

/* a summary of each block from the sequential pass */
struct BSum {
    UBYTE type;                  /* T_... (low byte of the type long; 0 for anything else) */
    BYTE  sectype;               /* SEC_... for headers and lists */
    UBYTE csumok;                /* the standard header checksum adds up */
    UBYTE pad;
    UWORD key;                   /* header_key (data blocks: their file header) */
    UWORD seq;                   /* OFS data: sequence number */
    UWORD size;                  /* OFS data: bytes used */
    UWORD parent;                /* headers: parent block */
};

enum { OK_DIR, OK_FILE, OK_SOFTLINK, OK_HARDLINK };

/* problems: found while walking (fs.c) or by the check (check.c) */
enum { F_NOTDOS = 1, F_BOOTSUM, F_ROOT, F_ROOTCSUM, F_BMINVALID, F_BMCSUM, F_BMBAD, F_HDRCSUM,
       F_DATACSUM, F_TWICE, F_UNMARKED, F_LOST, F_CHAINRANGE, F_CHAINTYPE, F_CHAINLOOP, F_PARENT,
       F_HASHSLOT, F_EXTBAD, F_DATATYPE, F_DATAOWNER, F_DATASEQ, F_DATASIZE, F_SIZE, F_UNREAD,
       F_UNREADFREE, F_DIRCACHE, F_BOOTDOS, F_COUNT };
#define NOOBJ 0xFFFF
struct Prob { UBYTE code; UBYTE pad; UWORD block; UWORD obj; };
#define MAXPROB 64

/* how a track read */
enum { TRK_OK, TRK_NODOS, TRK_DAMAGED };

/* object status bits */
#define OS_UNREADABLE 1          /* some of its blocks can't be read */
#define OS_DAMAGED    2          /* something about it doesn't add up (see the check) */

struct Obj {
    char  name[31];
    UBYTE kind;                  /* OK_... */
    UWORD parent;                /* index of the parent object (the root is object 0) */
    UWORD header;                /* its header block */
    UWORD nblocks;               /* data blocks it has */
    UWORD frags;                 /* runs of consecutive data blocks (1 = in one piece) */
    UBYTE status;
    UBYTE pad;
    LONG  size;
    LONG  prot;
    struct DateStamp date;
};

struct FS {
    ULONG dostype;               /* "DOS\0".."DOS\5", or something else */
    BOOL  dos, ffs, intl, dirc;
    LONG  root;
    char  volname[31];
    struct DateStamp created, altered;
    LONG  bmflag;                /* -1: the bitmap is valid */
    LONG  bmblocks[26];          /* the bitmap blocks (0-terminated) */
    LONG  usedbm, usedreal;      /* used blocks: per the bitmap / actually in use */
    LONG  readerrs;              /* blocks that could not be read */
    UBYTE kind[NBLOCKS];
    UBYTE flags[NBLOCKS];
    UWORD owner[NBLOCKS];        /* object index + 1, 0 = none */
    struct BSum sum[NBLOCKS];
    struct Obj *obj;
    int   nobj, objcap;
    BOOL  aborted;
    BOOL  rootok;                /* the root block was readable and looked like one */
    BOOL  bootbad;               /* the boot block doesn't say DOS, but the root block is fine */
    UBYTE track[NTRACKS];        /* how each track read: TRK_... */
    struct Prob prob[MAXPROB];   /* what the walk ran into */
    int   nprob;
};

/* called every track during the reading pass; FALSE = stop */
typedef BOOL (*FsProgress)(void *ctx, LONG done, LONG total);

/* read the disk and build the model. 0, or a DOS / trackdisk error with *why set.
   A disk that isn't AmigaDOS is not an error: fs->dos is FALSE and only the map is filled. */
LONG fs_scan(struct FS *fs, struct Disk *d, FsProgress p, void *ctx, const char **why);
void fs_free(struct FS *fs);

/* "Work/sub/name" for object i (the root is "") */
void fs_path(struct FS *fs, int i, char *out, int size);
/* the data blocks of file i in order (up to max): how many */
int  fs_datablocks(struct FS *fs, struct Disk *d, int i, UWORD *out, int max);

ULONG fs_long(const UBYTE *b, int byteoff);
BOOL  fs_csumok(const UBYTE *b, int at);        /* the 128 longs add up to 0 */
int   fs_hash(const char *name, BOOL intl);      /* hash table slot of a name */
const char *fs_kindname(int bk);
char  fs_kindchar(struct FS *fs, LONG b);        /* the CMD MAP character for block b */

#endif
