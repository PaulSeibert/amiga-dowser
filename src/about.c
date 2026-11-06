/* About Oasis: a small modal window with the Desert Palms Software shield and credits.
 * Deliberately quiet: it uses the Workbench's own four colours and never touches the palette. */
#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <graphics/text.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include "about.h"

extern struct IntuitionBase *IntuitionBase;
extern const UWORD logo_W, logo_H, logo_WORDS, logo_13[], logo_20[];

#define AB_W   400
#define AB_H   120
#define OK_W   64
#define OK_H   14

static const char *const credits[] = {
    "Oasis 1.0",
    "Two-pane file manager",
    "for Kickstart 1.3 and later",
    "",
    "Written by Paul Seibert",
    "(c) 2026 Desert Palms Software",
    "Freeware - please share it!",
};
#define N_CREDITS (sizeof(credits) / sizeof(credits[0]))

static WORD okXY[] = { 0, 0, OK_W - 1, 0, OK_W - 1, OK_H - 1, 0, OK_H - 1, 0, 0 };

static void say(struct RastPort *rp, int x, int y, int pen, const char *s)
{
    int n = 0;
    while (s[n])
        n++;
    SetAPen(rp, pen);
    SetDrMd(rp, JAM1);
    Move(rp, x, y + rp->TxBaseline);
    Text(rp, (STRPTR)s, n);
}

void about_show(struct Window *parent, struct TextAttr *ta)
{
    /* 2.0 swapped the Workbench pens (0 grey, 1 black, 2 white, 3 blue): pick matching art,
       and the highlight pen: orange on 1.3, blue on 2.0+ (both are pen 3). */
    BOOL v36 = IntuitionBase->LibNode.lib_Version >= 36;
    int text = 1, accent = 3;
    ULONG bytes = (ULONG)logo_WORDS * sizeof(UWORD);
    UWORD *chip = AllocMem(bytes, MEMF_CHIP);   /* DrawImage blits: data must be in chip RAM */
    struct Screen *scr = parent->WScreen;
    struct IntuiText okText = { 1, 0, JAM1, (OK_W - 16) / 2, 3, ta, (UBYTE *)"OK", NULL };
    struct Border okBorder = { 0, 0, 1, 0, JAM1, 5, okXY, NULL };
    struct Gadget ok = { NULL, 0, 0, OK_W, OK_H, GADGHCOMP | GRELRIGHT | GRELBOTTOM, RELVERIFY,
                         BOOLGADGET, &okBorder, NULL, &okText, 0, NULL, 1, NULL };
    struct NewWindow nw = {
        0, 0, AB_W, AB_H, 0, 1, CLOSEWINDOW | GADGETUP | RAWKEY,
        WINDOWCLOSE | WINDOWDRAG | WINDOWDEPTH | SMART_REFRESH | NOCAREREFRESH | ACTIVATE,
        &ok, NULL, (UBYTE *)"About Oasis", NULL, NULL, 0, 0, 0, 0, WBENCHSCREEN
    };
    struct Image logo = { 0, 0, 0, 0, 2, NULL, 0x03, 0x00, NULL };
    struct Window *w;
    struct TextFont *font;
    BOOL done = FALSE;
    int lx, ly, tx, i;

    nw.LeftEdge = (scr->Width - AB_W) / 2;
    nw.TopEdge = parent->TopEdge + 16;
    if (nw.TopEdge + AB_H > scr->Height)
        nw.TopEdge = scr->Height > AB_H ? scr->Height - AB_H : 0;
    ok.LeftEdge = -(OK_W + 14);
    ok.TopEdge = -(OK_H + 6);
    if (!(w = OpenWindow(&nw))) {
        if (chip)
            FreeMem(chip, bytes);
        return;
    }
    if ((font = OpenFont(ta)))
        SetFont(w->RPort, font);

    lx = w->BorderLeft + 12;
    ly = w->BorderTop + 6;
    if (chip) {
        CopyMem((APTR)(v36 ? logo_20 : logo_13), chip, bytes);
        logo.Width = logo_W;
        logo.Height = logo_H;
        logo.ImageData = chip;
        DrawImage(w->RPort, &logo, lx, ly);
    }
    /* wordmark under the shield: 12 characters = the shield's 96 pixels */
    say(w->RPort, lx, ly + logo_H + 4, text, "DESERT PALMS");
    say(w->RPort, lx + 16, ly + logo_H + 14, accent, "SOFTWARE");

    tx = lx + logo_W + 20;
    for (i = 0; i < (int)N_CREDITS; i++)
        say(w->RPort, tx, ly + 4 + i * 11, i == 0 ? accent : text, credits[i]);
    SetAPen(w->RPort, text);
    Move(w->RPort, tx, ly + 4 + 9);
    Draw(w->RPort, tx + 9 * 8 - 1, ly + 4 + 9);     /* underline the title */

    while (!done) {
        struct IntuiMessage *im;
        WaitPort(w->UserPort);
        while ((im = (struct IntuiMessage *)GetMsg(w->UserPort))) {
            ULONG class = im->Class;
            UWORD code = im->Code;
            ReplyMsg((struct Message *)im);
            if (class == CLOSEWINDOW || class == GADGETUP)
                done = TRUE;
            else if (class == RAWKEY && (code == 0x44 || code == 0x43 || code == 0x45))
                done = TRUE;   /* Return, keypad Enter, Esc */
        }
    }
    CloseWindow(w);
    if (font)
        CloseFont(font);
    if (chip)
        FreeMem(chip, bytes);
}
