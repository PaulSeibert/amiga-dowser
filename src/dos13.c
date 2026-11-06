/* Oasis - AmigaDOS helpers for Kickstart 1.3 (V33).
 * Written by Paul Seibert, (c) 2026 Desert Palms Software. */
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include "util.h"
#include "dos13.h"

extern struct DosLibrary *DOSBase;

/* ---- device list ----------------------------------------------------------------- */

static const char *const notfs[] = {
    "CON", "RAW", "SER", "PAR", "PRT", "NIL", "PIPE", "SPEAK", "AUX", "NEWCON", "KCON", "KRAW", NULL
};

static BOOL isfs(const char *name)
{
    int i;
    for (i = 0; notfs[i]; i++)
        if (!nocase_cmp(name, notfs[i]))
            return FALSE;
    return TRUE;
}

static void bstr(char *d, BSTR b)
{
    const UBYTE *p = BADDR(b);
    int i, n = p ? p[0] : 0;
    if (n > DEVNAME - 1)
        n = DEVNAME - 1;
    for (i = 0; i < n; i++)
        d[i] = p[1 + i];
    d[n] = 0;
}

static int devorder(const struct DevEntry *a, const struct DevEntry *b)
{
    if (a->kind != b->kind)
        return a->kind - b->kind;
    return nocase_cmp(a->name, b->name);
}

int dos_devices(struct DevEntry *out, int max)
{
    struct RootNode *root = (struct RootNode *)DOSBase->dl_Root;
    struct DosInfo *di = BADDR(root->rn_Info);
    struct DeviceList *dl;
    int n = 0, i, j;

    Forbid();   /* copy only: no DOS calls while the list is held */
    for (dl = BADDR(di->di_DevInfo); dl && n < max; dl = BADDR(dl->dl_Next)) {
        struct DevEntry *e = &out[n];
        bstr(e->name, dl->dl_Name);
        if (!e->name[0])
            continue;
        if (dl->dl_Type == DLT_DEVICE) {
            if (!isfs(e->name))
                continue;
            e->kind = DK_DEVICE;
        } else if (dl->dl_Type == DLT_VOLUME)
            e->kind = DK_VOLUME;
        else if (dl->dl_Type == DLT_DIRECTORY)
            e->kind = DK_ASSIGN;
        else
            continue;
        n++;
    }
    Permit();
    for (i = 1; i < n; i++) {           /* devices, then volumes, then assigns; by name */
        struct DevEntry t = out[i];
        for (j = i; j > 0 && devorder(&t, &out[j - 1]) < 0; j--)
            out[j] = out[j - 1];
        out[j] = t;
    }
    return n;
}

/* ---- DOS requesters ---------------------------------------------------------------- */

static APTR quietSaved, origWindow;
static int quietDepth;
static BOOL origSaved;

void dos_quiet(BOOL on)
{
    struct Process *me = (struct Process *)FindTask(NULL);
    if (on) {
        if (quietDepth++ == 0) {
            quietSaved = me->pr_WindowPtr;
            me->pr_WindowPtr = (APTR)-1;
        }
    } else if (quietDepth > 0 && --quietDepth == 0)
        me->pr_WindowPtr = quietSaved;
}

void dos_setwindow(APTR win)
{
    struct Process *me = (struct Process *)FindTask(NULL);
    if (!origSaved) {
        origWindow = me->pr_WindowPtr;
        origSaved = TRUE;
    }
    me->pr_WindowPtr = win ? win : origWindow;   /* NULL = restore the original */
}

/* ---- texts -------------------------------------------------------------------------- */

static const struct { UWORD code; const char *text; } errs[] = {
    { 103, "Not enough memory" },          { 105, "Task table full" },
    { 120, "Argument line invalid" },      { 121, "Not a program" },
    { 202, "Object in use" },              { 203, "Object already exists" },
    { 204, "Directory not found" },        { 205, "Object not found" },
    { 209, "Action not known by device" }, { 210, "Invalid name" },
    { 212, "Wrong type of object" },       { 213, "Disk not validated" },
    { 214, "Disk is write-protected" },    { 215, "Rename across devices" },
    { 216, "Directory not empty" },        { 218, "Device not mounted" },
    { 219, "Seek error" },                 { 220, "Comment too long" },
    { 221, "Disk is full" },               { 222, "Protected from deletion" },
    { 223, "Protected from writing" },     { 224, "Protected from reading" },
    { 225, "Not a DOS disk" },             { 226, "No disk in drive" },
    { 232, "No more entries" },            { 0, NULL }
};

const char *dos_errstr(LONG err)
{
    static char buf[24];
    int i;
    for (i = 0; errs[i].text; i++)
        if (errs[i].code == err)
            return errs[i].text;
    fmt(buf, "DOS error %ld", err);
    return buf;
}

void dos_datestr(char *buf, const struct DateStamp *ds)
{
    static const char *const mon[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    static const UBYTE mdays[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    LONG d = ds->ds_Days, y = 1978;
    int m = 0;
    for (;;) {
        LONG len = ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 366 : 365;
        if (d < len)
            break;
        d -= len;
        y++;
    }
    for (;;) {
        LONG len = mdays[m] + (m == 1 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0));
        if (d < len || m == 11)
            break;
        d -= len;
        m++;
    }
    fmt(buf, "%02ld-%s-%02ld %02ld:%02ld", d + 1, mon[m], y % 100,
        ds->ds_Minute / 60, ds->ds_Minute % 60);
}

void dos_protstr(char *buf, LONG prot)
{
    /* h s p a: set = on.  r w e d: stored inverted, set = NOT allowed. */
    static const char letters[] = "hsparwed";
    int i;
    for (i = 0; i < 8; i++) {
        int bit = 7 - i;
        BOOL on = (i < 4) ? (prot >> bit) & 1 : !((prot >> bit) & 1);
        buf[i] = on ? letters[i] : '-';
    }
    buf[8] = 0;
}

BOOL dos_space(BPTR lock, const char *path, ULONG *freek, char *volname)
{
    struct InfoData *id = AllocMem(sizeof(struct InfoData), MEMF_PUBLIC | MEMF_CLEAR);
    BOOL ok = FALSE;
    *freek = 0;
    volname[0] = 0;
    if (!id)
        return FALSE;
    if (Info(lock, id)) {
        struct DeviceList *vol = BADDR(id->id_VolumeNode);
        ULONG blocks = id->id_NumBlocks > id->id_NumBlocksUsed ? id->id_NumBlocks - id->id_NumBlocksUsed : 0;
        ULONG bpb = id->id_BytesPerBlock;
        /* in KB: bytes would overflow 32 bits on big (or emulated host) drives */
        *freek = bpb >= 1024 ? blocks * (bpb >> 10) : blocks / (bpb ? 1024 / bpb : 1);
        if (vol)
            bstr(volname, vol->dl_Name);
        /* The 1.3 RAM disk is always "full": its real limit is free memory. */
        if (!nocase_cmp(volname, "RAM DISK"))
            *freek = AvailMem(MEMF_PUBLIC) >> 10;
        ok = TRUE;
    }
    FreeMem(id, sizeof(struct InfoData));
    (void)path;
    return ok;
}

/* ---- packets (1.3 has no DoPkt) --------------------------------------------------------- */

LONG dos_packet(struct MsgPort *port, LONG type, LONG a1, LONG a2, LONG a3, LONG a4, LONG *res2)
{
    struct StandardPacket *sp = AllocMem(sizeof(*sp), MEMF_PUBLIC | MEMF_CLEAR);
    struct MsgPort *rp = AllocMem(sizeof(*rp), MEMF_PUBLIC | MEMF_CLEAR);
    BYTE sig = -1;
    LONG res1 = 0;
    *res2 = ERROR_NO_FREE_STORE;
    if (sp && rp && (sig = AllocSignal(-1)) >= 0) {
        struct List *l = &rp->mp_MsgList;
        rp->mp_Node.ln_Type = NT_MSGPORT;
        rp->mp_Flags = PA_SIGNAL;
        rp->mp_SigBit = sig;
        rp->mp_SigTask = FindTask(NULL);
        l->lh_Head = (struct Node *)&l->lh_Tail;       /* NewList() without amiga.lib */
        l->lh_Tail = NULL;
        l->lh_TailPred = (struct Node *)&l->lh_Head;
        sp->sp_Msg.mn_Node.ln_Name = (char *)&sp->sp_Pkt;
        sp->sp_Msg.mn_ReplyPort = rp;
        sp->sp_Pkt.dp_Link = &sp->sp_Msg;
        sp->sp_Pkt.dp_Port = rp;
        sp->sp_Pkt.dp_Type = type;
        sp->sp_Pkt.dp_Arg1 = a1;
        sp->sp_Pkt.dp_Arg2 = a2;
        sp->sp_Pkt.dp_Arg3 = a3;
        sp->sp_Pkt.dp_Arg4 = a4;
        PutMsg(port, &sp->sp_Msg);
        WaitPort(rp);
        GetMsg(rp);
        res1 = sp->sp_Pkt.dp_Res1;
        *res2 = sp->sp_Pkt.dp_Res2;
    }
    if (sig >= 0)
        FreeSignal(sig);
    if (rp)
        FreeMem(rp, sizeof(*rp));
    if (sp)
        FreeMem(sp, sizeof(*sp));
    return res1;
}

LONG dos_setdate(const char *path, const struct DateStamp *ds)
{
    static char dir[PATHMAX];        /* static: called from deep in a recursive copy */
    int i, cut = -1;
    UBYTE *bname;
    struct DateStamp *pds;
    BPTR lock;
    LONG err = 0, res2, n;

    for (i = 0; path[i]; i++)
        if (path[i] == ':' || path[i] == '/')
            cut = i;
    scopy(dir, path, PATHMAX);
    dir[cut < 0 ? 0 : (path[cut] == ':' ? cut + 1 : cut)] = 0;   /* keep "DH0:", drop "/" */
    n = slen(path + cut + 1);
    if (n > 255)
        return ERROR_INVALID_COMPONENT_NAME;
    /* the name as a BSTR and the date, both in longword-aligned public memory */
    bname = AllocMem(260 + sizeof(struct DateStamp), MEMF_PUBLIC | MEMF_CLEAR);
    if (!bname)
        return ERROR_NO_FREE_STORE;
    pds = (struct DateStamp *)(bname + 260);
    bname[0] = (UBYTE)n;
    CopyMem((APTR)(path + cut + 1), bname + 1, n);
    *pds = *ds;
    if ((lock = Lock((STRPTR)dir, ACCESS_READ))) {
        struct FileLock *fl = BADDR(lock);
        if (!dos_packet(fl->fl_Task, ACTION_SET_DATE, 0, lock, MKBADDR(bname), (LONG)pds, &res2))
            err = res2 ? res2 : ERROR_ACTION_NOT_KNOWN;
        UnLock(lock);
    } else
        err = IoErr();
    FreeMem(bname, 260 + sizeof(struct DateStamp));
    return err;
}

/* ---- paths ---------------------------------------------------------------------------- */

void path_add(char *path, const char *name)
{
    int n = slen(path);
    if (n && path[n - 1] != ':' && path[n - 1] != '/')
        scat(path, "/", PATHMAX);
    scat(path, name, PATHMAX);
}

static int colonpos(const char *path)
{
    int i;
    for (i = 0; path[i]; i++)
        if (path[i] == ':')
            return i;
    return -1;
}

BOOL path_isroot(const char *path)
{
    int c = colonpos(path);
    return c >= 0 && path[c + 1] == 0;
}

BOOL path_parent(char *path)
{
    int c = colonpos(path), i, cut = -1;
    for (i = c + 1; path[i]; i++)
        if (path[i] == '/')
            cut = i;
    if (cut >= 0) {
        path[cut] = 0;
        return TRUE;
    }
    if (c >= 0 && path[c + 1]) {
        path[c + 1] = 0;
        return TRUE;
    }
    return FALSE;
}

void path_root(char *path)
{
    int c = colonpos(path);
    if (c >= 0)
        path[c + 1] = 0;
}
