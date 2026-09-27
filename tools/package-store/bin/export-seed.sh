#!/usr/bin/env bash
# Run on the engine development machine; generated archives stay under var/.
set -euo pipefail
repo=$(git -C "$(dirname -- "${BASH_SOURCE[0]}")" rev-parse --show-toplevel)
[ -n "$repo" ]
binary=${DEMI_ENGINE_BINARY:-"$repo/build/linux-release/demi"}
while [ "$#" -gt 0 ]; do
  case "$1" in
    --binary)
      [ "$#" -ge 2 ] && [ -n "$2" ] || { echo "--binary requires a path" >&2; exit 2; }
      binary=$2
      shift 2
      ;;
    --help)
      echo "Usage: $0 [--binary PATH] (default: DEMI_ENGINE_BINARY or build/linux-release/demi)"
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      exit 2
      ;;
  esac
done
[ -x "$binary" ] || { echo "Engine binary is not executable: $binary" >&2; exit 1; }
command -v jq >/dev/null || { echo "jq is required to read package versions" >&2; exit 1; }
store="$repo/tools/package-store"
stage=$(mktemp -d)
[ -n "$stage" ] && [ -d "$stage" ]
mkdir -p "$store/var/seed"
for metadata in "$store"/catalog/*.json; do
  name=$(basename "$metadata" .json)
  source="$repo/packages/sources/$name"
  manifest="$source/demi.package.json"
  [ -f "$manifest" ] || { echo "Missing package manifest: $manifest" >&2; exit 1; }
  version=$(jq -er '.version | strings | select(test("^[0-9]+\\.[0-9]+\\.[0-9]+([+-][0-9A-Za-z.-]+)?$"))' "$manifest") || {
    echo "Invalid package version in $manifest" >&2
    exit 1
  }
  "$binary" package publish "$source" --registry "$stage"
  archive="$stage/packages/$name/$version/package.demipkg"
  [ -f "$archive" ]
  cp "$archive" "$store/var/seed/$name.demipkg"
done
echo "Seed archives are in tools/package-store/var/seed."
