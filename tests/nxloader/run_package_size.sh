#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build=$(mktemp -d "${TMPDIR:-/tmp}/nxloader-package-size.XXXXXX")
trap 'rm -rf "$build"' EXIT HUP INT TERM
"${CXX:-c++}" -std=c++20 -fno-exceptions -Wall -Wextra -Werror \
    -I "$root/sphaira/include" "$root/tests/nxloader/package_size_test.cpp" \
    -o "$build/package_size_test"
"$build/package_size_test"
