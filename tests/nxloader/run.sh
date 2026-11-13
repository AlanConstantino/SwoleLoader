#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
temporary_root=${TMPDIR:-/tmp}
build=$(mktemp -d "${temporary_root%/}/nxloader-tests.XXXXXX")
trap 'rm -rf "$build"' EXIT HUP INT TERM

"${CXX:-c++}" -std=c++20 -fno-exceptions -Wall -Wextra -Werror \
    -I "$root/sphaira/include" "$root/tests/nxloader/package_scan_test.cpp" \
    -o "$build/package_scan_test"
"$build/package_scan_test"

"${CXX:-c++}" -std=c++20 -fno-exceptions -Wall -Wextra -Werror \
    -I "$root/sphaira/include" "$root/tests/nxloader/batch_review_test.cpp" \
    -o "$build/batch_review_test"
"$build/batch_review_test"

"${CXX:-c++}" -std=c++20 -fno-exceptions -Wall -Wextra -Werror \
    -I "$root/sphaira/include" "$root/tests/nxloader/install_metrics_test.cpp" \
    -o "$build/install_metrics_test"
"$build/install_metrics_test"

"${CXX:-c++}" -std=c++20 -fno-exceptions -Wall -Wextra -Werror \
    -I "$root/sphaira/include" "$root/tests/nxloader/scan_directory.cpp" \
    -o "$build/scan_directory"

mkdir -p "$build/fixture/Game A/Update" "$build/fixture/Game B" "$build/fixture/.hidden"
touch "$build/fixture/Game A/base.NSP" "$build/fixture/Game A/Update/patch.nsz" \
    "$build/fixture/Game B/other.nsp" "$build/fixture/Game B/._other.nsp" \
    "$build/fixture/.hidden/hidden.nsp"
ln -s "$build/fixture" "$build/fixture/Game A/cycle"
"$build/scan_directory" "$build/fixture" "$build/fixture/Game A" > "$build/actual"
cat > "$build/expected" <<EOF
$build/fixture/Game A/Update/patch.nsz
$build/fixture/Game A/base.NSP
$build/fixture/Game B/other.nsp
EOF
diff -u "$build/expected" "$build/actual"
printf '%s\n' 'Filesystem adapter test passed.'

sh "$root/tests/nxloader/run_lists.sh"
sh "$root/tests/nxloader/run_ncz.sh"
sh "$root/tests/nxloader/run_browser.sh"
sh "$root/tests/nxloader/run_transfer_rate.sh"
sh "$root/tests/nxloader/run_footer.sh"
sh "$root/tests/nxloader/run_package_size.sh"
sh "$root/tests/nxloader/run_capacity.sh"
sh "$root/tests/nxloader/run_selection_size.sh"

sh "$root/tests/nxloader/run_cart.sh"

sh "$root/tests/nxloader/run_self_update.sh"

sh "$root/tests/nxloader/run_power.sh"
