/* Dowser 1.0: a disk inspector and rescue tool for AmigaOS 1.3 (V33) and later.
 * Written by Paul Seibert. (c) 2026 Desert Palms Software. Freeware.
 *   dowser              open the window (also from Workbench)
 *   dowser CMD ...      command mode, without the window (see cmd.c) */
#include <exec/types.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include "util.h"
#include "cmd.h"

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;

static const char version[] __attribute__((used)) =
    "$VER: Dowser 1.0 (6.11.2026) (c) 2026 Desert Palms Software, by Paul Seibert. Freeware.";

int main(int argc, char **argv)
{
    int rc = 20;
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 33);
    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 33);
    if (!IntuitionBase || !GfxBase)
        goto out;
    if (argc > 1 && !nocase_cmp(argv[1], "CMD")) {
        rc = cmd_run(argc, argv);
        goto out;
    }
    {
        static const char usage[] =
            "Dowser 1.0 - (c) 2026 Desert Palms Software, by Paul Seibert. Freeware.\n"
            "Usage: dowser CMD ...   (the window arrives in a later test version)\n";
        Write(Output(), (APTR)usage, sizeof(usage) - 1);
        rc = 0;
    }
out:
    if (GfxBase)
        CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase)
        CloseLibrary((struct Library *)IntuitionBase);
    return rc;
}
