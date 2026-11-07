/* Dowser - the boot block (blocks 0 and 1): its type, checksum and boot code. */
#ifndef BOOT_H
#define BOOT_H
#include <exec/types.h>

enum { BOOT_NOCODE, BOOT_STD13, BOOT_STD20, BOOT_CUSTOM };

#define BOOT_MAXSTR 8
struct BootInfo {
    ULONG dostype;
    BOOL  sumok;                 /* the checksum adds up: Kickstart will run the code */
    BOOL  code;                  /* there is something in the code area */
    int   kind;                  /* BOOT_... */
    ULONG crc;                   /* CRC32 of the code area (bytes 12-1023), to look it up */
    int   nstr;
    char  str[BOOT_MAXSTR][48];  /* readable text found in the code area */
};

void boot_inspect(const UBYTE *b, struct BootInfo *bi);      /* b: the 1024 bytes */
ULONG boot_crc32(const UBYTE *p, LONG n);
const char *boot_kindname(int kind);

#endif
