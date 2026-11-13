#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build=$(mktemp -d "${TMPDIR:-/tmp}/nxloader-cart-tests.XXXXXX")
trap 'rm -rf "$build"' EXIT HUP INT TERM
"${CXX:-c++}" -std=c++20 -O2 -fno-exceptions -Wall -Wextra -Werror -pthread \
    -I "$root/tests/nxloader/selection_stubs" -I "$root/tests/nxloader/browser_stubs" -I "$root/sphaira/include" \
    "$root/sphaira/source/nxloader/selection_size.cpp" "$root/sphaira/source/nxloader/selection_cart.cpp" \
    "$root/tests/nxloader/selection_cart_test.cpp" -o "$build/selection_cart_test"
"$build/selection_cart_test"
