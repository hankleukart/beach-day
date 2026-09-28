#!/usr/bin/env bash
# Fetch the OFL-licensed variable TTFs the design uses. Only needed to
# regenerate src/fonts/ (the generated headers are committed).
set -euo pipefail
cd "$(dirname "$0")/fonts"
base=https://raw.githubusercontent.com/google/fonts/main/ofl
curl -sSfL -o ArchivoBlack-Regular.ttf         "$base/archivoblack/ArchivoBlack-Regular.ttf"
curl -sSfL -o AtkinsonHyperlegible-Bold.ttf    "$base/atkinsonhyperlegible/AtkinsonHyperlegible-Bold.ttf"
curl -sSfL -o AtkinsonHyperlegible-Regular.ttf "$base/atkinsonhyperlegible/AtkinsonHyperlegible-Regular.ttf"
curl -sSfL -o OFL-Archivo.txt                  "$base/archivoblack/OFL.txt"
curl -sSfL -o OFL-Atkinson.txt                 "$base/atkinsonhyperlegible/OFL.txt"
ls -la
