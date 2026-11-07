/* Dowser user interface: one window on the Workbench screen, built by hand from V33
 * Intuition (Kickstart 1.3 has no GadTools). Written by Paul Seibert, (c) 2026 Desert Palms Software.
 *
 * The block map shows the 80 cylinders across and the 22 blocks of each cylinder down (head 0
 * above head 1). With only the Workbench's four colours, block types are told apart by pen
 * and by fill pattern (patterns in chip RAM: the blitter reads them). */
#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <graphics/gfxbase.h>
#include <graphics/gfxmacros.h>
#include <graphics/text.h>
#include <devices/inputevent.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/dos.h>
#include "util.h"
#include "dos13.h"
#include "disk.h"
#include "fs.h"
#include "boot.h"
#include "check.h"
#include "blockview.h"
#include "view.h"
#include "req.h"
#include "about.h"
#include "ui.h"

extern struct GfxBase *GfxBase;
extern struct IntuitionBase *IntuitionBase;

#define ROW_H   9
#define PROP_W  16
#define BTN_H   12
#define BGAP    4
#define WIN_W   640
#define MIN_H   190
#define CW      6                /* map cell: 6 x 4 pixels, of which 5 x 3 are drawn */
#define CH      4
#define HGAP    3                /* between the heads */
#define NTOP    6                /* DF0-DF3, Image, Rescan */
#define NBOT    3                /* Block, Rescue, Surface */

enum { BG, FG, DARK, HI };
enum { GID_TOP = 10, GID_TAB = 20, GID_BOT = 30, GID_PROP = 40 };
enum { B_IMAGE = 4, B_RESCAN = 5 };
enum { BB_BLOCK, BB_RESCUE, BB_SURFACE };

struct Btn {
    struct Gadget g;
    const char *label;
    int x, y, w;
    BOOL on, used;
};

static struct TextAttr topaz8 = { (STRPTR)"topaz.font", 8, FS_NORMAL, FPF_ROMFONT };
static struct TextFont *font;
static struct Window *win;
static struct RastPort *rp;
static UWORD *chip;              /* the fill patterns, in chip RAM */
#define PAT_GHOST   (chip)
#define PAT_CHECK   (chip + 2)
#define PAT_STRIPE  (chip + 4)
#define PAT_DOTS    (chip + 6)
#define PAT_VLINE   (chip + 8)

static struct FS fs;
static struct Disk disk;
static struct Check chk;
static BOOL loaded;
static char source[256], label[80];
static char lastimage[256] = "Work:";
static int tab = TAB_CHECK;
static LONG selb = -1;           /* the selected block */
static int selo = -1;            /* the selected object (its blocks are highlighted) */
static int top[NTABS], cur[NTABS];
static char info[100];
static BOOL infoIsMsg, resizedDuring;
static char bootlines[12][64];
static int nboot;

static struct Btn topb[NTOP], tabs[NTABS], bot[NBOT];
static const char *const topLabel[NTOP] = { "DF0:", "DF1:", "DF2:", "DF3:", "Image", "Rescan" };
static const char *const tabLabel[NTABS] = { "Check", "Files", "Boot" };
static const char *const botLabel[NBOT] = { "Block", "Rescue", "Surface" };
static struct Gadget prop;
static struct PropInfo propInfo;
static struct Image knob;
static BOOL dragging;
static ULONG released;        /* when Dowser last gave a drive back to DOS (seconds) */
static ULONG clickSecs, clickMicros;
static int clickRow = -1;
static LONG clickBlock = -1;

/* layout */
static int X0, XR, yBar, mapX, mapY, legX, yInfo, yTabs, listT, listB, rows, cols, yBot;

/* ---- drawing helpers ------------------------------------------------------------------------- */

static void text(int x, int y, int fg, int bg, const char *s, int n)
{
    SetAPen(rp, fg);
    SetBPen(rp, bg);
    SetDrMd(rp, JAM2);
    Move(rp, x, y + rp->TxBaseline);
    Text(rp, (STRPTR)s, n);
}

static void textpad(int x, int y, int fg, int bg, const char *s, int n)
{
    char buf[128];
    int i;
    if (n > (int)sizeof(buf))
        n = sizeof(buf);
    if (n <= 0)
        return;
    for (i = 0; i < n && s[i]; i++)
        buf[i] = s[i];
    for (; i < n; i++)
        buf[i] = ' ';
    text(x, y, fg, bg, buf, n);
}

static void fill(int x0, int y0, int x1, int y1, int pen)
{
    if (x1 < x0 || y1 < y0)
        return;
    SetAPen(rp, pen);
    SetDrMd(rp, JAM1);
    RectFill(rp, x0, y0, x1, y1);
}

static void box(int x0, int y0, int x1, int y1, int pen)
{
    SetAPen(rp, pen);
    SetDrMd(rp, JAM1);
    Move(rp, x0, y0);
    Draw(rp, x1, y0);
    Draw(rp, x1, y1);
    Draw(rp, x0, y1);
    Draw(rp, x0, y0);
}

/* a pattern fill: pen a where the pattern is set, pen b elsewhere */
static void pfill(int x0, int y0, int x1, int y1, int a, int b, UWORD *pat)
{
    SetAPen(rp, a);
    SetBPen(rp, b);
    SetDrMd(rp, JAM2);
    if (pat && chip)
        SetAfPt(rp, pat, 1);
    RectFill(rp, x0, y0, x1, y1);
    SetAfPt(rp, NULL, 0);
}

/* ---- block styles ---------------------------------------------------------------------------- */

enum { SY_FREE, SY_UNSEEN, SY_BOOT, SY_ROOT, SY_BITMAP, SY_DIR, SY_FILE, SY_EXT, SY_DATA, SY_DIRC,
       SY_LINK, SY_LOST, SY_UNREAD, SY_FAULT, NSTYLES };
static const char *const styleName[NSTYLES] = {
    "Free", "Not AmigaDOS", "Boot block", "Root block", "Bitmap", "Drawer", "File header",
    "File extension", "Data", "Dir cache", "Link", "Lost", "Unreadable", "Problem"
};

static int blockstyle(LONG b)
{
    UBYTE f = fs.flags[b];
    if (!loaded)
        return SY_FREE;
    if (f & (BF_TWICE | BF_UNMARKED | BF_CSUM))
        return SY_FAULT;
    switch (fs.kind[b]) {
    case BK_UNSEEN: return SY_UNSEEN;
    case BK_BOOT:   return SY_BOOT;
    case BK_ROOT:   return SY_ROOT;
    case BK_BITMAP: case BK_BMEXT: return SY_BITMAP;
    case BK_DIR:    return SY_DIR;
    case BK_FILE:   return SY_FILE;
    case BK_EXT:    return SY_EXT;
    case BK_DATA:   return SY_DATA;
    case BK_DIRC:   return SY_DIRC;
    case BK_LINK:   return SY_LINK;
    case BK_LOST:   return SY_LOST;
    case BK_UNREAD: return SY_UNREAD;
    default:        return SY_FREE;
    }
}

static void drawstyle(int st, int x0, int y0, int x1, int y1)
{
    switch (st) {
    case SY_FREE:   fill(x0, y0, x1, y1, BG); break;
    case SY_UNSEEN: pfill(x0, y0, x1, y1, FG, BG, PAT_DOTS); break;
    case SY_BOOT:   pfill(x0, y0, x1, y1, DARK, HI, PAT_CHECK); break;
    case SY_ROOT:   fill(x0, y0, x1, y1, DARK); break;
    case SY_BITMAP: pfill(x0, y0, x1, y1, DARK, FG, PAT_CHECK); break;
    case SY_DIR:    pfill(x0, y0, x1, y1, HI, FG, PAT_CHECK); break;
    case SY_FILE:   fill(x0, y0, x1, y1, HI); break;
    case SY_EXT:    pfill(x0, y0, x1, y1, HI, FG, PAT_STRIPE); break;
    case SY_DATA:   fill(x0, y0, x1, y1, FG); break;
    case SY_DIRC:   pfill(x0, y0, x1, y1, FG, DARK, PAT_STRIPE); break;
    case SY_LINK:   pfill(x0, y0, x1, y1, HI, DARK, PAT_STRIPE); break;
    case SY_LOST:   pfill(x0, y0, x1, y1, HI, BG, PAT_DOTS); break;
    case SY_UNREAD: pfill(x0, y0, x1, y1, DARK, BG, PAT_VLINE); break;
    default:        pfill(x0, y0, x1, y1, HI, DARK, PAT_CHECK); break;
    }
}

static void cellpos(LONG b, int *x, int *y)
{
    LONG cyl = b / 22, head = (b / 11) & 1, sec = b % 11;
    *x = mapX + cyl * CW;
    *y = mapY + head * (11 * CH + HGAP) + sec * CH;
}

static BOOL selected(LONG b)
{
    return selo >= 0 && loaded && fs.owner[b] == selo + 1;
}

static void drawcell(LONG b)
{
    int x, y;
    cellpos(b, &x, &y);
    drawstyle(blockstyle(b), x, y, x + CW - 2, y + CH - 2);
    if (selected(b)) {                               /* the selected file's blocks: inverted */
        SetDrMd(rp, COMPLEMENT);
        RectFill(rp, x, y, x + CW - 2, y + CH - 2);
        SetDrMd(rp, JAM1);
    }
    /* the selected block: a frame in the gap around the cell */
    box(x - 1, y - 1, x + CW - 1, y + CH - 1, b == selb ? HI : BG);
}

/* the whole map. A 68000 is slow at thousands of small fills, so: free cells are left as the
   background, runs of same-style cells down a column are one fill, and the gaps between the
   cells are put back afterwards with one line per column and per row */
static void drawmap(void)
{
    LONG cyl, b;
    int head, s, e, st, x, y0;
    fill(mapX - 2, mapY - 2, mapX + 80 * CW, mapY + 22 * CH + HGAP, BG);
    if (loaded) {
        for (cyl = 0; cyl < 80; cyl++)
            for (head = 0; head < 2; head++) {
                LONG first = (cyl * 2 + head) * 11;
                cellpos(first, &x, &y0);
                for (s = 0; s < 11; s = e + 1) {
                    st = blockstyle(first + s);
                    for (e = s; e + 1 < 11 && blockstyle(first + e + 1) == st; e++)
                        ;
                    if (st != SY_FREE)
                        drawstyle(st, x, y0 + s * CH, x + CW - 2, y0 + e * CH + CH - 2);
                }
            }
        SetAPen(rp, BG);
        SetDrMd(rp, JAM1);
        for (cyl = 0; cyl < 80; cyl++) {             /* the gaps between the columns */
            Move(rp, mapX + cyl * CW + CW - 1, mapY);
            Draw(rp, mapX + cyl * CW + CW - 1, mapY + 22 * CH + HGAP - 1);
        }
        for (head = 0; head < 2; head++)             /* and between the rows */
            for (s = 1; s <= 11; s++) {
                int y = mapY + head * (11 * CH + HGAP) + s * CH - 1;
                Move(rp, mapX, y);
                Draw(rp, mapX + 80 * CW - 1, y);
            }
        for (b = 0; b < NBLOCKS; b++)                /* the selection on top */
            if (selected(b) || b == selb)
                drawcell(b);
    }
    box(mapX - 3, mapY - 3, mapX + 80 * CW + 1, mapY + 22 * CH + HGAP + 1, FG);
}

static void drawlegend(void)
{
    int i, y = mapY - 4;
    for (i = 0; i < NSTYLES; i++, y += 8) {
        if (i == SY_UNSEEN || i == SY_LINK)
            { y -= 8; continue; }                    /* (rare: left out to save room) */
        drawstyle(i, legX, y + 2, legX + 9, y + 6);
        box(legX - 1, y + 1, legX + 10, y + 7, FG);
        SetFont(rp, font);
        textpad(legX + 16, y - 1, FG, BG, styleName[i], (XR - legX - 16) / 8);
    }
}

/* ---- buttons ----------------------------------------------------------------------------------- */

static void drawbtn(struct Btn *b, BOOL active)
{
    int n = slen(b->label);
    if (!b->used)
        return;
    if (b->g.Flags & GRELBOTTOM)
        b->y = win->Height + b->g.TopEdge;
    if (n > (b->w - 4) / 8)
        n = (b->w - 4) / 8;
    fill(b->x + 1, b->y + 1, b->x + b->w - 2, b->y + BTN_H - 2, active ? HI : BG);
    box(b->x, b->y, b->x + b->w - 1, b->y + BTN_H - 1, FG);
    text(b->x + (b->w - n * 8) / 2, b->y + 2, active ? DARK : FG, active ? HI : BG, b->label, n);
    if (!b->on && chip) {
        SetDrMd(rp, JAM1);
        SetAPen(rp, BG);
        SetAfPt(rp, PAT_GHOST, 1);
        RectFill(rp, b->x + 1, b->y + 1, b->x + b->w - 2, b->y + BTN_H - 2);
        SetAfPt(rp, NULL, 0);
    }
}

static void initbtn(struct Btn *b, int id, const char *lab, int x, int y, int w, BOOL relbottom, BOOL on)
{
    struct Gadget z = { 0 };
    b->g = z;
    b->g.LeftEdge = x;
    b->g.TopEdge = relbottom ? y - win->Height : y;
    b->g.Width = w;
    b->g.Height = BTN_H;
    b->g.Flags = (on ? GADGHCOMP : GADGHNONE) | (relbottom ? GRELBOTTOM : 0);
    b->g.Activation = RELVERIFY;
    b->g.GadgetType = BOOLGADGET;
    b->g.GadgetID = id;
    b->label = lab;
    b->x = x;
    b->y = y;
    b->w = w;
    b->on = on;
    b->used = TRUE;
    AddGadget(win, &b->g, (UWORD)~0);
}

static __attribute__((unused)) void enable(struct Btn *b, BOOL on)
{
    UWORD pos;
    if (!b->used || b->on == on)
        return;
    pos = RemoveGadget(win, &b->g);
    b->g.Flags = (b->g.Flags & ~GADGHIGHBITS) | (on ? GADGHCOMP : GADGHNONE);
    AddGadget(win, &b->g, pos);
    b->on = on;
    drawbtn(b, FALSE);
}

/* ---- layout ------------------------------------------------------------------------------------ */

static void layout(void)
{
    X0 = win->BorderLeft + 4;
    XR = win->Width - win->BorderRight - 5;
    yBar = win->BorderTop + 3;
    mapX = X0 + 3;
    mapY = yBar + BTN_H + 7;
    legX = mapX + 80 * CW + 12;
    yInfo = mapY + 22 * CH + HGAP + 6;
    yTabs = yInfo + 11;
    listT = yTabs + BTN_H + 5;
    yBot = win->Height - win->BorderBottom - 3 - BTN_H;
    listB = yBot - 5;
    rows = (listB - listT + 1) / ROW_H;
    if (rows < 1)
        rows = 1;
    cols = (XR - X0 - PROP_W - 8) / 8;
}

static void present(BOOL p[4])
{
    int u;
    for (u = 0; u < 4; u++)
        p[u] = disk_present(u);
}

static void addgadgets(void)
{
    BOOL p[4];
    int i, x = X0, w = 52;
    struct Gadget z = { 0 };
    present(p);
    for (i = 0; i < NTOP; i++) {
        topb[i].used = FALSE;
        if (i < 4 && !p[i])
            continue;
        initbtn(&topb[i], GID_TOP + i, topLabel[i], x, yBar, i >= 4 ? 60 : w, FALSE, TRUE);
        x += topb[i].w + BGAP;
    }
    for (i = 0; i < NTABS; i++)
        initbtn(&tabs[i], GID_TAB + i, tabLabel[i], X0 + i * (76 + BGAP), yTabs, 76, FALSE, TRUE);
    for (i = 0; i < NBOT; i++)
        initbtn(&bot[i], GID_BOT + i, botLabel[i], X0 + i * (76 + BGAP), yBot, 76, TRUE, i == BB_BLOCK);
    propInfo.Flags = AUTOKNOB | FREEVERT;
    propInfo.VertBody = propInfo.HorizBody = MAXBODY;
    prop = z;
    prop.LeftEdge = XR - PROP_W + 1;
    prop.TopEdge = listT - 2;
    prop.Width = PROP_W;
    prop.Height = (listB + 2) - (listT - 2) + 1 - win->Height;
    prop.Flags = GADGHNONE | GRELHEIGHT;
    prop.Activation = RELVERIFY | GADGIMMEDIATE | FOLLOWMOUSE;
    prop.GadgetType = PROPGADGET;
    prop.GadgetRender = &knob;
    prop.SpecialInfo = &propInfo;
    prop.GadgetID = GID_PROP;
    AddGadget(win, &prop, (UWORD)~0);
    RefreshGadgets(win->FirstGadget, win, NULL);
}

/* ---- the list under the tabs ------------------------------------------------------------------- */

static int count(int t)
{
    if (!loaded)
        return 0;
    if (t == TAB_CHECK)
        return chk.nf + (fs.dos ? 2 : 1);
    if (t == TAB_FILES)
        return fs.nobj > 0 ? fs.nobj - 1 : 0;
    return nboot;
}

static void rowtext(int t, int i, char *out, int size)
{
    static char path[PATHMAX], ln[PATHMAX + 64];
    out[0] = 0;
    if (t == TAB_CHECK) {
        if (i < chk.nf)
            check_line(&fs, &chk.f[i], ln, sizeof(ln));
        else if (i == chk.nf) {
            if (!chk.errors && !chk.warnings)
                scopy(ln, fs.dos ? "No problems found." : "Not an AmigaDOS disk: only the map applies.", sizeof(ln));
            else
                fmt(ln, "Result: %ld error%s, %ld warning%s.", chk.errors, chk.errors == 1 ? "" : "s",
                    chk.warnings, chk.warnings == 1 ? "" : "s");
        } else {
            path[0] = 0;
            if (chk.maxfragobj > 0)
                fs_path(&fs, chk.maxfragobj, path, PATHMAX);
            if (chk.fragfiles)
                fmt(ln, "Fragmented: %ld of %ld files (most pieces: %ld, \"%s\").", chk.fragfiles, chk.files,
                    (LONG)chk.maxfrags, path);
            else
                fmt(ln, "Fragmented: none of %ld files.", chk.files);
        }
        scopy(out, ln, size);
    } else if (t == TAB_FILES) {
        struct Obj *o = &fs.obj[i + 1];
        int pw = cols - 32, n;
        char sz[16], *p;
        fs_path(&fs, i + 1, path, PATHMAX);
        n = slen(path);
        p = n > pw ? path + n - pw : path;           /* long paths: keep the end */
        if (o->kind == OK_DIR)
            scopy(sz, "Drawer", sizeof(sz));
        else if (o->kind != OK_FILE)
            scopy(sz, "Link", sizeof(sz));
        else
            fmt(sz, "%ld", o->size);
        for (n = 0; n < pw && p[n]; n++)
            ln[n] = p[n];
        while (n < pw)
            ln[n++] = ' ';
        fmt(ln + n, " %8s %5ld %4ld  %s", sz, (LONG)o->nblocks, (LONG)o->frags,
            o->status & OS_UNREADABLE ? "unreadable" : o->status & OS_DAMAGED ? "damaged" : "ok");
        scopy(out, ln, size);
    } else if (i < nboot)
        scopy(out, bootlines[i], size);
}

static void setprop(void)
{
    int n = count(tab), over = n - rows;
    UWORD body = MAXBODY, pot = 0;
    if (dragging)
        return;
    if (over > 0) {
        body = (UWORD)((ULONG)rows * MAXBODY / n);
        pot = (UWORD)((ULONG)top[tab] * MAXPOT / over);
    }
    NewModifyProp(&prop, win, NULL, AUTOKNOB | FREEVERT, 0, pot, MAXBODY, body, 1);
}

static void drawlist(void)
{
    static char buf[160];
    int r, n = count(tab), w = cols;
    for (r = 0; r < rows; r++) {
        int i = top[tab] + r, y = listT + r * ROW_H;
        BOOL c = i == cur[tab] && i < n;
        if (i < n)
            rowtext(tab, i, buf, sizeof(buf));
        else
            buf[0] = 0;
        textpad(X0 + 3, y, c ? DARK : FG, c ? HI : BG, buf, w);
        fill(X0 + 3, y + 8, X0 + 3 + w * 8 - 1, y + ROW_H - 1, c ? HI : BG);
    }
    if (!loaded)
        textpad(X0 + 11, listT + ROW_H, FG, BG, "Choose a drive or an image above.", w - 2);
    box(X0, listT - 3, XR - PROP_W - 2, listB + 3, FG);
    setprop();
}

static void drawtabs(void)
{
    int i;
    for (i = 0; i < NTABS; i++)
        drawbtn(&tabs[i], i == tab);
}

/* ---- the info line and the label -------------------------------------------------------------- */

static void drawinfo(void)
{
    static char path[PATHMAX];
    int n = (XR - X0 + 1) / 8;
    if (!infoIsMsg) {
        if (!loaded)
            info[0] = 0;
        else if (selb >= 0) {
            path[0] = 0;
            if (fs.owner[selb] > 1)
                fs_path(&fs, fs.owner[selb] - 1, path, PATHMAX);
            fmt(info, "Block %ld (cyl %ld, head %ld, sec %ld): %s%s%s%s", selb, selb / 22, (selb / 11) & 1,
                selb % 11, fs_kindname(fs.kind[selb]), path[0] ? " of \"" : "", path, path[0] ? "\"" : "");
        } else
            scopy(info, "Click a block, a file or a finding. Double-click shows the block.", sizeof(info));
    }
    textpad(X0, yInfo, infoIsMsg ? HI : FG, BG, info, n);
}

static void message(const char *m)
{
    scopy(info, m, sizeof(info));
    infoIsMsg = TRUE;
    drawinfo();
}

static void drawlabel(void)
{
    int x = topb[B_RESCAN].x + topb[B_RESCAN].w + 10;
    textpad(x, yBar + 2, loaded ? HI : FG, BG, label, (XR - x) / 8);
}

static void redrawall(void)
{
    int i;
    for (i = 0; i < NTOP; i++)
        drawbtn(&topb[i], FALSE);
    for (i = 0; i < NBOT; i++)
        drawbtn(&bot[i], FALSE);
    drawlabel();
    drawmap();
    drawlegend();
    drawinfo();
    drawtabs();
    drawlist();
}

static void resized(void)
{
    layout();
    fill(win->BorderLeft, win->BorderTop, win->Width - win->BorderRight - 1, win->Height - win->BorderBottom - 1, BG);
    RefreshGadgets(win->FirstGadget, win, NULL);
    if (cur[tab] >= 0 && cur[tab] < top[tab])
        top[tab] = cur[tab];
    if (cur[tab] >= top[tab] + rows)
        top[tab] = cur[tab] - rows + 1;
    redrawall();
}

/* drop what arrived while busy (a scan, a dialog); Esc or the close gadget stops a scan */
static void flushinput(BOOL *stop)
{
    struct IntuiMessage *im;
    while ((im = (struct IntuiMessage *)GetMsg(win->UserPort))) {
        ULONG class = im->Class;
        UWORD code = im->Code;
        ReplyMsg((struct Message *)im);
        if (class == NEWSIZE)
            resizedDuring = TRUE;
        else if (class == REFRESHWINDOW) {
            BeginRefresh(win);
            EndRefresh(win, TRUE);
        } else if (stop && ((class == RAWKEY && code == 0x45) || class == CLOSEWINDOW))
            *stop = TRUE;
    }
}

static void afterbusy(void)
{
    flushinput(NULL);
    if (resizedDuring) {
        resizedDuring = FALSE;
        resized();
    }
}

/* ---- selection -------------------------------------------------------------------------------- */

/* select block b and its owner; redraw only the cells that change */
static void selectblk(LONG b, int o)
{
    LONG oldb = selb, k;
    int oldo = selo;
    LONG changed = 0;
    selb = b;
    selo = o;
    if (oldo != o)
        for (k = 0; k < NBLOCKS; k++)
            if ((oldo >= 0 && fs.owner[k] == oldo + 1) || (o >= 0 && fs.owner[k] == o + 1))
                changed++;
    if (changed > 150)
        drawmap();
    else if (oldo != o)
        for (k = 0; k < NBLOCKS; k++)
            if ((oldo >= 0 && fs.owner[k] == oldo + 1) || (o >= 0 && fs.owner[k] == o + 1))
                drawcell(k);
    if (oldb >= 0)
        drawcell(oldb);
    if (b >= 0)
        drawcell(b);
    infoIsMsg = FALSE;
    drawinfo();
}

static void selectrow(int i)
{
    int n = count(tab);
    if (i < 0 || i >= n)
        return;
    cur[tab] = i;
    if (i < top[tab])
        top[tab] = i;
    if (i >= top[tab] + rows)
        top[tab] = i - rows + 1;
    drawlist();
    if (tab == TAB_CHECK && i < chk.nf)
        selectblk(chk.f[i].block, chk.f[i].obj == NOOBJ ? -1 : chk.f[i].obj);
    else if (tab == TAB_FILES)
        selectblk(fs.obj[i + 1].header, i + 1);
    else if (tab == TAB_BOOT)
        selectblk(0, -1);
}

static void settab(int t)
{
    tab = t;
    drawtabs();
    drawlist();
}

/* ---- reading a disk ----------------------------------------------------------------------------- */

static BOOL s_progress(void *ctx, LONG done, LONG total)
{
    BOOL stop = FALSE;
    int n = (XR - X0 + 1) / 8, barw = 160, bx = XR - barw + 1, w;
    if (!resizedDuring) {
        char line[80];
        fmt(line, "Reading %s ... (Esc stops)", source);
        textpad(X0, yInfo, HI, BG, line, n - barw / 8 - 1);
        w = (int)(done * (barw - 2) / total);
        box(bx, yInfo, XR, yInfo + 7, FG);
        fill(bx + 1, yInfo + 1, bx + w, yInfo + 6, HI);
        fill(bx + 1 + w, yInfo + 1, XR - 1, yInfo + 6, BG);
    }
    flushinput(&stop);
    return !stop;
}

static void makeboot(void)
{
    struct BootInfo *bi = &chk.boot;
    char t[20];
    int i;
    nboot = 0;
    if ((bi->dostype & 0xFFFFFF00UL) == 0x444F5300UL)
        fmt(t, "DOS\\%ld", (LONG)(bi->dostype & 0xFF));
    else
        fmt(t, "$%08lx", bi->dostype);
    fmt(bootlines[nboot++], "Disk type:  %s", t);
    fmt(bootlines[nboot++], "Boot code:  %s", boot_kindname(bi->kind));
    fmt(bootlines[nboot++], "Checksum:   %s", bi->sumok ? "ok" : "wrong");
    fmt(bootlines[nboot++], "Bootable:   %s", bi->kind == BOOT_NOCODE ? "no (fine for a data disk)" :
        !bi->sumok ? "no: the checksum is wrong" : "yes");
    fmt(bootlines[nboot++], "CRC32:      %08lx (of the code area, to look it up)", bi->crc);
    if (bi->kind == BOOT_CUSTOM)
        scopy(bootlines[nboot++], "Custom code can be a game loader, a utility or a virus.", 64);
    for (i = 0; i < bi->nstr && nboot < 12; i++)
        fmt(bootlines[nboot++], "Text:       \"%s\"", bi->str[i]);
}

static void load(const char *src)
{
    const char *why;
    char msg[120];
    LONG err;
    int i;
    if (loaded)
        fs_free(&fs);
    loaded = FALSE;
    selb = -1;
    selo = -1;
    for (i = 0; i < NTABS; i++)
        top[i] = cur[i] = 0;
    scopy(source, src, sizeof(source));
    label[0] = 0;
    drawlabel();
    drawmap();
    if ((err = disk_open(&disk, src, &why))) {
        fmt(msg, "Cannot open %s: %s.", src, why);
        drawlist();
        message(msg);
        return;
    }
    err = fs_scan(&fs, &disk, s_progress, NULL, &why);
    if (!err)
        check_run(&fs, &disk, &chk);
    disk_close(&disk);                               /* DOS can have the drive back */
    {
        ULONG m;
        CurrentTime(&released, &m);
    }
    afterbusy();
    if (err) {
        fmt(msg, "Cannot read %s: %s.", src, why ? why : disk_errstr(err));
        fs_free(&fs);
        drawlist();
        message(msg);
        return;
    }
    loaded = TRUE;
    makeboot();
    if (fs.dos) {
        static const char *const kind[] = { "OFS", "FFS", "OFS intl", "FFS intl", "OFS dircache", "FFS dircache" };
        fmt(label, "%s  %s  %ld/1760 used", fs.volname, (fs.dostype & 0xFF) <= 5 ? kind[fs.dostype & 0xFF] : "?",
            fs.usedreal);
    } else
        scopy(label, "not an AmigaDOS disk", sizeof(label));
    drawlabel();
    drawmap();
    drawlist();
    if (!chk.errors && !chk.warnings)
        fmt(msg, "%s  %s", src, fs.dos ? "no problems found." : "not an AmigaDOS disk.");
    else
        fmt(msg, "%s  %ld error%s, %ld warning%s (see Check).", src, chk.errors, chk.errors == 1 ? "" : "s",
            chk.warnings, chk.warnings == 1 ? "" : "s");
    message(msg);
}

/* ---- the block viewer -------------------------------------------------------------------------- */

static char bvbuf[6000], bvtitle[32];
static LONG bvblock;

static BOOL bvkey(int key, char **t, LONG *len, const char **name)
{
    LONG nb = bvblock;
    if (key == 'n' || key == 'N' || key == '+')
        nb++;
    else if (key == 'p' || key == 'P' || key == '-')
        nb--;
    else
        return FALSE;
    if (nb < 0 || nb >= NBLOCKS)
        return FALSE;
    bvblock = nb;
    *len = bv_text(&fs, &disk, nb, bvbuf, sizeof(bvbuf));
    fmt(bvtitle, "Block %ld (n/p: next/previous)", nb);
    *t = bvbuf;
    *name = bvtitle;
    return TRUE;
}

static void blockview(LONG b)
{
    const char *why;
    LONG len, err;
    if (!loaded || b < 0)
        return;
    if ((err = disk_open(&disk, source, &why))) {
        char msg[100];
        fmt(msg, "Cannot open %s: %s.", source, why);
        message(msg);
        return;
    }
    bvblock = b;
    len = bv_text(&fs, &disk, b, bvbuf, sizeof(bvbuf));
    fmt(bvtitle, "Block %ld (n/p: next/previous)", b);
    view_sethook(bvkey);
    view_text(win, &topaz8, bvtitle, bvbuf, len);
    view_sethook(NULL);
    disk_close(&disk);
    {
        ULONG m;
        CurrentTime(&released, &m);
    }
    afterbusy();
    if (bvblock != selb && bvblock >= 0)
        selectblk(bvblock, fs.owner[bvblock] ? fs.owner[bvblock] - 1 : -1);
}

static void openimage(void)
{
    char path[256];
    BPTR l;
    scopy(path, lastimage, sizeof(path));
    if (!req_string(win, &topaz8, "Open image", "Disk image file (.adf), e.g. Work:Disks/game.adf:", path, 255) || !path[0]) {
        afterbusy();
        return;
    }
    afterbusy();
    dos_quiet(TRUE);
    l = Lock((STRPTR)path, ACCESS_READ);
    dos_quiet(FALSE);
    if (l)
        UnLock(l);
    scopy(lastimage, path, sizeof(lastimage));
    load(path);
}

/* ---- events -------------------------------------------------------------------------------------- */

static LONG mapblock(int mx, int my)
{
    int col, y, head, sec;
    if (mx < mapX || mx >= mapX + 80 * CW || my < mapY || my >= mapY + 22 * CH + HGAP)
        return -1;
    col = (mx - mapX) / CW;
    y = my - mapY;
    head = y >= 11 * CH + HGAP ? 1 : 0;
    if (head)
        y -= 11 * CH + HGAP;
    else if (y >= 11 * CH)
        return -1;                                   /* the gap between the heads */
    sec = y / CH;
    return ((LONG)col * 2 + head) * 11 + sec;
}

static void click(int mx, int my, ULONG secs, ULONG micros)
{
    LONG b = mapblock(mx, my);
    if (b >= 0 && loaded) {
        if (b == clickBlock && DoubleClick(clickSecs, clickMicros, secs, micros)) {
            clickBlock = -1;
            blockview(b);
            return;
        }
        clickBlock = b;
        clickSecs = secs;
        clickMicros = micros;
        selectblk(b, fs.owner[b] ? fs.owner[b] - 1 : -1);
        if (fs.owner[b] && fs.owner[b] > 1) {        /* show the file in the Files list too */
            int i = fs.owner[b] - 2;
            cur[TAB_FILES] = i;
            if (tab == TAB_FILES)
                selectrow(i);
        }
        return;
    }
    if (mx >= X0 && mx < XR - PROP_W && my >= listT && my < listT + rows * ROW_H && loaded) {
        int i = top[tab] + (my - listT) / ROW_H;
        if (i == clickRow && DoubleClick(clickSecs, clickMicros, secs, micros)) {
            clickRow = -1;
            blockview(selb);
            return;
        }
        clickRow = i;
        clickSecs = secs;
        clickMicros = micros;
        selectrow(i);
    }
}

static void key(UWORD code, UWORD qual)
{
    BOOL shift = (qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) != 0;
    if (code & IECODE_UP_PREFIX)
        return;
    switch (code) {
    case 0x4C: selectrow(cur[tab] - (shift ? rows - 1 : 1)); break;          /* up */
    case 0x4D: selectrow(cur[tab] + (shift ? rows - 1 : 1)); break;          /* down */
    case 0x4F: if (selb > 0) selectblk(selb - 1, fs.owner[selb - 1] ? fs.owner[selb - 1] - 1 : -1); break;   /* left */
    case 0x4E: if (selb >= 0 && selb < NBLOCKS - 1) selectblk(selb + 1, fs.owner[selb + 1] ? fs.owner[selb + 1] - 1 : -1); break;
    case 0x44: case 0x43: blockview(selb); break;                            /* Return */
    case 0x42: settab((tab + 1) % NTABS); break;                             /* Tab */
    }
}

static void topfromprop(void)
{
    int over = count(tab) - rows, t = 0;
    if (over > 0)
        t = (int)(((ULONG)propInfo.VertPot * over + MAXPOT / 2) / MAXPOT);
    if (t != top[tab]) {
        top[tab] = t;
        drawlist();
    }
}

/* ---- menus ------------------------------------------------------------------------------------------ */

#define N_PROJ 4
static struct MenuItem projItem[N_PROJ];
static struct IntuiText projText[N_PROJ];
static struct Menu projMenu = { NULL, 0, 0, 80, 0, MENUENABLED, (UBYTE *)"Project", projItem, 0, 0, 0, 0 };

static void initmenus(void)
{
    static const char *const lab[N_PROJ] = { "Open image...", "Rescan", "About...", "Quit" };
    static const char keys[N_PROJ] = { 'O', 'R', 0, 'Q' };
    int i;
    for (i = 0; i < N_PROJ; i++) {
        struct IntuiText t = { 0, 1, JAM2, 2, 1, &topaz8, (UBYTE *)lab[i], NULL };
        struct MenuItem m = { i + 1 < N_PROJ ? &projItem[i + 1] : NULL, 0, i * 10, 8 * 14 + COMMWIDTH + 8, 10,
                              ITEMTEXT | ITEMENABLED | HIGHCOMP | (keys[i] ? COMMSEQ : 0), 0, &projText[i], NULL,
                              keys[i], NULL, MENUNULL };
        projText[i] = t;
        projItem[i] = m;
    }
}

static void menupick(UWORD code, BOOL *quit)
{
    while (code != MENUNULL) {
        struct MenuItem *it = ItemAddress(&projMenu, code);
        switch (ITEMNUM(code)) {
        case 0: openimage(); break;
        case 1: if (source[0]) load(source); break;
        case 2: about_show(win, &topaz8); afterbusy(); break;
        case 3: *quit = TRUE; break;
        }
        code = it ? it->NextSelect : MENUNULL;
    }
}

static void gadgetup(struct Gadget *g)
{
    int id = g->GadgetID;
    if (id == GID_PROP) {
        topfromprop();
        dragging = FALSE;
        setprop();
    } else if (id >= GID_TOP && id < GID_TOP + 4) {
        char dev[6];
        fmt(dev, "DF%ld:", (LONG)(id - GID_TOP));
        load(dev);
    } else if (id == GID_TOP + B_IMAGE)
        openimage();
    else if (id == GID_TOP + B_RESCAN) {
        if (source[0])
            load(source);
        else
            message("Choose a drive or an image first.");
    } else if (id >= GID_TAB && id < GID_TAB + NTABS)
        settab(id - GID_TAB);
    else if (id == GID_BOT + BB_BLOCK) {
        if (selb >= 0)
            blockview(selb);
        else
            message("Click a block on the map (or a file or a finding) first.");
    }
}

BOOL ui_handle(void)
{
    struct IntuiMessage *im;
    BOOL quit = FALSE;
    while ((im = (struct IntuiMessage *)GetMsg(win->UserPort))) {
        ULONG class = im->Class, secs = im->Seconds, micros = im->Micros;
        UWORD code = im->Code, qual = im->Qualifier;
        WORD mx = im->MouseX, my = im->MouseY;
        struct Gadget *g = (struct Gadget *)im->IAddress;
        ReplyMsg((struct Message *)im);
        switch (class) {
        case CLOSEWINDOW:   quit = TRUE; break;
        case GADGETDOWN:    if (g->GadgetID == GID_PROP) { dragging = TRUE; topfromprop(); } break;
        case MOUSEMOVE:     if (dragging) topfromprop(); break;
        case GADGETUP:      gadgetup(g); break;
        case MOUSEBUTTONS:  if (code == SELECTDOWN) click(mx, my, secs, micros); break;
        case RAWKEY:        key(code, qual); break;
        case MENUPICK:      menupick(code, &quit); break;
        case NEWSIZE:       resized(); break;
        case DISKINSERTED:
        case DISKREMOVED:
            if (loaded && disk_unit(source) >= 0 && secs - released > 4)
                message("A disk was changed: press Rescan to read the disk in the drive now.");
            break;
        case REFRESHWINDOW: BeginRefresh(win); EndRefresh(win, TRUE); break;
        }
    }
    return !quit;
}

/* ---- open / close ------------------------------------------------------------------------------------ */

BOOL ui_open(const char *src, int t, LONG block, const char *file)
{
    struct NewWindow nw = {
        0, 0, WIN_W, 200, 0, 1,
        CLOSEWINDOW | GADGETUP | GADGETDOWN | MOUSEMOVE | MOUSEBUTTONS | MENUPICK | NEWSIZE |
            REFRESHWINDOW | RAWKEY | DISKINSERTED | DISKREMOVED,
        WINDOWCLOSE | WINDOWDRAG | WINDOWDEPTH | WINDOWSIZING | SIZEBBOTTOM | SMART_REFRESH | ACTIVATE,
        NULL, NULL, (UBYTE *)"Dowser 1.0  -  Desert Palms Software", NULL, NULL,
        WIN_W, MIN_H, WIN_W, 1024, WBENCHSCREEN
    };
    static const UWORD pats[10] = { 0x8888, 0x2222, 0xAAAA, 0x5555, 0xFFFF, 0x0000, 0x8888, 0x0000, 0xAAAA, 0xAAAA };
    int i;
    if ((chip = AllocMem(sizeof(pats), MEMF_CHIP)))
        CopyMem((APTR)pats, chip, sizeof(pats));
    if (!(win = OpenWindow(&nw)))
        return FALSE;
    WindowLimits(win, WIN_W, MIN_H, WIN_W, win->WScreen->Height);
    rp = win->RPort;
    if ((font = OpenFont(&topaz8)))
        SetFont(rp, font);
    dos_setwindow(win);
    initmenus();
    SetMenuStrip(win, &projMenu);
    layout();
    addgadgets();
    tab = t >= 0 && t < NTABS ? t : TAB_CHECK;
    redrawall();
    {   /* 1.3 reports 200 rows until the Workbench prefs are loaded: grow to the real screen */
        struct Screen *s = win->WScreen;
        int dh = s->Height - win->TopEdge - win->Height;
        if (dh > 0)
            SizeWindow(win, 0, dh);
    }
    if (src && src[0]) {
        Delay(25);                                   /* (let the size change arrive first) */
        afterbusy();
        if (disk_unit(src) < 0)
            scopy(lastimage, src, sizeof(lastimage));
        load(src);
        if (loaded && file && file[0]) {
            static char path[PATHMAX];
            for (i = 1; i < fs.nobj; i++) {
                fs_path(&fs, i, path, PATHMAX);
                if (!nocase_cmp(path, file)) {
                    settab(TAB_FILES);
                    selectrow(i - 1);
                    break;
                }
            }
        }
        if (loaded && block >= 0 && block < NBLOCKS) {
            selectblk(block, fs.owner[block] ? fs.owner[block] - 1 : -1);
            blockview(block);
        }
    }
    return TRUE;
}

void ui_close(void)
{
    dos_setwindow(NULL);
    if (win) {
        ClearMenuStrip(win);
        CloseWindow(win);
        win = NULL;
    }
    if (loaded)
        fs_free(&fs);
    loaded = FALSE;
    if (font)
        CloseFont(font);
    font = NULL;
    if (chip)
        FreeMem(chip, 10 * sizeof(UWORD));
    chip = NULL;
}

ULONG ui_sigmask(void)
{
    return 1UL << win->UserPort->mp_SigBit;
}
