#!/usr/bin/env bash
# Make the key pair that signs firmware releases. Run once, ever.
#
#   tools/make-signing-key.sh
#
# The private key goes OUTSIDE the repo, to $BEACHDAY_SIGNING_KEY (default
# ~/.config/beachday/release-signing-key.pem). tools/release.sh signs every
# release with it. Back it up somewhere safe (a password manager): without it
# no board will accept another update over Wi-Fi, and each one has to be
# reflashed over USB with firmware carrying a new public key.
#
# The public key goes INTO the repo, keys/release-signing.pub.pem, and is
# compiled into the firmware. It is not secret.
set -euo pipefail
cd "$(dirname "$0")/.."

KEY="${BEACHDAY_SIGNING_KEY:-$HOME/.config/beachday/release-signing-key.pem}"
PUB=keys/release-signing.pub.pem

[ ! -e "$KEY" ] || { echo "A signing key already exists at $KEY. Not replacing it."; exit 1; }
if [ -e "$PUB" ]; then
  echo "$PUB already exists: the boards trust that key."
  echo "Replacing it means every board must be reflashed over USB. If that is"
  echo "really what you want, delete $PUB yourself and run this again."
  exit 1
fi

mkdir -p "$(dirname "$KEY")" keys
( umask 077; openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -out "$KEY" )
openssl pkey -in "$KEY" -pubout -out "$PUB"
echo "==> Private key: $KEY  (keep it, back it up, never commit it)"
echo "==> Public key:  $PUB  (commit this)"
