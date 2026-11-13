#!/bin/sh
# Self-update tests. The signature test needs openssl and mbedtls 2.x: set
# MBEDTLS_DIR to an mbedtls source tree with library/libmbedcrypto.a built, or
# install mbedcrypto for pkg-config. Without either it is skipped.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
temporary_root=${TMPDIR:-/tmp}
build=$(mktemp -d "${temporary_root%/}/nxloader-self-update-tests.XXXXXX")
trap 'rm -rf "$build"' EXIT HUP INT TERM

"${CXX:-c++}" -std=c++20 -fno-exceptions -Wall -Wextra -Werror \
    -I "$root/sphaira/include" "$root/tests/nxloader/self_update_test.cpp" \
    -o "$build/self_update_test"
"$build/self_update_test"

if command -v cmake >/dev/null 2>&1; then
    cmake -P "$root/tests/nxloader/app_config_test.cmake"
else
    echo 'App config tests skipped: cmake not found.'
fi

if [ -n "${MBEDTLS_DIR:-}" ]; then
    mbedtls_flags="-I $MBEDTLS_DIR/include $MBEDTLS_DIR/library/libmbedcrypto.a"
elif pkg-config --exists mbedcrypto 2>/dev/null; then
    mbedtls_flags=$(pkg-config --cflags --libs mbedcrypto)
else
    echo 'Update signature tests skipped: set MBEDTLS_DIR or install mbedcrypto.'
    exit 0
fi

openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -out "$build/key.pem" 2>/dev/null
openssl pkey -in "$build/key.pem" -pubout -outform DER -out "$build/key.der"
openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -out "$build/other.pem" 2>/dev/null
openssl pkey -in "$build/other.pem" -pubout -outform DER -out "$build/other.der"
openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:2048 -out "$build/rsa.pem" 2>/dev/null
openssl pkey -in "$build/rsa.pem" -pubout -outform DER -out "$build/rsa.der"
head -c 300000 /dev/urandom > "$build/app.nro"
openssl dgst -sha256 -sign "$build/key.pem" -out "$build/app.nro.sig" "$build/app.nro"

# shellcheck disable=SC2086
"${CXX:-c++}" -std=c++20 -fno-exceptions -Wall -Wextra -Werror \
    -I "$root/sphaira/include" "$root/tests/nxloader/update_signature_test.cpp" \
    "$root/sphaira/source/nxloader/update_signature.cpp" $mbedtls_flags \
    -o "$build/update_signature_test"
"$build/update_signature_test" "$build/key.der" "$build/other.der" "$build/rsa.der" \
    "$build/app.nro" "$build/app.nro.sig"
