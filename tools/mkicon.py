#!/usr/bin/env python3
"""Write a Workbench 1.x icon (.info) from ASCII art: a tool, drawer or project icon.

Usage: mkicon.py [--drawer | --project DEFAULTTOOL] ART.txt OUT.info [STACK]
  (default)   tool icon (WBTOOL), STACK = stack size for the program (default 4096)
  --drawer    drawer icon (WBDRAWER), with DrawerData for the drawer's window
  --project   project icon (WBPROJECT) opened with DEFAULTTOOL, e.g. SYS:Utilities/More
ART.txt: equal-length rows; lines starting with '#' and blank lines are ignored.
Pixel key, 1.3 Workbench palette: . blue (0)  W white (1)  K black (2)  O orange (3).

File layout: DiskObject (78 bytes) [+ DrawerData (56)] + Image header (20) + planes
[+ DefaultTool string as ULONG length incl. NUL, then the bytes].
"""
import argparse
import struct

PEN = {".": 0, "W": 1, "K": 2, "O": 3}
WBDRAWER, WBTOOL, WBPROJECT = 2, 3, 4
NO_ICON_POSITION = 0x80000000
GADGIMAGE, RELVERIFY_GADGIMMEDIATE, BOOLGADGET = 0x0004, 0x0003, 0x0001


def load_art(path):
    rows = [line.rstrip("\r\n") for line in open(path)]
    return [r for r in rows if r and not r.startswith("#")]


def planes(art, depth=2):
    w = len(art[0])
    assert all(len(row) == w for row in art), "ragged icon art"
    words = (w + 15) // 16
    out = b""
    for p in range(depth):
        for row in art:
            bits = 0
            for x in range(words * 16):
                pen = PEN[row[x]] if x < w else 0
                bits = (bits << 1) | ((pen >> p) & 1)
            out += bits.to_bytes(words * 2, "big")
    return out


def drawerdata(left=40, top=30, width=320, height=110):
    """56-byte DrawerData: a NewWindow for the drawer's window plus its scroll offsets.
    Pens, IDCMP/Flags, limits and Type match Workbench 1.3's own Utilities.info;
    the pointer fields are meaningless on disk and left 0."""
    nw = struct.pack(">hhhhBBIIIIIIIhhhhH",
                     left, top, width, height, 0xFF, 0xFF,
                     0, 0x0240027F,                          # IDCMPFlags, Flags (as Workbench writes them)
                     0, 0, 0, 0, 0,                          # FirstGadget, CheckMark, Title, Screen, BitMap
                     90, 40, -1, -1,                         # Min/Max size
                     1)                                      # Type = WBENCHSCREEN
    assert len(nw) == 48
    return nw + struct.pack(">ii", 0, 0)                     # dd_CurrentX/Y: scroll position


def icon(art, kind=WBTOOL, stack=4096, deftool=None):
    w, h = len(art[0]), len(art)
    gadget = struct.pack(">IhhhhHHHIIIiIHI",
                         0, 0, 0, w, h,                      # NextGadget, Left, Top, Width, Height
                         GADGIMAGE, RELVERIFY_GADGIMMEDIATE, BOOLGADGET,
                         1, 0, 0,                            # GadgetRender (non-NULL = image follows), Select, Text
                         0, 0, 0, 0)                         # MutualExclude, SpecialInfo, GadgetID, UserData (rev 0 = 1.x icon)
    assert len(gadget) == 44
    disk = struct.pack(">HH", 0xE310, 1) + gadget + struct.pack(
        ">BBIIIIIII", kind, 0,
        1 if deftool else 0, 0,                              # DefaultTool (non-NULL = string follows), ToolTypes
        NO_ICON_POSITION, NO_ICON_POSITION,                  # CurrentX, CurrentY: let Workbench place it
        1 if kind == WBDRAWER else 0, 0,                     # DrawerData (non-NULL = follows), ToolWindow
        0 if kind == WBDRAWER else stack)                    # StackSize (projects: for their tool)
    assert len(disk) == 78
    if kind == WBDRAWER:
        disk += drawerdata()
    image = struct.pack(">hhhhhIBBI", 0, 0, w, h, 2, 1, 0b11, 0, 0)  # 2 planes, PlanePick 3
    assert len(image) == 20
    out = disk + image + planes(art)
    if deftool:
        s = deftool.encode("latin-1") + b"\0"
        out += struct.pack(">I", len(s)) + s
    return out


def main():
    ap = argparse.ArgumentParser(description="Write a Workbench 1.x .info file from ASCII art.")
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--drawer", action="store_true", help="drawer icon")
    g.add_argument("--project", metavar="DEFAULTTOOL", help="project icon opened with DEFAULTTOOL")
    ap.add_argument("art")
    ap.add_argument("out")
    ap.add_argument("stack", nargs="?", type=int, default=4096)
    a = ap.parse_args()
    kind = WBDRAWER if a.drawer else WBPROJECT if a.project else WBTOOL
    with open(a.out, "wb") as f:
        f.write(icon(load_art(a.art), kind, a.stack, a.project))


if __name__ == "__main__":
    main()
