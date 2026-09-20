#!/usr/bin/env bash
# Fetch the OFL-licensed variable TTFs the design uses. Only needed to
# regenerate src/fonts/ (the generated headers are committed).
set -euo pipefail
cd "$(dirname "$0")/fonts"
base=https://raw.githubusercontent.com/google/fonts/main/ofl
curl -sSfL -o Fraunces-Variable.ttf "$base/fraunces/Fraunces%5BSOFT%2CWONK%2Copsz%2Cwght%5D.ttf"
curl -sSfL -o Nunito-Variable.ttf   "$base/nunito/Nunito%5Bwght%5D.ttf"
curl -sSfL -o OFL-Fraunces.txt      "$base/fraunces/OFL.txt"
curl -sSfL -o OFL-Nunito.txt        "$base/nunito/OFL.txt"
ls -la
