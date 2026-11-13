#!/bin/sh
# Creates the key pair that signs over-the-air updates.
#
# The private key stays outside the repository. The public key is embedded in
# the app, which rejects any update that this private key did not sign. Keep a
# backup of the private key: without it, existing installs cannot be updated
# over the air and have to be reinstalled by hand.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
key=${NXLOADER_SIGNING_KEY:-$HOME/.config/nxloader-update/signing-key.pem}
public="$root/assets/embed/update_public_key.der"

if [ -e "$key" ]; then
    echo "Using existing signing key: $key"
else
    (
        umask 077
        mkdir -p "$(dirname -- "$key")"
        openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -out "$key"
    )
    echo "Created signing key: $key"
fi

openssl pkey -in "$key" -pubout -outform DER -out "$public"
echo "Wrote public key: $public"
