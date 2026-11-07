#!/bin/bash
# Dowser recovery test (milestone 4). Builds Dowser, makes the test images (mkdisks.py, then
# mkdeleted.py for deleted files, a lost root, a lost boot block, a damaged data block and an
# extended ADF with unreadable tracks) in Work:rs ($DOWSER_SHARE/rs), and a boot disk
# (build/rs.adf) that runs "dowser CMD" DELETED / RESCUE / SCAN on them and on the drives:
#   DF1: the extended ADF with two unreadable tracks   DF2: the OFS image (read-only)   DF3: empty
# Rescued files go to Work:rs/o-*. Then start WinUAE with $DOWSER_PLAY/dowser-rs-ks13.uae (or
# -ks31) and run tools/checkrescue.py when Work:rs-done.txt appears. Borrows Echo and FailAt
# from the Workbench 1.3 disk ($WB13_ADF); the test disks stay local.
set -euo pipefail
cd "$(dirname "$0")"
SHARE=${DOWSER_SHARE:-/mnt/j/amiga/share}
PLAY=${DOWSER_PLAY:-/mnt/j/amiga/play}
WB13_ADF=${WB13_ADF:-"/mnt/j/amiga/disks/Workbench v1.3.3 rev 34.34 (1990)(Commodore)(A500-A2000)(Disk 1 of 2)(Workbench).adf"}
IMG=${AGK_TOOLCHAIN_IMAGE:-amigadev/crosstools:m68k-amigaos}
./build.sh >/dev/null
rm -rf build/disks "$SHARE/rs" "$SHARE"/rs-*.txt
python3 tools/mkdisks.py build/disks >/dev/null
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:/src" -w /src "$IMG" sh -euc "sh build/disks/make.sh"
python3 tools/mkdeleted.py build/disks >/dev/null
mkdir -p "$SHARE/rs"
cp build/disks/ofs.adf build/disks/ffs.adf build/disks/ofsintl.adf build/disks/r-*.adf build/disks/expect-rescue.json "$SHARE/rs/"
for o in o-d1 o-d2 o-d2b o-d3 o-d4 o-noroot o-noboot o-dmg o-ffs o-intl o-sub o-tracks o-tracksb; do mkdir -p "$SHARE/rs/$o"; done

cat > build/rs-startup <<'EOS'
failat 21
dowser >Work:rs-mem1.txt CMD MEM
dowser >Work:rs-delofs-del.txt CMD DELETED Work:rs/r-delofs.adf
dowser >Work:rs-delffs-del.txt CMD DELETED Work:rs/r-delffs.adf
dowser >Work:rs-s1.txt CMD RESCUE Work:rs/r-delofs.adf DELETED readme TO Work:rs/o-d1
dowser >Work:rs-s2.txt CMD RESCUE Work:rs/r-delofs.adf DELETED ALL TO Work:rs/o-d2
dowser >Work:rs-s3.txt CMD RESCUE Work:rs/r-delffs.adf DELETED ALL TO Work:rs/o-d3
dowser >Work:rs-over-del.txt CMD DELETED Work:rs/r-over.adf
dowser >Work:rs-s4.txt CMD RESCUE Work:rs/r-over.adf DELETED big.bin TO Work:rs/o-d4
dowser >Work:rs-noroot-del.txt CMD DELETED Work:rs/r-noroot.adf
dowser >Work:rs-s5.txt CMD RESCUE Work:rs/r-noroot.adf DELETED ALL TO Work:rs/o-noroot
dowser >Work:rs-noboot.txt CMD REPORT Work:rs/r-noboot.adf
dowser >Work:rs-s6.txt CMD RESCUE Work:rs/r-noboot.adf ALL TO Work:rs/o-noboot
dowser >Work:rs-s7.txt CMD RESCUE Work:rs/r-dmg.adf readme TO Work:rs/o-dmg
dowser >Work:rs-s8.txt CMD RESCUE Work:rs/ffs.adf ALL TO Work:rs/o-ffs
dowser >Work:rs-s9.txt CMD RESCUE Work:rs/ofsintl.adf ALL TO Work:rs/o-intl
dowser >Work:rs-s10.txt CMD RESCUE Work:rs/ffs.adf sub TO Work:rs/o-sub
dowser >Work:rs-s11.txt CMD RESCUE Work:rs/ffs.adf ALL TO Work:rs/nothere
dowser >Work:rs-scanimg.txt CMD SCAN Work:rs/ffs.adf
dowser >Work:rs-scan1.txt CMD SCAN DF1:
dowser >Work:rs-files1.txt CMD FILES DF1:
dowser >Work:rs-s12.txt CMD RESCUE DF1: ALL TO Work:rs/o-tracks
dowser >Work:rs-scan2.txt CMD SCAN DF2:
dowser >Work:rs-scan3.txt CMD SCAN DF3:
dowser >Work:rs-s13.txt CMD RESCUE DF2: ALL TO DF2:
dowser >Work:rs-mem2.txt CMD MEM
dowser >Work:rs-r2a.txt CMD RESCUE Work:rs/r-delofs.adf DELETED ALL TO Work:rs/o-d2b
dowser >Work:rs-r2b.txt CMD RESCUE DF1: ALL TO Work:rs/o-tracksb
dowser >Work:rs-mem3.txt CMD MEM
echo >Work:rs-done.txt "done"
EOS
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:/src" -v "$(dirname "$WB13_ADF"):/wb:ro" -w /src "$IMG" sh -euc "
  wb='/wb/$(basename "$WB13_ADF")'
  mkdir -p build/c
  for c in FailAt Echo; do xdftool \"\$wb\" read c/\$c build/c/\$c; done
  rm -f build/rs.adf
  xdftool build/rs.adf format DowserTest ofs + boot install + write build/dowser dowser \
    + makedir c + write build/c/FailAt c/FailAt + write build/c/Echo c/Echo \
    + makedir s + write build/rs-startup s/startup-sequence >/dev/null"
chmod u+w "$PLAY/dowser-rs-df1.adf" "$PLAY/dowser-rs-df2.adf" 2>/dev/null || true
cp build/disks/r-tracks.adf "$PLAY/dowser-rs-df1.adf"
cp build/disks/ofs.adf "$PLAY/dowser-rs-df2.adf"    # OFS: Kickstart 1.3 reads it too
chmod a-w "$PLAY/dowser-rs-df2.adf"
cp build/rs.adf "$PLAY/dowser-rs.adf"
( cd "$SHARE/rs" && sha1sum *.adf; cd "$PLAY" && sha1sum dowser-rs-df1.adf dowser-rs-df2.adf ) > "$SHARE/rs/hashes.txt"
for k in ks13 ks31; do
  sed -e 's#^floppy0=.*#floppy0=J:\\amiga\\play\\dowser-rs.adf#' \
      -e 's#^nr_floppies=.*#nr_floppies=4#' \
      -e 's#^config_description=.*#config_description=Dowser recovery test#' \
      "/mnt/j/amiga/presets/oasis-$k.uae" > "$PLAY/dowser-rs-$k.uae"
  printf '%s\n' 'floppy1=J:\amiga\play\dowser-rs-df1.adf' 'floppy1type=0' \
    'floppy2=J:\amiga\play\dowser-rs-df2.adf' 'floppy2type=0' 'floppy3=' 'floppy3type=0' >> "$PLAY/dowser-rs-$k.uae"
done
echo "$(wc -l < build/rs-startup) steps; ready: $PLAY/dowser-rs-ks13.uae, $PLAY/dowser-rs-ks31.uae"
