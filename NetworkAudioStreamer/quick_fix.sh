#!/bin/bash
#
# Ultimate Developer Fix - Remove conflicts, clean build
#

echo "🔧 Surgical Fix: Removing Opus stub conflicts..."

# Remove stub file that conflicts with real Opus library
rm -f Source/OpusCodecStubs.cpp

# Update CMakeLists.txt to remove stub reference
sed -i '' 's/Source\/OpusCodecStubs.cpp//g' CMakeLists.txt

# Clean and minimal rebuild
echo "🧹 Clean rebuild..."
rm -rf build
mkdir build
cd build

# Configure with real Opus library
cmake .. \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=10.15

# Build just the standalone app (faster)
echo "🚀 Building standalone app..."
make NetworkAudioStreamerApp -j$(sysctl -n hw.ncpu)

# Direct launch if successful
if [ -f "NetworkAudioStreamerApp_artefacts/Release/Network Audio Streamer.app/Contents/MacOS/Network Audio Streamer" ]; then
    echo "✅ Success! Launching app..."
    open "NetworkAudioStreamerApp_artefacts/Release/Network Audio Streamer.app"
else
    echo "❌ Build failed - checking for binary..."
    find . -name "*Network*" -type f -executable 2>/dev/null | head -5
fi