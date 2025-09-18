# Pitch Detector + Autotune — Quickstart

## Build Standalone App
- Configure: `cmake -S PitchDetector -B PitchDetector/build -DCMAKE_BUILD_TYPE=Debug`
- Build: `cmake --build PitchDetector/build --target PitchDetectorApp`
- Launch: `open "PitchDetector/build/PitchDetectorApp_artefacts/Debug/Pitch Detector.app"`

Notes: macOS will prompt for microphone permission on first run. Autotune is ON by default; adjust strength/speed, scale, root, and LPC Formant in the UI.

## Build Plugins (AU + VST3)
- Configure with local copy enabled: `cmake -S PitchDetector -B PitchDetector/build -DCMAKE_BUILD_TYPE=Debug -DLOCAL_PLUGIN_COPY_DIR=ON`
- Build: `cmake --build PitchDetector/build --target PitchDetectorPlugin_VST3 PitchDetectorPlugin_AU`
- Local outputs:
  - VST3: `PitchDetector/build/LocalPlugins/VST3/Pitch Detector.vst3`
  - AU:   `PitchDetector/build/LocalPlugins/AU/Pitch Detector.component`

## Install into DAW
- VST3: copy to `~/Library/Audio/Plug-Ins/VST3/`
- AU:   copy to `~/Library/Audio/Plug-Ins/Components/`

You can also use the helper script: `bash tools/install_local_plugins.sh` (macOS).

## Package Plugins
- Create a zip in `PitchDetector/build/dist/`:
  - `cmake --build PitchDetector/build --target package_plugins`

## Tests
- LPC formant path check: `cmake --build PitchDetector/build --target LPCFormantTest && PitchDetector/build/LPCFormantTest`
- CTest (optional): `ctest --test-dir PitchDetector/build -R LPCFormantTest -V`

## Troubleshooting
- If your DAW doesn’t see the AU, run `auval -v aufx PtDt YrCo` (macOS) then rescan.
- On first AU/VST3 load, macOS Gatekeeper may prompt; these builds use ad-hoc signatures for development. For distribution, sign/notarize with your Developer ID.

