# FL Stream Browser VST

A JUCE-based general web browser plugin for FL Studio and other DAWs with integrated FL Stream voice collaboration capabilities.

## Features

- **General Web Browser**: Navigate to any website with full browser controls
- **FL Stream Voice Chat**: Built-in real-time voice collaboration via voice.latticeworks-ai.com
- **Push-to-Talk**: Parameter-driven voice activation for recording automation
- **Multiple Formats**: Available as VST3, AU, and Standalone application
- **Colyseus Integration**: Real-time multiplayer voice communication over WebSocket

## Quick Installation (macOS)

### Pre-built Plugins (Recommended)

1. **VST3 Plugin** (for FL Studio, Ableton, etc.):
   ```bash
   cp -r FLStreamPlugin_artefacts/VST3/FL\ Stream\ Plugin.vst3 ~/Library/Audio/Plug-Ins/VST3/
   ```

2. **AU Plugin** (for Logic Pro, GarageBand, etc.):
   ```bash
   cp -r FLStreamPlugin_artefacts/AU/FL\ Stream\ Plugin.component ~/Library/Audio/Plug-Ins/Components/
   ```

3. **Standalone App**:
   ```bash
   cp -r FLStreamPlugin_artefacts/Standalone/FL\ Stream\ Plugin.app /Applications/
   ```

### Usage

1. **Load the Plugin**: Open your DAW and load "FL Stream Plugin" from the plugin browser
2. **Web Browsing**: Use the navigation bar (back, forward, address bar, go button, home)
3. **FL Stream Voice Chat**: Click "Home" button to access voice.latticeworks-ai.com
4. **Push-to-Talk**: Automate the "Talking" parameter in your DAW for voice activation

## Building from Source

### Prerequisites

- **macOS 10.15+** (Catalina or later)
- **Xcode Command Line Tools**: `xcode-select --install`
- **CMake 3.22+**: `brew install cmake`
- **Dependencies**: `brew install curl boost opus openssl pkg-config`

### Build Steps

```bash
# Clone JUCE (if not present)
git clone https://github.com/juce-framework/JUCE.git
cd JUCE && git checkout 7.0.12 && cd ..

# Build the plugin
mkdir build && cd build
cmake ..
make -j4

# Built plugins will be in FLStreamPlugin_artefacts/
```

## Architecture

- **FLStreamProcessor**: Main audio processor with Colyseus WebSocket integration
- **FLStreamEditor**: General web browser GUI with navigation controls  
- **ColyseusRoomClient**: Raw SSL WebSocket client for voice.latticeworks-ai.com
- **WebBrowserComponent**: JUCE web browser with full navigation capabilities

## Technical Details

### Audio Processing
- Real-time push-to-talk detection via "Talking" parameter
- SSL WebSocket communication with voice.latticeworks-ai.com
- Audio mixing of incoming voice data with output buffer
- Thread-safe audio message queue for real-time processing

### Browser Features
- **Navigation**: Back, Forward, Home, Address bar, Go button
- **Home Page**: voice.latticeworks-ai.com with embedded voice chat interface
- **General Browsing**: Navigate to any website while maintaining FL Stream functionality
- **Window Size**: 1000x700 with 45px navigation bar

### Colyseus Integration
- **Matchmaking**: HTTPS POST to `/matchmake/joinOrCreate/my_room` 
- **WebSocket**: SSL connection to `/roomId?sessionId=sessionId`
- **Protocol**: Binary Colyseus protocol for room management and voice data
- **Threading**: Separate audio, WebSocket, and GUI threads

## Compatibility

### Tested DAWs
- ✅ FL Studio 21+ (VST3)
- ✅ Logic Pro X (AU)
- ✅ Ableton Live (VST3)
- ✅ Standalone Application

### System Requirements
- **macOS**: 10.15+ (Catalina or later)
- **Windows**: Windows 10+ (build from source)
- **RAM**: 2GB minimum
- **Network**: Internet connection for voice.latticeworks-ai.com

## Troubleshooting

### Plugin Not Loading
- Ensure plugin is in correct directory (`~/Library/Audio/Plug-Ins/VST3/` or `~/Library/Audio/Plug-Ins/Components/`)
- Restart your DAW after installation
- Check DAW's plugin scanner/manager

### Voice Chat Issues
- Verify internet connection to voice.latticeworks-ai.com
- Check browser console for WebSocket connection errors
- Ensure "Talking" parameter is properly automated for push-to-talk

### Build Issues
- Install all dependencies: `brew install cmake curl boost opus openssl pkg-config`
- Ensure JUCE is at version 7.0.12
- Use absolute paths in CMake configuration

## License

This project integrates multiple open-source components:
- **JUCE Framework**: GPL v3 / Commercial License
- **OpenSSL**: Apache License 2.0
- **libwebm**: BSD License
- **Opus Codec**: BSD License

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test with multiple DAWs
5. Submit a pull request

## Support

- **Issues**: Report bugs via GitHub Issues
- **Documentation**: See `/docs` for technical details
- **Voice Chat**: Connect via voice.latticeworks-ai.com

---

🎵 **FL Stream Browser VST** - Real-time voice collaboration meets general web browsing for music production.