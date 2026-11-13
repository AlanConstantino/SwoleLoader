#!/bin/sh
# Turns the nro+0x... addresses in a crash report into functions and source
# lines. Use the symbols .elf from the same version as the crash: the
# <folder>-<version>-symbols.zip in that release, or build/Release/sphaira/sphaira.elf
# for a local build.
#
# Usage: tools/symbolize.sh <symbols.elf> <crash.txt or log.txt>
set -eu

if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <symbols.elf> <crash.txt or log.txt>" >&2
    exit 2
fi

elf_dir=$(CDPATH= cd -- "$(dirname -- "$1")" && pwd)
elf_name=$(basename -- "$1")
# in report order, without repeats.
addresses=$(grep -o 'nro+0x[0-9a-fA-F]*' "$2" | sed 's/^nro+//' | awk '!seen[$0]++')
if [ -z "$addresses" ]; then
    echo "No nro+0x addresses found in $2." >&2
    exit 1
fi

# addr2line cannot read the line tables of this LTO build, so this uses
# llvm-symbolizer when installed, otherwise gdb from the build image.
for symbolizer in llvm-symbolizer /opt/homebrew/opt/llvm/bin/llvm-symbolizer /usr/local/opt/llvm/bin/llvm-symbolizer; do
    if command -v "$symbolizer" >/dev/null 2>&1; then
        # shellcheck disable=SC2086
        exec "$symbolizer" --obj="$elf_dir/$elf_name" --pretty-print --print-address --demangle $addresses
    fi
done

set --
for address in $addresses; do
    set -- "$@" -ex "echo \\n== nro+$address ==\\n" -ex "info symbol $address" -ex "info line *$address"
done

gdb=aarch64-none-elf-gdb
if command -v "$gdb" >/dev/null 2>&1; then
    exec "$gdb" -batch -nx "$@" "$elf_dir/$elf_name"
fi

if [ -n "${CONTAINER_ENGINE:-}" ]; then
    engine=$CONTAINER_ENGINE
elif command -v podman >/dev/null 2>&1; then
    engine=podman
else
    engine=docker
fi
image=localhost/nxloader-builder:local
if ! "$engine" image inspect "$image" >/dev/null 2>&1; then
    echo "No symbolizer found and the build image is missing; run tools/nxloader-build.sh once first." >&2
    exit 1
fi
exec "$engine" run --rm -v "$elf_dir:/symbols:ro" "$image" \
    /opt/devkitpro/devkitA64/bin/$gdb -batch -nx "$@" "/symbols/$elf_name"
