#!/bin/sh
# Rewrite the golden RMS files after an intentional DSP change.
set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
mkdir -p "$root/tests/golden"
for cfg in "$root"/tools/render/usecases/*.json; do
  name=$(basename "$cfg" .json)
  "$root/build/tools/render" "$cfg" --golden "$root/tests/golden/$name.csv" --write
done
