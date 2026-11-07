#!/bin/bash
# Dowser window screenshots: testgui.sh "ARGS"  (e.g. "Work:dm/frag.adf FILES FILE=fragmented.bin")
# Makes a boot disk whose startup-sequence is "dowser ARGS" and the presets dowser-gui-ks13/ks31
# (DF1: holds the FFS test image). Needs the images from testmodel.sh (Work:dm) and
# testcheck.sh (Work:dc). The disk has only Dowser's own files.
set -euo pipefail
cd "$(dirname "$0")"
PLAY=${DOWSER_PLAY:-/mnt/j/amiga/play}
./build.sh >/dev/null
printf 'dowser %s\n' "${1:-}" > build/gui-startup
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:/src" -w /src \
  "${AGK_TOOLCHAIN_IMAGE:-amigadev/crosstools:m68k-amigaos}" sh -euc "
  rm -f build/gui.adf
  xdftool build/gui.adf format DowserGUI ofs + boot install + write build/dowser dowser \
    + write build/dowser.info dowser.info + makedir s + write build/gui-startup s/startup-sequence >/dev/null"
cp build/gui.adf "$PLAY/dowser-gui.adf"
DF1=${DF1:-build/disks/ffs.adf}                 # the disk in DF1: (DF1=... to choose another)
if [ -f "$DF1" ]; then chmod u+w "$PLAY/dowser-gui-df1.adf" 2>/dev/null || true; cp "$DF1" "$PLAY/dowser-gui-df1.adf"; fi
for k in ks13 ks31; do
  sed -e 's#^floppy0=.*#floppy0=J:\\amiga\\play\\dowser-gui.adf#' \
      -e 's#^nr_floppies=.*#nr_floppies=2#' \
      -e 's#^config_description=.*#config_description=Dowser window#' \
      "/mnt/j/amiga/presets/oasis-$k.uae" > "$PLAY/dowser-gui-$k.uae"
  printf '%s\n' 'floppy1=J:\amiga\play\dowser-gui-df1.adf' 'floppy1type=0' >> "$PLAY/dowser-gui-$k.uae"
done
echo "ready: dowser ${1:-}"
