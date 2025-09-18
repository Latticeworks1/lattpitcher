#!/bin/bash

# NetworkAudioStreamer Build Script
# Professional JUCE-based VST plugin for real-time audio streaming

set -e

echo "🎵 Building NetworkAudioStreamer - Real-Time Audio Streaming VST Plugin"
echo "=================================================================="

# Check for required dependencies
echo "🔍 Checking dependencies..."

if ! command -v cmake &> /dev/null; then
    echo "❌ CMake not found. Please install CMake."
    exit 1
fi

if ! brew list opus &> /dev/null && ! dpkg -l | grep -q libopus-dev; then
    echo "❌ Opus codec not found. Please install:"
    echo "   macOS: brew install opus"
    echo "   Linux: sudo apt-get install libopus-dev"
    exit 1
fi

if [ ! -d "../JUCE" ]; then
    echo "❌ JUCE framework not found. Please clone JUCE:"
    echo "   git clone https://github.com/juce-framework/JUCE.git ../JUCE"
    exit 1
fi

echo "✅ Dependencies satisfied"

# Create build directory
echo "📁 Creating build directory..."
rm -rf build
mkdir build
cd build

# Configure CMake
echo "⚙️  Configuring CMake..."
if [[ "$OSTYPE" == "darwin"* ]]; then
    # macOS
    cmake .. -DCMAKE_BUILD_TYPE=Release
elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
    # Linux
    cmake .. -DCMAKE_BUILD_TYPE=Release
else
    # Windows (assuming MinGW or similar)
    cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
fi

# Build the project
echo "🔨 Building project..."
make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

echo ""
echo "🎉 Build completed successfully!"
echo ""

# Show installation paths
if [[ "$OSTYPE" == "darwin"* ]]; then
    echo "📍 Plugin installed to:"
    echo "   VST3: ~/Library/Audio/Plug-Ins/VST3/Network Audio Streamer.vst3"
    echo "   AU:   ~/Library/Audio/Plug-Ins/Components/Network Audio Streamer.component"
elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
    echo "📍 Plugin installed to:"
    echo "   VST3: ~/.vst3/Network Audio Streamer.vst3"
fi

echo ""
echo "🚀 Usage Instructions:"
echo "   1. Load plugin in FL Studio on multiple instances"
echo "   2. Set one instance to 'Server' mode (port 9001)"
echo "   3. Set other instances to 'Client' mode (connect to server IP)"
echo "   4. Enjoy real-time audio streaming with <10ms latency!"
echo ""

# Test if standalone app was built
if [ -f "./NetworkAudioStreamerApp_artefacts/Debug/NetworkAudioStreamerApp" ] || \
   [ -f "./NetworkAudioStreamerApp_artefacts/Release/NetworkAudioStreamerApp" ] || \
   [ -d "./NetworkAudioStreamerApp_artefacts/Debug/Network Audio Streamer.app" ]; then
    echo "🧪 Test the standalone application:"
    echo "   Server: ./NetworkAudioStreamerApp --server"
    echo "   Client: ./NetworkAudioStreamerApp --client --server-addr=127.0.0.1"
fi

echo ""
echo "📖 For detailed usage instructions, see README.md"
echo "🐛 Report issues at: https://github.com/your-repo/NetworkAudioStreamer/issues"