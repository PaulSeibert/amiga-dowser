/* Dowser - boot blocks. Written by Paul Seibert, (c) 2026 Desert Palms Software.
 *
 * Kickstart runs the code in blocks 0-1 only if the checksum adds up. Dowser tells standard
 * install boot code (recognised by the CRC32 of the code area) from custom code, and lists the
 * text in custom code. It does not name viruses: without a maintained signature list that
 * would be guesswork. A custom boot block can be a game loader, a utility or a virus. */
#include <exec/types.h>
#include "util.h"
#include "boot.h"

/* CRC32 of bytes 12-1023 of the standard install boot blocks */
#define CRC_STD13 0x2C9F6D94UL   /* Kickstart 1.x Install (opens dos.library) */
#define CRC_STD20 0x9942406EUL   /* Kickstart 2.0+ Install (also opens expansion.library) */

ULONG boot_crc32(const UBYTE *p, LONG n)
{
    ULONG c = 0xFFFFFFFFUL;
    int k;
    while (n-- > 0) {
        c ^= *p++;
        for (k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320UL & (0 - (c & 1)));
    }
    return ~c;
}

const char *boot_kindname(int kind)
{
    switch (kind) {
    case BOOT_NOCODE: return "no boot code";
    case BOOT_STD13:  return "standard boot code (Kickstart 1.x Install)";
    case BOOT_STD20:  return "standard boot code (Kickstart 2.0+ Install)";
    default:          return "custom boot code";
    }
}

void boot_inspect(const UBYTE *b, struct BootInfo *bi)
{
    ULONG sum = 0, prev;
    int i, run = 0;
    bi->dostype = ((ULONG)b[0] << 24) | ((ULONG)b[1] << 16) | ((ULONG)b[2] << 8) | b[3];
    /* the boot checksum: add the 256 longs (with the checksum field as 0), wrapping the carry
       around; the stored checksum is the complement */
    for (i = 0; i < 256; i++) {
        ULONG v = i == 1 ? 0 : ((ULONG)b[i * 4] << 24) | ((ULONG)b[i * 4 + 1] << 16) |
                                ((ULONG)b[i * 4 + 2] << 8) | b[i * 4 + 3];
        prev = sum;
        sum += v;
        if (sum < prev)
            sum++;
    }
    bi->sumok = (~sum) == (((ULONG)b[4] << 24) | ((ULONG)b[5] << 16) | ((ULONG)b[6] << 8) | b[7]);
    bi->code = FALSE;
    for (i = 12; i < 1024; i++)
        if (b[i]) {
            bi->code = TRUE;
            break;
        }
    bi->crc = boot_crc32(b + 12, 1012);
    bi->kind = !bi->code ? BOOT_NOCODE : bi->crc == CRC_STD13 ? BOOT_STD13 : bi->crc == CRC_STD20 ? BOOT_STD20
             : BOOT_CUSTOM;
    /* readable text: runs of at least 5 printable characters */
    bi->nstr = 0;
    for (i = 12; i <= 1024 && bi->nstr < BOOT_MAXSTR; i++) {
        UBYTE c = i < 1024 ? b[i] : 0;
        if (c >= 32 && c < 127)
            run++;
        else {
            if (run >= 5) {
                int n = run < 47 ? run : 47, k;
                for (k = 0; k < n; k++)
                    bi->str[bi->nstr][k] = b[i - run + k];
                bi->str[bi->nstr][n] = 0;
                bi->nstr++;
            }
            run = 0;
        }
    }
}
