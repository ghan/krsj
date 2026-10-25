#!/bin/bash
set -euo pipefail
overlay="$(cd "$(dirname "$0")/.." && pwd)"
source_dir="${1:-$overlay/../.build/source}"
base=35f1062dab196b9838a4bb529c4bf6592b7f5987
patch_hash="$(shasum -a 256 "$overlay/upstream.patch" | awk '{print $1}')"
expected="$(printf '%s\n%s' "$base" "$patch_hash")"
if [ -f "$source_dir/.station-desk-base" ]; then
  if [ "$(cat "$source_dir/.station-desk-base")" != "$expected" ]; then
    echo 'The upstream patch changed. Choose a new STATION_SOURCE_DIR to rebuild the generated source.' >&2
    exit 1
  fi
elif [ -e "$source_dir" ]; then
  echo "Refusing to overwrite an unrecognized source directory: $source_dir" >&2
  exit 1
else
  mkdir -p "$(dirname "$source_dir")"
  staging="$(mktemp -d "$(dirname "$source_dir")/source-stage.XXXXXX")"
  staging="$(cd "$staging" && pwd -P)"
  trap 'rm -rf "$staging"' EXIT
  curl --fail --location --retry 3 "https://github.com/sonosaurus/sonobus/archive/$base.tar.gz" -o "$staging/source.tar.gz"
  mkdir "$staging/source"
  tar -xzf "$staging/source.tar.gz" --strip-components=1 -C "$staging/source"
  # The extracted source sits inside the website checkout. Prevent git apply
  # from finding that parent repo and silently skipping paths outside its prefix.
  GIT_CEILING_DIRECTORIES="$staging" git -C "$staging/source" apply --whitespace=nowarn "$overlay/upstream.patch"
  if ! grep -q 'STATION_DESK=1' "$staging/source/CMakeLists.txt"; then
    echo 'Station Desk patch was not applied; refusing to build upstream SonoBus.' >&2
    exit 1
  fi
  printf '%s\n' "$expected" > "$staging/source/.station-desk-base"
  mv "$staging/source" "$source_dir"
fi
mkdir -p "$source_dir/Source" "$source_dir/tests"
cp "$overlay"/Source/*.h "$source_dir/Source/"
cp "$overlay"/tests/*.h "$overlay"/tests/*.cpp "$overlay"/tests/*.py "$source_dir/tests/"
printf 'Prepared: %s\n' "$source_dir"
