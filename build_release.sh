#!/bin/sh
# Builds a release into dist/:
#   <APP_NRO>                   the app, downloaded by over-the-air updates
#   <APP_NRO>.sig               its signature, when the signing key is present
#   <APP_FOLDER>-<version>.zip  for installing by hand
#   <APP_FOLDER>-<version>-symbols.zip
#                               debug symbols, for tools/symbolize.sh
#
# The signing key defaults to ~/.config/nxloader-update/signing-key.pem and can
# be moved with NXLOADER_SIGNING_KEY. Set NXLOADER_REQUIRE_SIGNATURE=1 to fail
# instead of skipping the signature when the key is missing.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
# The build copies assets/romfs over its own copy without deleting anything,
# so start that copy fresh or removed assets would still ship.
rm -rf "$root/build/Release/sphaira/romfs"
"$root/tools/nxloader-build.sh" Release

build="$root/build/Release"
value() {
    sed -n "s/^$1=//p" "$build/release.env"
}
version=$(value APP_VERSION)
folder=$(value APP_FOLDER)
nro=$(value APP_NRO)
key=${NXLOADER_SIGNING_KEY:-$HOME/.config/nxloader-update/signing-key.pem}
public_key="$root/assets/embed/update_public_key.der"
dist="$root/dist"

mkdir -p "$dist"
rm -f "$dist/$nro" "$dist/$nro.sig"
package_dir=$(mktemp -d "${TMPDIR:-/tmp}/nxloader-package.XXXXXX")
trap 'rm -rf "$package_dir"' EXIT HUP INT TERM
(cd "$build" && zip -q "$package_dir/package.zip" "switch/$folder/$nro")
mv "$package_dir/package.zip" "$dist/$folder-$version.zip"
cp "$build/$nro" "$dist/$nro"
cp "$build/sphaira/sphaira.elf" "$package_dir/$folder-$version.elf"
(cd "$package_dir" && zip -q -9 symbols.zip "$folder-$version.elf")
mv "$package_dir/symbols.zip" "$dist/$folder-$version-symbols.zip"

if [ -f "$key" ]; then
    openssl dgst -sha256 -sign "$key" -out "$dist/$nro.sig" "$dist/$nro"
    # The app only accepts the key embedded in it, so check against that one.
    if ! openssl dgst -sha256 -verify "$public_key" -keyform DER \
        -signature "$dist/$nro.sig" "$dist/$nro" >/dev/null; then
        rm -f "$dist/$nro.sig"
        echo "error: $key does not match $public_key." >&2
        exit 1
    fi
    echo "Signed $nro."
elif [ -n "${NXLOADER_REQUIRE_SIGNATURE:-}" ]; then
    echo "error: signing key not found: $key" >&2
    exit 1
else
    echo "warning: signing key not found ($key). This build can be installed by hand," >&2
    echo "but the in-app updater will not accept it." >&2
fi

echo "Release $version written to $dist."
