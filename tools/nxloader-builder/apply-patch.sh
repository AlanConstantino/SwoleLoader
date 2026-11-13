#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 patch-file" >&2
    exit 2
fi

if git apply --reverse --check --ignore-whitespace "$1" >/dev/null 2>&1; then
    exit 0
fi

git apply --ignore-whitespace "$1"
