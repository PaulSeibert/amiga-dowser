#!/bin/bash
# Build Dowser (AmigaOS 1.3+, 68000) into build/:
#   build/dowser       the program        build/dowser.info  its Workbench icon (once art/icon.txt exists)
# Needs Docker (amigadev/crosstools:m68k-amigaos: bebbo GCC + xdftool) and Python 3.
# If $DOWSER_SHARE (default /mnt/j/amiga/share, the emulator's Work: folder) exists,
# the program is copied there too.
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build
python3 tools/mkicon.py art/icon.txt build/dowser.info 8192
python3 tools/mkimage.py art/logo.txt logo build/logo.c
SRC="src/main.c src/ui.c src/cmd.c src/disk.c src/fs.c src/check.c src/boot.c src/blockview.c src/view.c src/req.c src/about.c src/dos13.c src/util.c build/logo.c"
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:/src" -w /src \
  "${AGK_TOOLCHAIN_IMAGE:-amigadev/crosstools:m68k-amigaos}" sh -euc "
  m68k-amigaos-gcc -mcrt=nix13 -m68000 -Os -Wall -Wextra -Wno-unused-parameter -s -Isrc \
    ${DOWSER_CFLAGS:-} -o build/dowser $SRC"
SHARE=${DOWSER_SHARE:-/mnt/j/amiga/share}
if [ -d "$SHARE" ]; then cp build/dowser build/dowser.info "$SHARE/"; fi
ls -l build/dowser
