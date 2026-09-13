#!/usr/bin/env bash
# Run on the engine development machine; generated archives stay under var/.
set -euo pipefail
repo=$(git rev-parse --show-toplevel)
[ -n "$repo" ] && [ -x "$repo/build/linux-debug/demi" ]
store="$repo/tools/package-store"
stage=$(mktemp -d)
[ -n "$stage" ] && [ -d "$stage" ]
mkdir -p "$store/var/seed"
for metadata in "$store"/catalog/*.json; do
  name=$(basename "$metadata" .json)
  [ -f "$repo/packages/sources/$name/demi.package.json" ]
  "$repo/build/linux-debug/demi" package publish "$repo/packages/sources/$name" --registry "$stage"
  archive="$stage/packages/$name/1.0.0/package.demipkg"
  [ -f "$archive" ]
  cp "$archive" "$store/var/seed/$name.demipkg"
done
echo "Seed archives are in tools/package-store/var/seed."
