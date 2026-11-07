#!/bin/bash
# Build the Dowser release in dist/ (runs ./build.sh first):
#   Dowser.lha     Dowser.info               drawer icon
#                  Dowser/Dowser             the program        Dowser/Dowser.info          its icon
#                  Dowser/Dowser.readme      Aminet readme      Dowser/Dowser.readme.info   opens it with More
#   Dowser.readme  (Aminet wants it next to the archive)
#   dowser.adf     a disk that boots straight into Dowser (only Dowser's own files)
set -euo pipefail
cd "$(dirname "$0")"
./build.sh >/dev/null
rm -rf dist
mkdir -p dist/Dowser dist/disk
cp build/dowser dist/Dowser/Dowser
cp build/dowser.info dist/Dowser/Dowser.info
cp Dowser.readme dist/Dowser/Dowser.readme
python3 tools/mkicon.py --drawer art/drawer.txt dist/Dowser.info
python3 tools/mkicon.py --project SYS:Utilities/More art/readme-icon.txt dist/Dowser/Dowser.readme.info
printf 'dowser\n' > dist/disk/startup-sequence
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:/src" -w /src \
  "${AGK_TOOLCHAIN_IMAGE:-amigadev/crosstools:m68k-amigaos}" sh -euc '
  cd dist
  lha -aq Dowser.lha Dowser.info Dowser/Dowser Dowser/Dowser.info Dowser/Dowser.readme Dowser/Dowser.readme.info
  lha -l Dowser.lha
  xdftool dowser.adf format Dowser ofs + boot install + write Dowser/Dowser dowser + write Dowser/Dowser.info dowser.info \
    + write Dowser/Dowser.readme Dowser.readme + makedir s + write disk/startup-sequence s/startup-sequence >/dev/null
  xdftool dowser.adf list'
rm -rf dist/Dowser dist/Dowser.info dist/disk
cp Dowser.readme dist/
SHARE=${DOWSER_SHARE:-/mnt/j/amiga/share}
[ -d "$SHARE" ] && cp dist/Dowser.lha "$SHARE/"
ls -l dist
