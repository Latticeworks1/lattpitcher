# FL Stream Plugin Suite v1.1.0

Real-time collaborative voice communication plugins for music production with Colyseus WebSocket integration.

## Features

### Two Plugin Variants

#### 1. FL Stream Plugin (Audio Effect)
- **Type**: Audio Effect (Insert/Send)
- **Formats**: VST3, AU, Standalone
- **I/O**: Stereo Input → Stereo Output
- **Use Case**: Insert on audio tracks for voice collaboration during recording/mixing

#### 2. FL Stream Generator (Synthesizer/Instrument)
- **Type**: Synthesizer/Instrument with Audio Input
- **Formats**: VST3, AU, Standalone  
- **I/O**: External Audio Input (optional) + MIDI Input → Stereo Output
- **Use Case**: Load as instrument for voice generation with side-chain audio processing

### Core Capabilities

- **Real-time Voice Chat**: Push-to-talk communication over network
- **Colyseus Integration**: WebSocket-based multiplayer room management
- **SSL Security**: Encrypted communication with voice.latticeworks-ai.com
- **Cross-Platform**: macOS, Windows, Linux support
- **Multiple Formats**: VST3, Audio Unit, Standalone applications
- **Modern UI**: Adaptive interface (WebView in DAW, Native in Standalone)

## Quick Start

### Building

```bash
# Clone and prepare
git clone [repository-url]
cd FLStreamPlugin

# Build both plugins
mkdir build && cd build
cmake ..
make -j4
```

### Installation

**Automatic Installation:**
- VST3: `~/Library/Audio/Plug-Ins/VST3/`
- AU: `~/Library/Audio/Plug-Ins/Components/`

**Manual Installation:**
- Copy plugin files from `*_artefacts/` directories to your DAW's plugin folders

### Usage

1. **Load Plugin**: Insert FL Stream Plugin (effect) or FL Stream Generator (instrument)
2. **Connect**: Plugin auto-connects to voice.latticeworks-ai.com
3. **Voice Chat**: Hold push-to-talk button to transmit audio
4. **Collaborate**: Other users in the same room hear your audio in real-time

## Technical Architecture

### Audio Processing
- **Sample Rate**: 48kHz optimized
- **Latency**: Low-latency real-time processing
- **Encoding**: Raw float32 audio transmission
- **Mixing**: Incoming voice mixed at 50% volume

### Network Protocol
- **Transport**: SSL WebSocket over HTTPS
- **Matchmaking**: POST to `/matchmake/joinOrCreate/my_room`
- **Protocol**: Colyseus binary frame protocol
- **Authentication**: Session-based room management

### Plugin Architecture
```
FLStreamProcessor (AudioProcessor)
├── ColyseusRoomClient (WebSocket SSL)
├── Audio Processing Pipeline
├── Parameter Management
└── FLStreamEditor (GUI)
    ├── WebView (VST/AU mode)
    └── Native UI (Standalone mode)
```

## Development

### Dependencies
- **JUCE 7+**: Audio framework
- **OpenSSL**: SSL/TLS encryption  
- **CURL**: HTTP/HTTPS networking
- **Opus**: Audio codec (optional)
- **CMake 3.22+**: Build system

### Code Structure
```
Source/
├── FLStreamProcessor.cpp/h    # Main audio processor
├── FLStreamEditor.cpp/h       # Plugin GUI interface  
└── ColyseusRoomClient.cpp/h   # WebSocket networking
```

### Build Configuration
- **Effect Plugin**: Standard input/output bus layout
- **Generator Plugin**: Output-only with optional external input
- **Conditional Compilation**: `JucePlugin_IsSynth` determines behavior

## Version History

### v1.1.0 (Current)
- ✅ Added FL Stream Generator (synthesizer with audio input)
- ✅ External audio input support (like Serum)
- ✅ UTF-8 encoding issues resolved
- ✅ Improved code documentation and comments
- ✅ ASCII-only interface elements
- ✅ Enhanced bus configuration for both plugin types

### v1.0.0
- ✅ Initial FL Stream Plugin (audio effect)
- ✅ Colyseus WebSocket integration
- ✅ Push-to-talk voice communication
- ✅ SSL encrypted connections
- ✅ Cross-platform VST3/AU/Standalone support

## Configuration

### CMake Options
```cmake
# Build both plugin variants
juce_add_plugin(FLStreamPlugin ...)     # Audio effect
juce_add_plugin(FLStreamGenerator ...)  # Synthesizer
```

### Server Configuration
- **Endpoint**: `https://voice.latticeworks-ai.com`
- **Room Management**: Automatic join/create
- **Session Handling**: Colyseus protocol compliance

## License

Production deployment configuration for FL Stream Plugin Suite.

## Support

- **Issues**: Report technical issues via repository issue tracker
- **Documentation**: See CLAUDE.md for detailed technical specifications
- **Development**: Follow conventional commit patterns for contributions