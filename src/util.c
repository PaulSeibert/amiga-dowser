/* Oasis - small string helpers. Written by Paul Seibert, (c) 2026 Desert Palms Software. */
#include <proto/exec.h>
#include <stdarg.h>
#include "util.h"

/* RawDoFmt put-char routine: move.b d0,(a3)+ ; rts */
static const UWORD putch[] = { 0x16C0, 0x4E75 };

void fmt(char *buf, const char *f, ...)
{
    va_list ap;
    va_start(ap, f);
    RawDoFmt((STRPTR)f, (APTR)ap, (void (*)())putch, buf);
    va_end(ap);
}

int slen(const char *s)
{
    int n = 0;
    while (s[n])
        n++;
    return n;
}

void scopy(char *d, const char *s, int size)
{
    int i;
    for (i = 0; i < size - 1 && s[i]; i++)
        d[i] = s[i];
    d[i] = 0;
}

void scat(char *d, const char *s, int size)
{
    int n = slen(d);
    if (n < size)
        scopy(d + n, s, size - n);
}

static int lower(int c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7) ? c + 32 : c;
}

int nocase_cmp(const char *a, const char *b)
{
    for (;; a++, b++) {
        int ca = lower((UBYTE)*a), cb = lower((UBYTE)*b);
        if (ca != cb || !ca)
            return ca - cb;
    }
}

BOOL ends_with_nocase(const char *s, const char *tail)
{
    int n = slen(s), m = slen(tail);
    return n >= m && !nocase_cmp(s + n - m, tail);
}

void kbstr(char *d, ULONG kb)
{
    if (kb < 10000)
        fmt(d, "%ldK", kb);
    else if (kb < 10000UL * 1024)
        fmt(d, "%ldM", kb >> 10);
    else
        fmt(d, "%ldG", kb >> 20);
}

void sizestr(char *d, LONG bytes)
{
    if (bytes < 10000000)
        fmt(d, "%ld", bytes);
    else if (bytes < 100000L * 1024)
        fmt(d, "%ldK", bytes >> 10);
    else
        fmt(d, "%ldM", bytes >> 20);
}
