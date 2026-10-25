#!/usr/bin/env bash
# Fetches the iPlug2 submodule and the plugin SDKs it needs (VST3, CLAP),
# pinned to known versions.  Re-run after changing a pin.
set -euo pipefail

VST3_SDK_TAG="v3.8.1_build_84"
CLAP_SDK_SHA="a47f6badb49d948fd009998f28309cdab78979c9"  # main; clap-helpers main needs post-1.2.x draft extensions
CLAP_HELPERS_SHA="a380c978f429103e08afc1d17e9efbf3e3c96ee2"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEPS="$ROOT/external/iPlug2/Dependencies/IPlug"

git -C "$ROOT" submodule update --init external/iPlug2

stamp() { cat "$1/.eeq-version" 2>/dev/null || true; }

if [ "$(stamp "$DEPS/VST3_SDK")" != "$VST3_SDK_TAG" ]; then
  (cd "$DEPS" && ./download-vst3-sdk.sh "$VST3_SDK_TAG")
  echo "$VST3_SDK_TAG" > "$DEPS/VST3_SDK/.eeq-version"
fi

# fetch_at_sha <repo-url> <sha> <dir name under Dependencies/IPlug>
fetch_at_sha() {
  local url="$1" sha="$2" dir="$DEPS/$3"
  [ "$(stamp "$dir")" == "$sha" ] && return
  rm -rf "$dir.tmp"
  git init -q "$dir.tmp"
  git -C "$dir.tmp" fetch -q --depth 1 "$url" "$sha"
  git -C "$dir.tmp" checkout -q FETCH_HEAD
  rm -rf "$dir.tmp/.git"
  find "$dir" -mindepth 1 ! -name readme.txt -exec rm -rf {} + 2>/dev/null || true
  cp -R "$dir.tmp/." "$dir/" && rm -rf "$dir.tmp"
  git -C "$DEPS" checkout -- "$3/readme.txt"
  echo "$sha" > "$dir/.eeq-version"
}

fetch_at_sha https://github.com/free-audio/clap.git "$CLAP_SDK_SHA" CLAP_SDK
fetch_at_sha https://github.com/free-audio/clap-helpers.git "$CLAP_HELPERS_SHA" CLAP_HELPERS

# Local fixes to iPlug2, kept until they land upstream.
for patch in "$ROOT"/scripts/patches/iplug2-*.patch; do
  if git -C "$ROOT/external/iPlug2" apply --reverse --check "$patch" 2>/dev/null; then
    continue  # already applied
  fi
  git -C "$ROOT/external/iPlug2" apply "$patch"
  echo "Applied $(basename "$patch")"
done

echo "Dependencies ready: VST3 SDK $VST3_SDK_TAG, CLAP ${CLAP_SDK_SHA:0:7}, clap-helpers ${CLAP_HELPERS_SHA:0:7}"
