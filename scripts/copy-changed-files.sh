#!/usr/bin/env bash
# Copies each file under FROM into TO only when its content differs, so the
# copy gets a new time and a build there rebuilds it, and nothing else does.
#
#   scripts/copy-changed-files.sh FROM TO
set -euo pipefail

from="$1"
to="$2"

cd "$from"
find . -type f | while read -r file; do
    if ! cmp -s "$file" "$to/$file"; then
        mkdir -p "$to/$(dirname "$file")"
        cp "$file" "$to/$file"
    fi
done
