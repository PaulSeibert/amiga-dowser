/* Oasis - modal dialogs: choose a button, or enter a line of text.
 * Written by Paul Seibert, (c) 2026 Desert Palms Software. */
#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <graphics/text.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include "util.h"
#include "req.h"

#define MAXLINES 6
#define MAXBTN   5
#define LINECH   60
#define BH       14
#define BGAP     8

struct Dlg {
    struct Gadget g[MAXBTN];
    struct IntuiText t[MAXBTN];
    struct Border b[MAXBTN];
    WORD xy[MAXBTN][10];
    char label[MAXBTN][16];
    int n;
};

static int split(const char *s, char sep, char out[][LINECH + 1], int max, int width)
{
    int n = 0, k = 0;
    if (!*s)
        return 0;
    for (;; s++) {
        if (*s == sep || !*s) {
            out[n][k] = 0;
            if (++n == max || !*s)
                break;
            k = 0;
        } else if (k < width)
            out[n][k++] = *s;
    }
    return n;
}

/* buttons along the bottom edge, centred, GRELBOTTOM so they follow the window */
static int mkbuttons(struct Dlg *d, const char *buttons, struct TextAttr *ta, int winw, int firstid)
{
    char tmp[MAXBTN][LINECH + 1];
    int i, total = 0, x, w[MAXBTN];
    d->n = split(buttons, '|', tmp, MAXBTN, 15);
    for (i = 0; i < d->n; i++) {
        scopy(d->label[i], tmp[i], 16);
        w[i] = slen(d->label[i]) * 8 + 16;
        if (w[i] < 64)
            w[i] = 64;
        total += w[i] + (i ? BGAP : 0);
    }
    x = (winw - total) / 2;
    for (i = 0; i < d->n; i++) {
        struct IntuiText t = { 1, 0, JAM1, (w[i] - slen(d->label[i]) * 8) / 2, 3, ta,
                               (UBYTE *)d->label[i], NULL };
        struct Border b = { 0, 0, 1, 0, JAM1, 5, d->xy[i], NULL };
        struct Gadget g = { i + 1 < d->n ? &d->g[i + 1] : NULL, x, -(BH + 5), w[i], BH,
                            GADGHCOMP | GRELBOTTOM, RELVERIFY, BOOLGADGET, &d->b[i], NULL, &d->t[i],
                            0, NULL, firstid + i, NULL };
        WORD *p = d->xy[i];
        p[0] = 0; p[1] = 0; p[2] = w[i] - 1; p[3] = 0; p[4] = w[i] - 1; p[5] = BH - 1;
        p[6] = 0; p[7] = BH - 1; p[8] = 0; p[9] = 0;
        d->t[i] = t;
        d->b[i] = b;
        d->g[i] = g;
        x += w[i] + BGAP;
    }
    return total;
}

static void say(struct RastPort *rp, int x, int y, const char *s)
{
    SetAPen(rp, 1);
    SetDrMd(rp, JAM1);
    Move(rp, x, y + rp->TxBaseline);
    Text(rp, (STRPTR)s, slen(s));
}

static struct Window *openat(struct Window *parent, int w, int h, const char *title,
                             struct Gadget *gads, ULONG idcmp)
{
    struct Screen *scr = parent->WScreen;
    struct NewWindow nw = {
        0, 0, 0, 0, 0, 1, idcmp,
        WINDOWDRAG | WINDOWDEPTH | SMART_REFRESH | NOCAREREFRESH | ACTIVATE | RMBTRAP,
        gads, NULL, (UBYTE *)title, NULL, NULL, 0, 0, 0, 0, WBENCHSCREEN
    };
    if (w > scr->Width)
        w = scr->Width;
    nw.Width = w;
    nw.Height = h;
    nw.LeftEdge = parent->LeftEdge + (parent->Width - w) / 2;
    if (nw.LeftEdge < 0 || nw.LeftEdge + w > scr->Width)
        nw.LeftEdge = (scr->Width - w) / 2;
    nw.TopEdge = parent->TopEdge + 30;
    if (nw.TopEdge + h > scr->Height)
        nw.TopEdge = scr->Height > h ? scr->Height - h : 0;
    return OpenWindow(&nw);
}

static int lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

int req_choose(struct Window *parent, struct TextAttr *ta, const char *title,
               const char *body, const char *buttons)
{
    static struct Dlg d;                 /* static: keeps 1.3's small stacks safe */
    static char line[MAXLINES][LINECH + 1];
    struct Window *w;
    struct TextFont *font;
    struct IntuiMessage *im;
    int n = split(body, '\n', line, MAXLINES, LINECH), i, width = 0, bw, choice = -1;

    for (i = 0; i < n; i++)
        if (slen(line[i]) > width)
            width = slen(line[i]);
    width = width * 8 + 32;
    bw = mkbuttons(&d, buttons, ta, 0, 1) + 32;
    if (bw > width)
        width = bw;
    if (width < 280)
        width = 280;
    mkbuttons(&d, buttons, ta, width, 1);   /* again, now centred in the real width */
    if (!(w = openat(parent, width, 12 + 8 + n * 10 + 10 + BH + 8, title, &d.g[0],
                     GADGETUP | RAWKEY | VANILLAKEY)))
        return d.n - 1;                      /* no memory for a window: take the safe way out */
    if ((font = OpenFont(ta)))
        SetFont(w->RPort, font);
    for (i = 0; i < n; i++)
        say(w->RPort, w->BorderLeft + 12, w->BorderTop + 6 + i * 10, line[i]);
    while (choice < 0) {
        WaitPort(w->UserPort);
        while ((im = (struct IntuiMessage *)GetMsg(w->UserPort))) {
            ULONG class = im->Class;
            UWORD code = im->Code;
            struct Gadget *g = (struct Gadget *)im->IAddress;
            ReplyMsg((struct Message *)im);
            if (choice >= 0)
                continue;
            if (class == GADGETUP)
                choice = g->GadgetID - 1;
            else if (class == VANILLAKEY) {
                if (code == 13)
                    choice = 0;
                else if (code == 27)
                    choice = d.n - 1;
                else
                    for (i = 0; i < d.n; i++)
                        if (lower(code) == lower((UBYTE)d.label[i][0])) {
                            choice = i;
                            break;
                        }
            }
        }
    }
    CloseWindow(w);
    if (font)
        CloseFont(font);
    return choice;
}

BOOL req_string(struct Window *parent, struct TextAttr *ta, const char *title,
                const char *prompt, char *buf, int size)
{
    static struct Dlg d;
    static UBYTE text[256], undo[256];
    static struct StringInfo si;
    static struct Gadget sg;
    static WORD frame[10];
    static struct Border fb;
    struct StringInfo zsi = { 0 };
    struct Gadget zg = { 0 };
    struct Border zb = { -3, -3, 1, 0, JAM1, 5, frame, NULL };
    struct Window *w;
    struct TextFont *font;
    struct IntuiMessage *im;
    int width = size > 40 ? 480 : 360, gw, result = -1;

    if (size > (int)sizeof(text))
        size = sizeof(text);
    scopy((char *)text, buf, size);
    if (slen(prompt) * 8 + 32 > width)
        width = slen(prompt) * 8 + 32;
    mkbuttons(&d, "OK|Cancel", ta, width, 1);
    gw = width - 40;
    frame[0] = 0; frame[1] = 0; frame[2] = gw + 5; frame[3] = 0; frame[4] = gw + 5;
    frame[5] = 13; frame[6] = 0; frame[7] = 13; frame[8] = 0; frame[9] = 0;
    fb = zb;
    si = zsi;
    si.Buffer = text;
    si.UndoBuffer = undo;
    si.MaxChars = size;
    si.BufferPos = slen((char *)text);
    sg = zg;
    sg.NextGadget = &d.g[0];
    sg.LeftEdge = 20;
    sg.TopEdge = 12 + 6 + 14;
    sg.Width = gw;
    sg.Height = 8;
    sg.Flags = GADGHCOMP;
    sg.Activation = RELVERIFY;
    sg.GadgetType = STRGADGET;
    sg.GadgetRender = &fb;
    sg.SpecialInfo = &si;
    sg.GadgetID = 100;
    if (!(w = openat(parent, width, 12 + 6 + 14 + 8 + 12 + BH + 8, title, &sg,
                     GADGETUP | VANILLAKEY)))
        return FALSE;
    if ((font = OpenFont(ta)))
        SetFont(w->RPort, font);
    say(w->RPort, 20, w->BorderTop + 5, prompt);
    ActivateGadget(&sg, w, NULL);
    while (result < 0) {
        WaitPort(w->UserPort);
        while ((im = (struct IntuiMessage *)GetMsg(w->UserPort))) {
            ULONG class = im->Class;
            UWORD code = im->Code;
            struct Gadget *g = (struct Gadget *)im->IAddress;
            ReplyMsg((struct Message *)im);
            if (result >= 0)
                continue;
            if (class == GADGETUP)
                result = (g->GadgetID == 100 || g->GadgetID == 1) ? 1 : 0;
            else if (class == VANILLAKEY) {
                if (code == 27)
                    result = 0;
                else if (code == 13)
                    result = 1;
                else
                    ActivateGadget(&sg, w, NULL);   /* typing goes to the field */
            }
        }
    }
    CloseWindow(w);
    if (font)
        CloseFont(font);
    if (result == 1)
        scopy(buf, (char *)text, size);
    return result == 1;
}

/* ---- protection bits ------------------------------------------------------------------- */

#define NPROT 8
static const char *const protname[NPROT] = { "Hidden", "Script", "Pure", "Archived",
                                             "Read", "Write", "Execute", "Delete" };

static void drawtoggle(struct RastPort *rp, struct Gadget *g, BOOL on)
{
    const char *l = protname[g->GadgetID - 50];
    int x = g->LeftEdge, y = g->TopEdge, n = slen(l);
    SetAPen(rp, on ? 3 : 0);
    RectFill(rp, x + 1, y + 1, x + g->Width - 2, y + g->Height - 2);
    SetAPen(rp, 1);
    Move(rp, x, y);
    Draw(rp, x + g->Width - 1, y);
    Draw(rp, x + g->Width - 1, y + g->Height - 1);
    Draw(rp, x, y + g->Height - 1);
    Draw(rp, x, y);
    SetAPen(rp, on ? 2 : 1);
    SetDrMd(rp, JAM1);
    Move(rp, x + (g->Width - n * 8) / 2, y + 3 + rp->TxBaseline);
    Text(rp, (STRPTR)l, n);
}

BOOL req_protect(struct Window *parent, struct TextAttr *ta, const char *title,
                 const char *prompt, BOOL on[8])
{
    static struct Dlg d;
    static struct Gadget tg[NPROT];
    static BOOL state[NPROT];
    struct Gadget zg = { 0 };
    struct Window *w;
    struct TextFont *font;
    struct IntuiMessage *im;
    int tw = 76, gap = 4, width = NPROT / 2 * tw + (NPROT / 2 - 1) * gap + 40, i, result = -1;

    if (slen(prompt) * 8 + 40 > width)
        width = slen(prompt) * 8 + 40;
    mkbuttons(&d, "OK|Cancel", ta, width, 1);
    for (i = 0; i < NPROT; i++) {
        struct Gadget *g = &tg[i];
        *g = zg;
        g->NextGadget = i + 1 < NPROT ? &tg[i + 1] : &d.g[0];
        g->LeftEdge = (width - (NPROT / 2 * tw + (NPROT / 2 - 1) * gap)) / 2 + (i % 4) * (tw + gap);
        g->TopEdge = 12 + 6 + 14 + (i / 4) * (BH + gap);
        g->Width = tw;
        g->Height = BH;
        g->Flags = GADGHNONE;
        g->Activation = RELVERIFY;
        g->GadgetType = BOOLGADGET;
        g->GadgetID = 50 + i;
        state[i] = on[i];
    }
    if (!(w = openat(parent, width, 12 + 6 + 14 + 2 * BH + gap + 10 + BH + 8, title, &tg[0],
                     GADGETUP | VANILLAKEY)))
        return FALSE;
    if ((font = OpenFont(ta)))
        SetFont(w->RPort, font);
    say(w->RPort, 20, w->BorderTop + 5, prompt);
    for (i = 0; i < NPROT; i++)
        drawtoggle(w->RPort, &tg[i], state[i]);
    while (result < 0) {
        WaitPort(w->UserPort);
        while ((im = (struct IntuiMessage *)GetMsg(w->UserPort))) {
            ULONG class = im->Class;
            UWORD code = im->Code;
            struct Gadget *g = (struct Gadget *)im->IAddress;
            int t = -1;
            ReplyMsg((struct Message *)im);
            if (result >= 0)
                continue;
            if (class == GADGETUP) {
                if (g->GadgetID >= 50)
                    t = g->GadgetID - 50;
                else
                    result = g->GadgetID == 1;
            } else if (class == VANILLAKEY) {
                if (code == 13)
                    result = 1;
                else if (code == 27)
                    result = 0;
                else                                  /* the letters h s p a r w e d */
                    for (i = 0; i < NPROT; i++)
                        if (lower(code) == lower((UBYTE)protname[i][0]))
                            t = i;
            }
            if (t >= 0) {
                state[t] = !state[t];
                drawtoggle(w->RPort, &tg[t], state[t]);
            }
        }
    }
    CloseWindow(w);
    if (font)
        CloseFont(font);
    if (result == 1)
        for (i = 0; i < NPROT; i++)
            on[i] = state[i];
    return result == 1;
}
