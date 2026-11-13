#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
preset=${1:-Dev}
case "$preset" in
    Dev|Release) ;;
    *) echo "Usage: $0 [Dev|Release]" >&2; exit 2 ;;
esac

if [ -n "${CONTAINER_ENGINE:-}" ]; then
    engine=$CONTAINER_ENGINE
elif command -v podman >/dev/null 2>&1; then
    engine=podman
else
    engine=docker
fi

jobs=${NXLOADER_BUILD_JOBS:-4}
case "$jobs" in
    ''|*[!0-9]*|0) echo 'NXLOADER_BUILD_JOBS must be a positive integer.' >&2; exit 2 ;;
esac

image=localhost/nxloader-builder:local
cache=${XDG_CACHE_HOME:-$HOME/.cache}/nxloader-ccache
mkdir -p "$cache"

"$engine" build --build-arg "BUILD_JOBS=$jobs" -t "$image" "$root/tools/nxloader-builder"
"$engine" run --rm \
    -v "$root:/src" -v "$cache:/ccache" \
    "$image" sh -c 'cmake --preset "$1" && cmake --build --preset "$1" --parallel "$2"' \
    sh "$preset" "$jobs"
