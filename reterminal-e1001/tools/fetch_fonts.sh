#!/usr/bin/env bash
# Fetch the OFL-licensed variable TTFs the design uses. Only needed to
# regenerate src/fonts/ (the generated headers are committed).
set -euo pipefail
cd "$(dirname "$0")/fonts"
base=https://raw.githubusercontent.com/google/fonts/main/ofl
curl -sSfL -o Literata-Variable.ttf            "$base/literata/Literata%5Bopsz,wght%5D.ttf"
curl -sSfL -o OFL-Literata.txt                 "$base/literata/OFL.txt"
curl -sSfL -o Lexend-Variable.ttf              "$base/lexend/Lexend%5Bwght%5D.ttf"
curl -sSfL -o OFL-Lexend.txt                   "$base/lexend/OFL.txt"
curl -sSfL -o ArchivoBlack-Regular.ttf         "$base/archivoblack/ArchivoBlack-Regular.ttf"
curl -sSfL -o AtkinsonHyperlegible-Bold.ttf    "$base/atkinsonhyperlegible/AtkinsonHyperlegible-Bold.ttf"
curl -sSfL -o AtkinsonHyperlegible-Regular.ttf "$base/atkinsonhyperlegible/AtkinsonHyperlegible-Regular.ttf"
curl -sSfL -o OFL-Archivo.txt                  "$base/archivoblack/OFL.txt"
curl -sSfL -o OFL-Atkinson.txt                 "$base/atkinsonhyperlegible/OFL.txt"
ls -la
