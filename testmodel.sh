#!/bin/bash
# Dowser disk-model test (milestone 1). Builds Dowser, makes test images (tools/mkdisks.py) in
# Work:dm ($DOWSER_SHARE/dm), and a boot disk (build/dm.adf) that runs "dowser CMD" INFO / MAP /
# FILES / BLOCK on each image and on the emulator's drives, writing Work:dm-*.txt:
#   DF1: the FFS test image (J:\amiga\play\dowser-df1.adf)
#   DF2: the OFS international image, read-only on the PC (write-protected)
#   DF3: no disk
# Then start WinUAE with $DOWSER_PLAY/dowser-dm-ks13.uae (or -ks31) and run tools/checkmodel.py
# when Work:dm-done.txt appears. Borrows Echo and FailAt from the Workbench 1.3 disk ($WB13_ADF).
set -euo pipefail
cd "$(dirname "$0")"
SHARE=${DOWSER_SHARE:-/mnt/j/amiga/share}
PLAY=${DOWSER_PLAY:-/mnt/j/amiga/play}
WB13_ADF=${WB13_ADF:-"/mnt/j/amiga/disks/Workbench v1.3.3 rev 34.34 (1990)(Commodore)(A500-A2000)(Disk 1 of 2)(Workbench).adf"}
IMG=${AGK_TOOLCHAIN_IMAGE:-amigadev/crosstools:m68k-amigaos}
./build.sh >/dev/null
rm -rf build/disks "$SHARE/dm" "$SHARE"/dm-*.txt
IMAGES=$(python3 tools/mkdisks.py build/disks)
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:/src" -w /src "$IMG" sh -euc "sh build/disks/make.sh"
mkdir -p "$SHARE/dm"
cp build/disks/*.adf build/disks/*.list "$SHARE/dm/"

{
  echo 'failat 21'
  echo 'dowser >Work:dm-mem1.txt CMD MEM'
  for n in $IMAGES raw-random raw-kick; do
    echo "dowser >Work:dm-$n-report.txt CMD REPORT Work:dm/$n.adf"
  done
  echo 'dowser >Work:dm-b0.txt CMD BLOCK Work:dm/ofs.adf 0'
  echo 'dowser >Work:dm-b880.txt CMD BLOCK Work:dm/ofs.adf 880'
  for c in INFO MAP FILES; do echo "dowser >Work:dm-df1-$(echo $c | tr A-Z a-z).txt CMD $c DF1:"; done
  echo 'dowser >Work:dm-df1-b880.txt CMD BLOCK DF1: 880'
  echo 'dowser >Work:dm-df2-info.txt CMD INFO DF2:'
  echo 'dowser >Work:dm-df2-files.txt CMD FILES DF2:'
  echo 'dowser >Work:dm-df3-info.txt CMD INFO DF3:'
  echo 'dowser >Work:dm-none-info.txt CMD INFO Work:dm/nothing.adf'
  echo 'dowser >Work:dm-short-info.txt CMD INFO Work:dm/ofs.list'
  echo 'dowser >Work:dm-mem2.txt CMD MEM'
  for n in ofs ffsdc frag; do echo "dowser >Work:dm-r2-$n.txt CMD FILES Work:dm/$n.adf"; done
  echo 'dowser >Work:dm-mem3.txt CMD MEM'
  # drives on their own: the first use can cost the system something, repeats must not
  for i in 1 2 3 4; do
    echo "dowser >Work:dm-r2-df1-$i.txt CMD FILES DF1:"
    echo "dowser >Work:dm-memd$i.txt CMD MEM"
  done
  echo 'echo >Work:dm-done.txt "done"'
} > build/dm-startup

docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:/src" -v "$(dirname "$WB13_ADF"):/wb:ro" -w /src "$IMG" sh -euc "
  wb='/wb/$(basename "$WB13_ADF")'
  mkdir -p build/c
  for c in FailAt Echo; do xdftool \"\$wb\" read c/\$c build/c/\$c; done
  rm -f build/dm.adf
  xdftool build/dm.adf format DowserTest ofs + boot install + write build/dowser dowser \
    + makedir c + write build/c/FailAt c/FailAt + write build/c/Echo c/Echo \
    + makedir s + write build/dm-startup s/startup-sequence >/dev/null"

# the drives' disks: DF1 the FFS image, DF2 the international OFS image (read-only on the PC)
cp build/disks/ffs.adf "$PLAY/dowser-df1.adf"
chmod u+w "$PLAY/dowser-df2.adf" 2>/dev/null || true
cp build/disks/ofsintl.adf "$PLAY/dowser-df2.adf"
chmod a-w "$PLAY/dowser-df2.adf"
cp build/dm.adf "$PLAY/dowser-dm.adf"
# fingerprints of every image Dowser will read, to prove afterwards that it wrote nothing
( cd "$SHARE/dm" && sha1sum *.adf; cd "$PLAY" && sha1sum dowser-df1.adf dowser-df2.adf ) > "$SHARE/dm/hashes.txt"
for k in ks13 ks31; do
  sed -e 's#^floppy0=.*#floppy0=J:\\amiga\\play\\dowser-dm.adf#' \
      -e 's#^nr_floppies=.*#nr_floppies=4#' \
      -e 's#^config_description=.*#config_description=Dowser disk-model test#' \
      "/mnt/j/amiga/presets/oasis-$k.uae" > "$PLAY/dowser-dm-$k.uae"
  printf '%s\n' 'floppy1=J:\amiga\play\dowser-df1.adf' 'floppy1type=0' \
    'floppy2=J:\amiga\play\dowser-df2.adf' 'floppy2type=0' 'floppy3=' 'floppy3type=0' >> "$PLAY/dowser-dm-$k.uae"
done
echo "$(wc -l < build/dm-startup) steps; ready: $PLAY/dowser-dm-ks13.uae, $PLAY/dowser-dm-ks31.uae"
