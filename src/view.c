/* Dowser (shared with Oasis) - text and hex viewer. Written by Paul Seibert, (c) 2026 Desert Palms Software.
 *
 * Text: the file (up to half the largest free block) is read into memory, and an index of
 * where each screen line starts is built for the window's width (long lines wrap, tabs
 * expand to 8, ANSI colour codes are skipped). Hex: 16 bytes a row, read from memory, or
 * from the file itself past the part that was loaded, so files of any size can be looked at. */
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <graphics/gfxmacros.h>
#include <devices/inputevent.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/dos.h>
#include "util.h"
#include "dos13.h"
#include "view.h"

#define PROP_W 16
#define LINE_H 8
#define MAXCOLS 160

static struct {
    UBYTE *buf;
    LONG len, size, alloc;       /* loaded bytes, file size, allocation */
    BPTR fh;                     /* kept open for hex beyond the loaded part */
    ULONG *line;                 /* text: start of each screen line */
    LONG nlines, lcap;
    int cols, rows;
    LONG top;
    BOOL hex;
    struct Window *w;
    struct RastPort *rp;
    struct Gadget prop;
    struct PropInfo pi;
    struct Image knob;
    char title[120];
    const char *name;
} V;

/* ---- text layout ------------------------------------------------------------------------- */

/* length of an escape sequence at p (ESC [ ... final, or CSI ... final), 0 if none */
static LONG escape(const UBYTE *p, const UBYTE *end)
{
    const UBYTE *q = p;
    if (*q == 0x9B)
        q++;
    else if (*q == 27 && q + 1 < end && q[1] == '[')
        q += 2;
    else
        return 0;
    while (q < end && *q >= 0x20 && *q < 0x40)        /* parameters */
        q++;
    if (q < end && *q >= 0x40 && *q < 0x7F)            /* the final byte */
        q++;
    return q - p;
}

/* where the screen line starting at p ends (the next line's start) */
static const UBYTE *nextline(const UBYTE *p, const UBYTE *end, int cols)
{
    int col = 0;
    while (p < end) {
        UBYTE c = *p;
        LONG e;
        if (c == '\n')
            return p + 1;
        if ((e = escape(p, end))) {
            p += e;
            continue;
        }
        if (c == '\t') {
            int nc = (col / 8 + 1) * 8;
            if (nc > cols)
                return p;                  /* the tab starts the next line */
            col = nc;
        } else if (c >= 32 && (c < 0x7F || c >= 0xA0)) {
            if (col == cols)
                return p;
            col++;
        }
        p++;                               /* other control characters take no room */
    }
    return p;
}

static BOOL index_lines(int cols)
{
    const UBYTE *p = V.buf, *end = V.buf + V.len;
    LONG n = 0;
    for (p = V.buf; p < end; p = nextline(p, end, cols))
        n++;
    if (!n)
        n = 1;
    if (V.line)
        FreeMem(V.line, V.lcap * sizeof(ULONG));
    V.lcap = n;
    if (!(V.line = AllocMem(n * sizeof(ULONG), MEMF_PUBLIC))) {
        V.nlines = V.lcap = 0;
        return FALSE;
    }
    V.line[0] = 0;
    for (n = 0, p = V.buf; p < end; p = nextline(p, end, cols))
        V.line[n++] = p - V.buf;
    V.nlines = n ? n : 1;
    return TRUE;
}

/* one screen line as characters, padded to cols */
static void textrow(LONG i, char *out, int cols)
{
    const UBYTE *p, *end, *stop;
    int col = 0;
    if (i >= V.nlines) {
        for (col = 0; col < cols; col++)
            out[col] = ' ';
        return;
    }
    end = V.buf + V.len;
    p = V.buf + V.line[i];
    stop = i + 1 < V.nlines ? V.buf + V.line[i + 1] : end;
    while (p < stop && col < cols) {
        UBYTE c = *p;
        LONG e;
        if ((e = escape(p, end))) {
            p += e;
            continue;
        }
        if (c == '\t') {
            int nc = (col / 8 + 1) * 8;
            while (col < nc && col < cols)
                out[col++] = ' ';
        } else if (c >= 32 && (c < 0x7F || c >= 0xA0))
            out[col++] = c;
        p++;
    }
    while (col < cols)
        out[col++] = ' ';
}

/* ---- hex --------------------------------------------------------------------------------- */

static LONG hexrows(void)
{
    return V.size ? (V.size + 15) / 16 : 1;
}

static void hexrow(LONG r, char *out, int cols)
{
    static const char hx[] = "0123456789ABCDEF";
    UBYTE b[16];
    LONG off = r * 16, n = V.size - off, i;
    char *d = out;
    if (n > 16)
        n = 16;
    if (n < 0)
        n = 0;
    if (off + n <= V.len)
        CopyMem(V.buf + off, b, n);
    else if (V.fh) {                                 /* past the loaded part: from the file */
        Seek(V.fh, off, OFFSET_BEGINNING);
        if (Read(V.fh, b, n) != n)
            n = 0;
    } else
        n = 0;
    for (i = 7; i >= 0; i--)
        *d++ = hx[(off >> (i * 4)) & 15];
    *d++ = ':';
    *d++ = ' ';
    for (i = 0; i < 16; i++) {
        if (i < n) {
            *d++ = hx[b[i] >> 4];
            *d++ = hx[b[i] & 15];
        } else
            *d++ = ' ', *d++ = ' ';
        if (i & 1)
            *d++ = ' ';
        if (i == 7)
            *d++ = ' ';
    }
    *d++ = ' ';
    for (i = 0; i < 16; i++)
        *d++ = i >= n ? ' ' : (b[i] >= 32 && b[i] < 0x7F) || b[i] >= 0xA0 ? b[i] : '.';
    while (d - out < cols)
        *d++ = ' ';
}

/* ---- drawing ------------------------------------------------------------------------------- */

static LONG total(void)
{
    return V.hex ? hexrows() : V.nlines;
}

static void layout(void)
{
    struct Window *w = V.w;
    V.cols = (w->Width - w->BorderLeft - w->BorderRight - PROP_W - 10) / 8;
    if (V.cols > MAXCOLS)
        V.cols = MAXCOLS;
    if (V.cols < 8)
        V.cols = 8;
    V.rows = (w->Height - w->BorderTop - w->BorderBottom - 4) / LINE_H;
    if (V.rows < 1)
        V.rows = 1;
}

static void clamp(void)
{
    if (V.top > total() - V.rows)
        V.top = total() - V.rows;
    if (V.top < 0)
        V.top = 0;
}

static void drawrow(int r)
{
    char buf[MAXCOLS];
    struct Window *w = V.w;
    LONG i = V.top + r;
    if (V.hex) {
        if (i < hexrows())
            hexrow(i, buf, V.cols);
        else {
            int k;
            for (k = 0; k < V.cols; k++)
                buf[k] = ' ';
        }
    } else
        textrow(i, buf, V.cols);
    Move(V.rp, w->BorderLeft + 4, w->BorderTop + 2 + r * LINE_H + V.rp->TxBaseline);
    Text(V.rp, (STRPTR)buf, V.cols);
}

static void settitle(void)
{
    char a[16], b[16], c[16];
    if (V.hex) {
        fmt(a, "%lx", V.top * 16);
        fmt(b, "%ld", V.size);
        fmt(V.title, "%s - offset $%s of %s bytes - Tab: text, Esc: close", V.name, a, b);
    } else {
        fmt(a, "%ld", V.top + 1);
        fmt(b, "%ld", V.nlines);
        fmt(c, "%ld", V.len >> 10);
        fmt(V.title, "%s - line %s of %s%s%s%s - Tab: hex, Esc: close", V.name, a, b,
            V.len < V.size ? " (first " : "", V.len < V.size ? c : "", V.len < V.size ? "K)" : "");
    }
    SetWindowTitles(V.w, (UBYTE *)V.title, (UBYTE *)-1);
}

static void setprop(void)
{
    LONG over = total() - V.rows;
    UWORD body = MAXBODY, pot = 0;
    if (over > 0) {
        ULONG s = over / 0x8000 + 1;                    /* keeps the products in 32 bits */
        body = (UWORD)(((ULONG)V.rows * 0xFFFF) / (ULONG)total());
        if (body < 0x100)
            body = 0x100;
        pot = V.top >= over ? MAXPOT : (UWORD)(((ULONG)(V.top / s) * 0xFFFF) / (ULONG)(over / s));
    }
    NewModifyProp(&V.prop, V.w, NULL, AUTOKNOB | FREEVERT, 0, pot, MAXBODY, body, 1);
}

static void redraw(void)
{
    int r;
    SetAPen(V.rp, 1);
    SetBPen(V.rp, 0);
    SetDrMd(V.rp, JAM2);
    for (r = 0; r < V.rows; r++)
        drawrow(r);
    settitle();
    setprop();
}

/* move to a new top line; one-line steps scroll the window contents instead of redrawing */
static void scrollto(LONG t, BOOL fromprop)
{
    struct Window *w = V.w;
    LONG old = V.top, d;
    V.top = t;
    clamp();
    d = V.top - old;
    if (!d)
        return;
    SetAPen(V.rp, 1);
    SetBPen(V.rp, 0);
    SetDrMd(V.rp, JAM2);
    if (d == 1 || d == -1) {
        int x0 = w->BorderLeft + 4, y0 = w->BorderTop + 2;
        ScrollRaster(V.rp, 0, d * LINE_H, x0, y0, x0 + V.cols * 8 - 1, y0 + V.rows * LINE_H - 1);
        drawrow(d > 0 ? V.rows - 1 : 0);
    } else {
        int r;
        for (r = 0; r < V.rows; r++)
            drawrow(r);
    }
    settitle();
    if (!fromprop)
        setprop();
}

static LONG topfromprop(void)
{
    LONG over = total() - V.rows;
    ULONG s;
    if (over <= 0)
        return 0;
    s = over / 0x8000 + 1;
    return (LONG)((((ULONG)V.pi.VertPot * (ULONG)(over / s) + 0x7FFF) / 0xFFFF) * s);
}

static void switchmode(void)
{
    LONG off = V.hex ? V.top * 16 : (V.top < V.nlines ? (LONG)V.line[V.top] : 0), i;
    V.hex = !V.hex;
    if (V.hex)
        V.top = off / 16;
    else {                                       /* the screen line that holds that byte */
        for (i = 0; i + 1 < V.nlines && V.line[i + 1] <= (ULONG)off; i++)
            ;
        V.top = i;
    }
    clamp();
    redraw();
}

static void resized(void)
{
    LONG off = V.hex ? 0 : (V.top < V.nlines ? V.line[V.top] : 0), i;
    int oldcols = V.cols;
    layout();
    if (!V.hex && V.cols != oldcols && index_lines(V.cols)) {
        for (i = 0; i + 1 < V.nlines && V.line[i + 1] <= (ULONG)off; i++)
            ;
        V.top = i;
    }
    clamp();
    SetAPen(V.rp, 0);
    RectFill(V.rp, V.w->BorderLeft, V.w->BorderTop, V.w->Width - V.w->BorderRight - 1,
             V.w->Height - V.w->BorderBottom - 1);
    RefreshGadgets(&V.prop, V.w, NULL);
    redraw();
}

/* ---- the window ------------------------------------------------------------------------------ */

static void cleanup(void)
{
    if (V.w)
        CloseWindow(V.w);
    if (V.line)
        FreeMem(V.line, V.lcap * sizeof(ULONG));
    if (V.buf && V.alloc)
        FreeMem(V.buf, V.alloc);
    if (V.fh)
        Close(V.fh);
    V.w = NULL;
    V.line = NULL;
    V.buf = NULL;
    V.fh = 0;
}

static LONG load(const char *path, int cols)
{
    LONG max = (LONG)(AvailMem(MEMF_LARGEST) / 2) - 16384, n;
    if (max > 2L * 1024 * 1024)
        max = 2L * 1024 * 1024;
    if (max < 4096)
        return ERROR_NO_FREE_STORE;
    dos_quiet(TRUE);
    V.fh = Open((STRPTR)path, MODE_OLDFILE);
    dos_quiet(FALSE);
    if (!V.fh)
        return IoErr();
    Seek(V.fh, 0, OFFSET_END);
    V.size = Seek(V.fh, 0, OFFSET_BEGINNING);
    V.len = V.size < max ? V.size : max;
    V.alloc = V.len + 2;
    if (!(V.buf = AllocMem(V.alloc, MEMF_PUBLIC)))
        return ERROR_NO_FREE_STORE;
    n = Read(V.fh, V.buf, V.len);
    if (n != V.len)
        return n < 0 ? IoErr() : ERROR_SEEK_ERROR;
    if (!index_lines(cols))
        return ERROR_NO_FREE_STORE;
    return 0;
}

static const char *leafname(const char *path)
{
    const char *l = path;
    for (; *path; path++)
        if (*path == ':' || *path == '/')
            l = path + 1;
    return l;
}

static LONG show(struct Window *parent, struct TextAttr *ta, BOOL hex);
static ViewKey hook;

void view_sethook(ViewKey k)
{
    hook = k;
}

static int startcols(struct Window *parent)
{
    int c = (parent->WScreen->Width - 40) / 8;     /* close to the real width: index once */
    return c > MAXCOLS ? MAXCOLS : c;
}

LONG view_file(struct Window *parent, struct TextAttr *ta, const char *path, BOOL hex)
{
    LONG err;
    V.name = leafname(path);
    V.cols = startcols(parent);
    if ((err = load(path, V.cols))) {
        cleanup();
        return err;
    }
    return show(parent, ta, hex);
}

LONG view_text(struct Window *parent, struct TextAttr *ta, const char *name, char *text, LONG len)
{
    V.name = name;
    V.cols = startcols(parent);
    V.buf = (UBYTE *)text;
    V.alloc = 0;                                   /* the caller's: not freed here */
    V.len = V.size = len;
    V.fh = 0;
    if (!index_lines(V.cols)) {
        cleanup();
        return ERROR_NO_FREE_STORE;
    }
    return show(parent, ta, FALSE);
}

static LONG show(struct Window *parent, struct TextAttr *ta, BOOL hex)
{
    struct Screen *scr = parent->WScreen;
    struct NewWindow nw = {
        0, 0, 0, 0, 0, 1,
        CLOSEWINDOW | RAWKEY | VANILLAKEY | NEWSIZE | GADGETDOWN | GADGETUP | MOUSEMOVE | REFRESHWINDOW,
        WINDOWCLOSE | WINDOWDRAG | WINDOWDEPTH | WINDOWSIZING | SIZEBRIGHT | SMART_REFRESH |
            ACTIVATE | NOCAREREFRESH,
        NULL, NULL, (UBYTE *)"Dowser", NULL, NULL, 200, 60, 0, 0, WBENCHSCREEN
    };
    struct TextFont *font = NULL;
    struct IntuiMessage *im;
    struct Gadget zg = { 0 };
    struct PropInfo zp = { 0 };
    BOOL done = FALSE, drag = FALSE;
    int firstcols = V.cols;

    V.top = 0;
    V.hex = hex;
    nw.TopEdge = scr->BarHeight + 1;
    nw.Width = scr->Width;
    nw.Height = scr->Height - nw.TopEdge;
    nw.MaxWidth = scr->Width;
    nw.MaxHeight = scr->Height;
    V.prop = zg;
    V.pi = zp;
    V.pi.Flags = AUTOKNOB | FREEVERT;
    V.pi.VertBody = V.pi.HorizBody = MAXBODY;
    V.prop.Flags = GADGHNONE | GRELRIGHT | GRELHEIGHT;
    V.prop.Activation = RELVERIFY | GADGIMMEDIATE | FOLLOWMOUSE;
    V.prop.GadgetType = PROPGADGET;
    V.prop.GadgetRender = &V.knob;
    V.prop.SpecialInfo = &V.pi;
    V.prop.GadgetID = 1;
    nw.FirstGadget = &V.prop;
    /* the scroller runs down the right edge, inside the border, above the sizing gadget */
    V.prop.Width = PROP_W;
    if (!(V.w = OpenWindow(&nw))) {
        cleanup();
        return ERROR_NO_FREE_STORE;
    }
    RemoveGadget(V.w, &V.prop);
    V.prop.LeftEdge = -(V.w->BorderRight + PROP_W - 1) - 2;
    V.prop.TopEdge = V.w->BorderTop + 1;
    V.prop.Height = -(V.w->BorderTop + V.w->BorderBottom + 2);
    AddGadget(V.w, &V.prop, (UWORD)~0);
    V.rp = V.w->RPort;
    if ((font = OpenFont(ta)))
        SetFont(V.rp, font);
    layout();
    if (V.cols != firstcols && !hex)
        index_lines(V.cols);
    RefreshGadgets(&V.prop, V.w, NULL);
    redraw();

    while (!done) {
        WaitPort(V.w->UserPort);
        while ((im = (struct IntuiMessage *)GetMsg(V.w->UserPort))) {
            ULONG class = im->Class;
            UWORD code = im->Code, qual = im->Qualifier;
            BOOL shift = (qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) != 0;
            ReplyMsg((struct Message *)im);
            switch (class) {
            case CLOSEWINDOW: done = TRUE; break;
            case NEWSIZE:     resized(); break;
            case GADGETDOWN:  drag = TRUE; scrollto(topfromprop(), TRUE); break;
            case MOUSEMOVE:   if (drag) scrollto(topfromprop(), TRUE); break;
            case GADGETUP:    drag = FALSE; scrollto(topfromprop(), TRUE); setprop(); break;
            case RAWKEY:
                switch (code) {
                case 0x4C: scrollto(V.top - (shift ? V.rows - 1 : 1), FALSE); break;   /* up */
                case 0x4D: scrollto(V.top + (shift ? V.rows - 1 : 1), FALSE); break;   /* down */
                case 0x4F: case 0x3F: scrollto(V.top - (V.rows - 1), FALSE); break;    /* left, PgUp */
                case 0x4E: case 0x1F: scrollto(V.top + (V.rows - 1), FALSE); break;    /* right, PgDn */
                case 0x3D: scrollto(0, FALSE); break;                                  /* Home */
                case 0x1D: scrollto(total(), FALSE); break;                            /* End */
                }
                break;
            case VANILLAKEY:
                if (code == 27 || code == 'q' || code == 'Q')
                    done = TRUE;
                else if (code == ' ' || code == 13)
                    scrollto(V.top + (code == 13 ? 1 : V.rows - 1), FALSE);
                else if (code == 8 || code == 'b' || code == 'B')
                    scrollto(V.top - (V.rows - 1), FALSE);
                else if (code == 't' || code == 'T' || code == '<')
                    scrollto(0, FALSE);
                else if (code == 'e' || code == 'E' || code == '>')
                    scrollto(total(), FALSE);
                else if (code == 9 || code == 'h' || code == 'H')
                    switchmode();
                else if (hook) {
                    char *t;
                    LONG l;
                    const char *nm;
                    if (hook(code, &t, &l, &nm)) {            /* other text, e.g. the next block */
                        V.buf = (UBYTE *)t;
                        V.len = V.size = l;
                        V.name = nm;
                        V.top = 0;
                        if (index_lines(V.cols))
                            redraw();
                    }
                }
                break;
            }
        }
    }
    cleanup();
    if (font)
        CloseFont(font);
    return 0;
}

LONG view_lines(const char *path, int cols, int show, void (*out)(const char *))
{
    static char buf[MAXCOLS + 3];
    char line[48];
    LONG err, i;
    int k;
    if (cols > MAXCOLS)
        cols = MAXCOLS;
    err = load(path, cols);
    if (!err) {
        fmt(line, "%ld lines of %ld\n", V.nlines, (LONG)cols);
        out(line);
        for (i = 0; i < V.nlines && i < show; i++) {
            textrow(i, buf, cols);
            for (k = cols; k > 0 && buf[k - 1] == ' '; k--)
                ;
            buf[k] = '|';                  /* marks the end of the line as shown */
            buf[k + 1] = '\n';
            buf[k + 2] = 0;
            out(buf);
        }
    }
    cleanup();
    return err;
}
