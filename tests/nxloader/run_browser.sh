#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
temporary_root=${TMPDIR:-/tmp}
build=$(mktemp -d "${temporary_root%/}/nxloader-browser-tests.XXXXXX")
trap 'rm -rf "$build"' EXIT HUP INT TERM
"${CXX:-c++}" -std=c++20 -O2 -fno-exceptions -Wall -Wextra -Werror -pthread \
    -I "$root/tests/nxloader/browser_stubs" -I "$root/sphaira/include" \
    "$root/sphaira/source/nxloader/browser.cpp" "$root/tests/nxloader/browser_worker_test.cpp" \
    -o "$build/browser_worker_test"
"$build/browser_worker_test"
