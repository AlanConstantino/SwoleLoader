#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
temporary_root=${TMPDIR:-/tmp}
build=$(mktemp -d "${temporary_root%/}/nxloader-rate-tests.XXXXXX")
trap 'rm -rf "$build"' EXIT HUP INT TERM

"${CXX:-c++}" -std=c++20 -fno-exceptions -Wall -Wextra -Werror \
    -I "$root/sphaira/include" "$root/tests/nxloader/transfer_rate_test.cpp" \
    -o "$build/transfer_rate_test"
"$build/transfer_rate_test"

"${CXX:-c++}" -std=c++20 -fno-exceptions -Wall -Wextra -Werror \
    -I "$root/sphaira/include" "$root/tests/nxloader/batch_progress_test.cpp" \
    -o "$build/batch_progress_test"
"$build/batch_progress_test"
