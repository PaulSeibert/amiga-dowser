/* Dowser 1.0: a disk inspector and rescue tool for AmigaOS 1.3 (V33) and later.
 * Written by Paul Seibert. (c) 2026 Desert Palms Software. Freeware.
 *   dowser [source] [CHECK|FILES|DELETED|BOOT|SURFACE] [BLOCK=n] [FILE=path]
 *                       open the window (also from Workbench); source is DF0:-DF3: or an .adf
 *   dowser CMD ...      command mode, without the window (see cmd.c) */
#include <exec/types.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include "util.h"
#include "cmd.h"
#include "ui.h"

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;

static const char version[] __attribute__((used)) =
    "$VER: Dowser 1.0 (6.11.2026) (c) 2026 Desert Palms Software, by Paul Seibert. Freeware.";

static BOOL prefix(const char *s, const char *p)
{
    for (; *p; s++, p++)
        if ((*s | 0x20) != (*p | 0x20))
            return FALSE;
    return TRUE;
}

int main(int argc, char **argv)
{
    int rc = 20, i, tab = -1;
    LONG block = -1;
    const char *src = NULL, *file = NULL;
    BOOL running = TRUE, about = FALSE;
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 33);
    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 33);
    if (!IntuitionBase || !GfxBase)
        goto out;
    if (argc > 1 && !nocase_cmp(argv[1], "CMD")) {
        rc = cmd_run(argc, argv);
        goto out;
    }
    if (argc > 1 && (argv[1][0] == '?' || !nocase_cmp(argv[1], "HELP"))) {
        static const char usage[] =
            "Dowser 1.0 - (c) 2026 Desert Palms Software, by Paul Seibert. Freeware.\n"
            "Usage: dowser [DF0:-DF3: | disk.adf] [CHECK|FILES|DELETED|BOOT|SURFACE] [BLOCK=n] [FILE=path]\n"
            "       dowser CMD ...      (dowser CMD alone lists the commands)\n";
        Write(Output(), (APTR)usage, sizeof(usage) - 1);
        rc = 0;
        goto out;
    }
    for (i = 1; i < argc; i++) {
        if (!nocase_cmp(argv[i], "CHECK"))
            tab = TAB_CHECK;
        else if (!nocase_cmp(argv[i], "FILES"))
            tab = TAB_FILES;
        else if (!nocase_cmp(argv[i], "BOOT"))
            tab = TAB_BOOT;
        else if (!nocase_cmp(argv[i], "DELETED"))
            tab = TAB_DELETED;
        else if (!nocase_cmp(argv[i], "SURFACE"))
            tab = TAB_SURFACE;
        else if (prefix(argv[i], "BLOCK=")) {
            const char *c;
            block = 0;
            for (c = argv[i] + 6; *c >= '0' && *c <= '9'; c++)
                block = block * 10 + *c - '0';
        } else if (prefix(argv[i], "FILE="))
            file = argv[i] + 5;
        else if (!nocase_cmp(argv[i], "ABOUT"))
            about = TRUE;
        else
            src = argv[i];
    }
    if (!ui_open(src, tab, block, file, about))
        goto out;
    while (running) {
        ULONG sigs = Wait(ui_sigmask() | SIGBREAKF_CTRL_C);
        if (sigs & ui_sigmask())
            running = ui_handle();
        if (sigs & SIGBREAKF_CTRL_C)
            running = FALSE;
    }
    rc = 0;
out:
    ui_close();
    if (GfxBase)
        CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase)
        CloseLibrary((struct Library *)IntuitionBase);
    return rc;
}
