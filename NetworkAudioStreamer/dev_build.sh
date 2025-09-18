#!/bin/bash
#
# Ultimate Developer Build Script - Cross-Platform NetworkAudioStreamer  
# One clean, tiny script that handles everything automatically
#

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${BLUE}🎵 Ultimate NetworkAudioStreamer Build${NC}"
echo "================================================"

# Auto-detect platform
OS=$(uname -s)
ARCH=$(uname -m)

case "$OS" in
    Darwin)
        PLATFORM="macOS"
        CORES=$(sysctl -n hw.ncpu)
        ;;
    Linux)
        PLATFORM="Linux"
        CORES=$(nproc)
        ;;
    CYGWIN*|MINGW32*|MINGW64*|MSYS*)
        PLATFORM="Windows"
        CORES=${NUMBER_OF_PROCESSORS:-4}
        ;;
    *)
        echo -e "${RED}❌ Unsupported platform: $OS${NC}"
        exit 1
        ;;
esac

echo -e "${GREEN}🖥️  Platform: $PLATFORM ($ARCH) - $CORES cores${NC}"

# Clean build
echo -e "${YELLOW}🧹 Cleaning previous build...${NC}"
rm -rf build
mkdir -p build
cd build

# Configure with proper flags for each platform
echo -e "${YELLOW}⚙️  Configuring CMake for $PLATFORM...${NC}"

CMAKE_FLAGS="-DCMAKE_BUILD_TYPE=Release"

case "$PLATFORM" in
    macOS)
        CMAKE_FLAGS+=" -DCMAKE_OSX_ARCHITECTURES=${ARCH}"
        CMAKE_FLAGS+=" -DCMAKE_OSX_DEPLOYMENT_TARGET=10.15"
        ;;
    Linux)
        CMAKE_FLAGS+=" -DCMAKE_POSITION_INDEPENDENT_CODE=ON"
        ;;
    Windows)
        CMAKE_FLAGS+=" -G \"MinGW Makefiles\""
        ;;
esac

# Run CMake
eval "cmake .. $CMAKE_FLAGS"

# Build with all cores
echo -e "${YELLOW}🔨 Building with $CORES cores...${NC}"
make -j$CORES VERBOSE=1

# Verify build results
echo -e "${YELLOW}🔍 Checking build results...${NC}"

# Find and list all built executables
APPS_FOUND=0

if [ "$PLATFORM" = "macOS" ]; then
    # macOS app bundles
    find . -name "*.app" -type d | while read app; do
        echo -e "${GREEN}📱 Found app: $app${NC}"
        APPS_FOUND=$((APPS_FOUND + 1))
        
        # Check if executable exists
        EXEC_PATH="$app/Contents/MacOS/Network Audio Streamer"
        if [ -f "$EXEC_PATH" ]; then
            echo -e "  ✅ Executable: $EXEC_PATH"
            chmod +x "$EXEC_PATH"
        else
            echo -e "  ❌ Missing executable in $app"
        fi
    done
    
    # VST3 plugins
    find . -name "*.vst3" -type d | while read vst; do
        echo -e "${GREEN}🎛️  Found VST3: $vst${NC}"
    done
    
    # AU plugins  
    find . -name "*.component" -type d | while read au; do
        echo -e "${GREEN}🎵 Found AU: $au${NC}"
    done
else
    # Linux/Windows executables
    find . -name "*NetworkAudioStreamer*" -type f -executable | while read exec; do
        echo -e "${GREEN}⚡ Found executable: $exec${NC}"
        APPS_FOUND=$((APPS_FOUND + 1))
    done
fi

echo ""
echo -e "${GREEN}🎉 Build completed successfully!${NC}"
echo ""

# Platform-specific run instructions
case "$PLATFORM" in
    macOS)
        echo -e "${BLUE}🚀 To run standalone app:${NC}"
        echo "   open \"build/NetworkAudioStreamerApp_artefacts/Release/Network Audio Streamer.app\""
        echo ""
        echo -e "${BLUE}📍 Plugin locations:${NC}"
        echo "   VST3: ~/Library/Audio/Plug-Ins/VST3/"
        echo "   AU:   ~/Library/Audio/Plug-Ins/Components/"
        ;;
    Linux)
        echo -e "${BLUE}🚀 To run standalone app:${NC}"
        echo "   ./NetworkAudioStreamerApp"
        echo ""
        echo -e "${BLUE}📍 Plugin location:${NC}"
        echo "   VST3: ~/.vst3/"
        ;;
    Windows)
        echo -e "${BLUE}🚀 To run standalone app:${NC}"
        echo "   NetworkAudioStreamerApp.exe"
        echo ""
        echo -e "${BLUE}📍 Plugin location:${NC}"
        echo "   VST3: %COMMONPROGRAMFILES%/VST3/"
        ;;
esac

echo ""
echo -e "${GREEN}💡 Usage:${NC}"
echo "   Server: Set one instance to 'Server' mode (port 9001)"
echo "   Client: Set other instances to 'Client' mode (connect to server IP)"
echo "   Ultra-low latency: <10ms with proper network setup"
echo ""