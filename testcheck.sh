#!/bin/bash
# Dowser disk-check test (milestone 2). Builds Dowser, makes the test images (tools/mkdisks.py)
# and damaged copies of them (tools/mkbroken.py), copies them to Work:dc ($DOWSER_SHARE/dc),
# and makes a boot disk (build/dc.adf) that runs "dowser CMD REPORT" (INFO, MAP, FILES, CHECK
# and BOOT) on each, plus CHECK and BOOT through the emulator's drives:
#   DF1: the "twice" image (two files sharing a block)    DF2: the 1.x boot block image (read-only)
# Then start WinUAE with $DOWSER_PLAY/dowser-dc-ks13.uae (or -ks31) and run tools/checkcheck.py
# when Work:dc-done.txt appears. Borrows Echo and FailAt (and the 1.x boot block, for comparison)
# from the Workbench 1.3 disk ($WB13_ADF); the images stay local.
set -euo pipefail
cd "$(dirname "$0")"
SHARE=${DOWSER_SHARE:-/mnt/j/amiga/share}
PLAY=${DOWSER_PLAY:-/mnt/j/amiga/play}
WB13_ADF=${WB13_ADF:-"/mnt/j/amiga/disks/Workbench v1.3.3 rev 34.34 (1990)(Commodore)(A500-A2000)(Disk 1 of 2)(Workbench).adf"}
IMG=${AGK_TOOLCHAIN_IMAGE:-amigadev/crosstools:m68k-amigaos}
./build.sh >/dev/null
rm -rf build/disks "$SHARE/dc" "$SHARE"/dc-*.txt
python3 tools/mkdisks.py build/disks >/dev/null
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:/src" -w /src "$IMG" sh -euc "sh build/disks/make.sh"
IMAGES=$(python3 tools/mkbroken.py build/disks "$WB13_ADF")
mkdir -p "$SHARE/dc"
cp build/disks/*.adf build/disks/expect-check.json "$SHARE/dc/"

{
  echo 'failat 21'
  echo 'dowser >Work:dc-mem1.txt CMD MEM'
  for n in $IMAGES; do echo "dowser >Work:dc-$n-report.txt CMD REPORT Work:dc/$n.adf"; done
  echo 'dowser >Work:dc-df1-check.txt CMD CHECK DF1:'
  echo 'dowser >Work:dc-df1-boot.txt CMD BOOT DF1:'
  echo 'dowser >Work:dc-df2-boot.txt CMD BOOT DF2:'
  echo 'dowser >Work:dc-mem2.txt CMD MEM'
  for n in c-twice c-extbad c-dircache; do echo "dowser >Work:dc-r2-$n.txt CMD CHECK Work:dc/$n.adf"; done
  echo 'dowser >Work:dc-mem3.txt CMD MEM'
  echo 'echo >Work:dc-done.txt "done"'
} > build/dc-startup

docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:/src" -v "$(dirname "$WB13_ADF"):/wb:ro" -w /src "$IMG" sh -euc "
  wb='/wb/$(basename "$WB13_ADF")'
  mkdir -p build/c
  for c in FailAt Echo; do xdftool \"\$wb\" read c/\$c build/c/\$c; done
  rm -f build/dc.adf
  xdftool build/dc.adf format DowserTest ofs + boot install + write build/dowser dowser \
    + makedir c + write build/c/FailAt c/FailAt + write build/c/Echo c/Echo \
    + makedir s + write build/dc-startup s/startup-sequence >/dev/null"
cp build/disks/c-twice.adf "$PLAY/dowser-df1.adf"
chmod u+w "$PLAY/dowser-df2.adf" 2>/dev/null || true
cp build/disks/c-boot13.adf "$PLAY/dowser-df2.adf"
chmod a-w "$PLAY/dowser-df2.adf"
cp build/dc.adf "$PLAY/dowser-dc.adf"
( cd "$SHARE/dc" && sha1sum *.adf; cd "$PLAY" && sha1sum dowser-df1.adf dowser-df2.adf ) > "$SHARE/dc/hashes.txt"
for k in ks13 ks31; do
  sed -e 's#^floppy0=.*#floppy0=J:\\amiga\\play\\dowser-dc.adf#' \
      -e 's#^nr_floppies=.*#nr_floppies=3#' \
      -e 's#^config_description=.*#config_description=Dowser disk-check test#' \
      "/mnt/j/amiga/presets/oasis-$k.uae" > "$PLAY/dowser-dc-$k.uae"
  printf '%s\n' 'floppy1=J:\amiga\play\dowser-df1.adf' 'floppy1type=0' \
    'floppy2=J:\amiga\play\dowser-df2.adf' 'floppy2type=0' >> "$PLAY/dowser-dc-$k.uae"
done
echo "$(wc -l < build/dc-startup) steps; ready: $PLAY/dowser-dc-ks13.uae, $PLAY/dowser-dc-ks31.uae"
