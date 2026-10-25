#!/bin/sh
# Render every use case to renders/<name>.wav plus a detector probe CSV.
set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
mkdir -p "$root/renders"
for cfg in "$root"/tools/render/usecases/*.json; do
  name=$(basename "$cfg" .json)
  "$root/build/tools/render" "$cfg" -o "$root/renders/$name.wav" --probe "$root/renders/$name.csv"
  echo "renders/$name.wav"
done
