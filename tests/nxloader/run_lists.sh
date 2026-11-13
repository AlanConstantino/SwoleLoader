#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
temporary_root=${TMPDIR:-/tmp}
build=$(mktemp -d "${temporary_root%/}/nxloader-lists.XXXXXX")
trap 'rm -rf "$build"' EXIT HUP INT TERM

"${CXX:-c++}" -std=c++20 -O2 -fno-exceptions -Wall -Wextra -Werror \
    -I "$root/sphaira/include" "$root/tests/nxloader/list_visible_test.cpp" \
    -o "$build/list_visible_test"
"$build/list_visible_test"

if [ "${1:-}" = "--benchmark" ]; then
    "${CXX:-c++}" -std=c++20 -O2 -fno-exceptions -Wall -Wextra -Werror \
        -I "$root/sphaira/include" "$root/tests/nxloader/list_benchmark.cpp" \
        -o "$build/list_benchmark"
    "$build/list_benchmark"
fi
