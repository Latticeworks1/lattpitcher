#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BUILD_DIR="$ROOT_DIR/PitchDetector/build"

echo "[run_standalone] Configuring CMake (Debug) if needed..."
if [[ ! -f "$BUILD_DIR/Makefile" ]]; then
  cmake -S "$ROOT_DIR/PitchDetector" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug
fi

echo "[run_standalone] Building Standalone + App targets..."
cmake --build "$BUILD_DIR" --target PitchDetectorPlugin_Standalone -j2
cmake --build "$BUILD_DIR" --target PitchDetectorApp -j2

# Prefer the App target (more self-contained), fall back to plugin standalone
APP_DIR_APP="$BUILD_DIR/PitchDetectorApp_artefacts/Debug/Pitch Detector.app"
APP_DIR_PLUGIN="$BUILD_DIR/PitchDetectorPlugin_artefacts/Standalone/Pitch Detector.app"

if [[ -d "$APP_DIR_APP" ]]; then
  APP_PATH="$APP_DIR_APP"
elif [[ -d "$APP_DIR_PLUGIN" ]]; then
  APP_PATH="$APP_DIR_PLUGIN"
else
  echo "[run_standalone] ERROR: No app bundle found in: $APP_DIR_APP or $APP_DIR_PLUGIN" >&2
  exit 1
fi

echo "[run_standalone] Selected bundle: $APP_PATH"

# Try running the inner executable to surface logs in the terminal
BIN_PATH=$(find "$APP_PATH/Contents/MacOS" -maxdepth 1 -type f -perm +111 -print 2>/dev/null | head -n1)
if [[ -x "$BIN_PATH" ]]; then
  LOG_FILE="$BUILD_DIR/standalone_run.log"
  echo "[run_standalone] Running binary: $BIN_PATH (logging to $LOG_FILE)"
  echo "==== $(date) ====" >> "$LOG_FILE"
  "$BIN_PATH" 2>&1 | tee -a "$LOG_FILE"
else
  echo "[run_standalone] No direct binary found; falling back to 'open'"
  open "$APP_PATH"
fi

echo "[run_standalone] If the app doesn't appear, check Gatekeeper (right-click → Open)."
