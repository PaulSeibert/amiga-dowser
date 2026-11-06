/* Oasis - small string helpers (no stdio: keeps the program small). */
#ifndef UTIL_H
#define UTIL_H
#include <exec/types.h>

/* sprintf via exec RawDoFmt: every numeric argument must be a LONG (%ld, %lx, %lc). */
void fmt(char *buf, const char *f, ...);
int  slen(const char *s);
void scopy(char *d, const char *s, int size);         /* always NUL-terminates */
void scat(char *d, const char *s, int size);
int  nocase_cmp(const char *a, const char *b);
BOOL ends_with_nocase(const char *s, const char *tail);
void sizestr(char *d, LONG bytes);                      /* "12345", "1234K", "12M" */
void kbstr(char *d, ULONG kb);                          /* "880K", "120M", "2G" */

#endif
