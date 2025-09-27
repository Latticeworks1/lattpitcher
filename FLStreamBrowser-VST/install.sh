#!/bin/bash

# FL Stream Browser VST - Installation Script
# Automatically installs VST3, AU, and Standalone versions

set -e

echo "🎵 FL Stream Browser VST - Installation Script"
echo "=============================================="

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Check if we're on macOS
if [[ "$OSTYPE" != "darwin"* ]]; then
    echo -e "${RED}Error: This installer is for macOS only${NC}"
    echo "For other platforms, please build from source (see README.md)"
    exit 1
fi

# Create plugin directories if they don't exist
echo -e "${BLUE}Creating plugin directories...${NC}"
mkdir -p ~/Library/Audio/Plug-Ins/VST3
mkdir -p ~/Library/Audio/Plug-Ins/Components

# Check if plugin files exist
if [ ! -d "FLStreamPlugin_artefacts/VST3/FL Stream Plugin.vst3" ]; then
    echo -e "${RED}Error: VST3 plugin not found${NC}"
    echo "Expected: FLStreamPlugin_artefacts/VST3/FL Stream Plugin.vst3"
    echo "Please ensure you have the complete distribution package"
    exit 1
fi

# Install VST3 Plugin
echo -e "${BLUE}Installing VST3 plugin...${NC}"
if cp -r "FLStreamPlugin_artefacts/VST3/FL Stream Plugin.vst3" ~/Library/Audio/Plug-Ins/VST3/; then
    echo -e "${GREEN}✅ VST3 plugin installed successfully${NC}"
    echo "   Location: ~/Library/Audio/Plug-Ins/VST3/FL Stream Plugin.vst3"
else
    echo -e "${RED}❌ Failed to install VST3 plugin${NC}"
    exit 1
fi

# Install AU Plugin
echo -e "${BLUE}Installing AU plugin...${NC}"
if cp -r "FLStreamPlugin_artefacts/AU/FL Stream Plugin.component" ~/Library/Audio/Plug-Ins/Components/; then
    echo -e "${GREEN}✅ AU plugin installed successfully${NC}"
    echo "   Location: ~/Library/Audio/Plug-Ins/Components/FL Stream Plugin.component"
else
    echo -e "${RED}❌ Failed to install AU plugin${NC}"
    exit 1
fi

# Install Standalone Application
echo -e "${BLUE}Installing Standalone application...${NC}"
if cp -r "FLStreamPlugin_artefacts/Standalone/FL Stream Plugin.app" /Applications/; then
    echo -e "${GREEN}✅ Standalone app installed successfully${NC}"
    echo "   Location: /Applications/FL Stream Plugin.app"
else
    echo -e "${RED}❌ Failed to install Standalone app${NC}"
    exit 1
fi

# Set proper permissions
echo -e "${BLUE}Setting permissions...${NC}"
chmod -R 755 ~/Library/Audio/Plug-Ins/VST3/FL\ Stream\ Plugin.vst3
chmod -R 755 ~/Library/Audio/Plug-Ins/Components/FL\ Stream\ Plugin.component
chmod -R 755 /Applications/FL\ Stream\ Plugin.app

echo ""
echo -e "${GREEN}🎉 Installation completed successfully!${NC}"
echo ""
echo -e "${YELLOW}Next steps:${NC}"
echo "1. Restart your DAW (FL Studio, Logic Pro, Ableton Live, etc.)"
echo "2. Look for 'FL Stream Plugin' in your plugin browser"
echo "3. Load the plugin and click 'Home' to access FL Stream voice chat"
echo "4. Automate the 'Talking' parameter for push-to-talk functionality"
echo ""
echo -e "${YELLOW}Testing installation:${NC}"
echo "• VST3: Available in FL Studio, Ableton Live, etc."
echo "• AU: Available in Logic Pro, GarageBand, etc."  
echo "• Standalone: Launch from Applications or Spotlight"
echo ""
echo -e "${BLUE}Voice Chat:${NC} https://voice.latticeworks-ai.com"
echo -e "${BLUE}Documentation:${NC} See README.md and INSTALL.md"
echo ""
echo "🚀 Happy music making with FL Stream Browser VST!"