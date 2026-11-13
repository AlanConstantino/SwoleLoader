#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
temporary_root=${TMPDIR:-/tmp}
build=$(mktemp -d "${temporary_root%/}/nxloader-ncz-tests.XXXXXX")
trap 'rm -rf "$build"' EXIT HUP INT TERM

"${CXX:-c++}" -std=c++20 -fno-exceptions -Wall -Wextra -Werror \
    -I "$root/sphaira/include" "$root/tests/nxloader/ncz_lookup_test.cpp" \
    -o "$build/ncz_lookup_test"
"$build/ncz_lookup_test"

if ! command -v pkg-config >/dev/null 2>&1 || ! pkg-config --exists libzstd; then
    printf '%s\n' 'NCZ output tests require pkg-config and libzstd development files.' >&2
    exit 1
fi
# pkg-config intentionally supplies separate compiler/linker flags.
"${CXX:-c++}" -std=c++20 -fno-exceptions -Wall -Wextra -Werror \
    -I "$root/sphaira/include" $(pkg-config --cflags libzstd) \
    "$root/tests/nxloader/ncz_output_test.cpp" $(pkg-config --libs libzstd) \
    -o "$build/ncz_output_test"
"$build/ncz_output_test"
