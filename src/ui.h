/* Dowser - the window. */
#ifndef UI_H
#define UI_H
#include <exec/types.h>

enum { TAB_CHECK, TAB_FILES, TAB_DELETED, TAB_BOOT, TAB_SURFACE, NTABS };

/* open the window; source (DFn: or an .adf) is read at once if given, tab is shown, block
   (>= 0) opens the block viewer, file (path on the disk) is selected, about opens About */
BOOL  ui_open(const char *source, int tab, LONG block, const char *file, BOOL about);
BOOL  ui_handle(void);           /* FALSE: quit */
void  ui_close(void);
ULONG ui_sigmask(void);

#endif
