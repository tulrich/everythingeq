#!/usr/bin/env bash
# Runs the plugin validators against a plugin build:
#   macOS:   auval (AU), pluginval (AU, VST3), clap-validator (CLAP)
#   Windows: pluginval (VST3), clap-validator (CLAP)
# Usage: scripts/validate-plugins.sh [build-dir]   (default: build-plugin)
# Set PLUGINVAL_SKIP_GUI=1 on headless machines.  The AU must already be
# installed in ~/Library/Audio/Plug-Ins/Components (EEQ_INSTALL_PLUGINS=ON).
set -euo pipefail

PLUGINVAL_VERSION="v1.0.4"
CLAP_VALIDATOR_VERSION="0.4.1"
CLAP_VALIDATOR_BUILD="0.4.1-127-g152b982"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$(cd "${1:-$ROOT/build-plugin}" && pwd)"
OUT="$BUILD/out"
TOOLS="$BUILD/validators"
mkdir -p "$TOOLS"

case "$(uname -s)" in
  Darwin) OS=macos ;;
  MINGW*|MSYS*|CYGWIN*) OS=windows ;;
  *) echo "Unsupported OS"; exit 1 ;;
esac

fetch() {  # fetch <url> <dest-zip>
  [ -f "$2" ] || curl -fsSL -o "$2" "$1"
}

GH=https://github.com
if [ "$OS" == macos ]; then
  fetch "$GH/Tracktion/pluginval/releases/download/$PLUGINVAL_VERSION/pluginval_macOS.zip" "$TOOLS/pluginval.zip"
  fetch "$GH/free-audio/clap-validator/releases/download/$CLAP_VALIDATOR_VERSION/clap-validator-$CLAP_VALIDATOR_BUILD-macos-universal.zip" "$TOOLS/clap-validator.zip"
  [ -d "$TOOLS/pluginval.app" ] || unzip -qo "$TOOLS/pluginval.zip" -d "$TOOLS"
  if [ ! -x "$TOOLS/binaries/clap-validator" ]; then
    unzip -qo "$TOOLS/clap-validator.zip" -d "$TOOLS"
    tar xzf "$TOOLS"/clap-validator-*.tar.gz -C "$TOOLS"
  fi
  xattr -dr com.apple.quarantine "$TOOLS" 2>/dev/null || true
  PLUGINVAL="$TOOLS/pluginval.app/Contents/MacOS/pluginval"
  CLAP_VALIDATOR="$TOOLS/binaries/clap-validator"
else
  fetch "$GH/Tracktion/pluginval/releases/download/$PLUGINVAL_VERSION/pluginval_Windows.zip" "$TOOLS/pluginval.zip"
  fetch "$GH/free-audio/clap-validator/releases/download/$CLAP_VALIDATOR_VERSION/clap-validator-$CLAP_VALIDATOR_BUILD-windows.zip" "$TOOLS/clap-validator.zip"
  [ -f "$TOOLS/pluginval.exe" ] || unzip -qo "$TOOLS/pluginval.zip" -d "$TOOLS"
  [ -f "$TOOLS/clap-validator.exe" ] || unzip -qo "$TOOLS/clap-validator.zip" -d "$TOOLS"
  PLUGINVAL="$TOOLS/pluginval.exe"
  CLAP_VALIDATOR="$TOOLS/clap-validator.exe"
fi

PV_ARGS=(--strictness-level 10 --validate-in-process)
[ "${PLUGINVAL_SKIP_GUI:-0}" == 1 ] && PV_ARGS+=(--skip-gui-tests)

status=0
run() {  # run <label> <command...>
  echo "=== $1"
  if "${@:2}"; then echo "=== $1: PASS"; else echo "=== $1: FAIL"; status=1; fi
}

if [ "$OS" == macos ]; then
  killall -9 AudioComponentRegistrar 2>/dev/null || true
  run "auval (AU)" auval -strict -v aufx EvEQ Tulr
  run "pluginval (AU)" "$PLUGINVAL" "${PV_ARGS[@]}" --validate "$HOME/Library/Audio/Plug-Ins/Components/EverythingEQ.component"
fi
run "pluginval (VST3)" "$PLUGINVAL" "${PV_ARGS[@]}" --validate "$OUT/EverythingEQ.vst3"
run "clap-validator (CLAP)" "$CLAP_VALIDATOR" validate --only-failed "$OUT/EverythingEQ.clap"

exit $status
