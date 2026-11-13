#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
temporary_root=${TMPDIR:-/tmp}
build=$(mktemp -d "${temporary_root%/}/nxloader-footer-tests.XXXXXX")
trap 'rm -rf "$build"' EXIT HUP INT TERM
"${CXX:-c++}" -std=c++20 -fno-exceptions -Wall -Wextra -Werror \
    -I "$root/sphaira/include" "$root/tests/nxloader/footer_layout_test.cpp" \
    -o "$build/footer_layout_test"
"$build/footer_layout_test"
