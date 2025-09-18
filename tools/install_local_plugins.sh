#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")"/.. && pwd)"
BUILD_DIR="$ROOT_DIR/PitchDetector/build"
SRC_VST3="$BUILD_DIR/LocalPlugins/VST3"
SRC_AU="$BUILD_DIR/LocalPlugins/AU"
DEST_VST3="$HOME/Library/Audio/Plug-Ins/VST3"
DEST_AU="$HOME/Library/Audio/Plug-Ins/Components"

echo "Installing local plugins..."
echo "Source VST3: $SRC_VST3"
echo "Source AU:   $SRC_AU"

mkdir -p "$DEST_VST3" "$DEST_AU"

shopt -s nullglob
for p in "$SRC_VST3"/*.vst3; do
  echo "Copying $(basename "$p") -> $DEST_VST3"
  cp -R "$p" "$DEST_VST3/"
done
for p in "$SRC_AU"/*.component; do
  echo "Copying $(basename "$p") -> $DEST_AU"
  cp -R "$p" "$DEST_AU/"
done

echo "Done. If your DAW doesn’t see the plugins, rescan your plugin folders."

