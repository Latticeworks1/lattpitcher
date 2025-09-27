# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Architecture

This is a JUCE-based FL Stream Plugin Suite for real-time voice collaboration with Colyseus WebSocket integration:
- **FL Stream Plugin**: VST3/AU/Standalone audio effect for track insertion
- **FL Stream Generator**: VST3/AU/Standalone synthesizer with optional audio input (like Serum)
- **Colyseus Integration**: Real-time multiplayer voice communication over WebSocket

### Core Components

- `FLStreamProcessor`: Main audio processor with push-to-talk and Colyseus integration
- `FLStreamEditor`: Plugin GUI with adaptive interface (WebView for DAW, Native for Standalone)
- `ColyseusRoomClient`: Raw SSL WebSocket client for Colyseus protocol communication

### Key Data Structures
- `AudioMessage`: Incoming voice data from other clients with session info
- `ColyseusProtocol`: Protocol constants for room management and voice data

## Build System

### CMake Configuration
Uses JUCE's modern CMake API with OpenSSL and WebSocket dependencies:

```cmake
# FL Stream Plugin (Audio Effect)
juce_add_plugin(FLStreamPlugin
    VST3_CATEGORIES "Effect" "Network"
    AU_MAIN_TYPE "kAudioUnitType_MusicEffect"
)

# FL Stream Generator (Synthesizer with Audio Input)
juce_add_plugin(FLStreamGenerator
    VST3_CATEGORIES "Instrument" "Synth" "Network"
    AU_MAIN_TYPE "kAudioUnitType_MusicDevice"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
)
```

### Build Commands
```bash
# From FLStreamPlugin root directory:
mkdir build && cd build
cmake ..
make -j4

# Test plugins:
timeout 30s "./FLStreamPlugin_artefacts/Standalone/FL Stream Plugin.app/Contents/MacOS/FL Stream Plugin"
timeout 30s "./FLStreamGenerator_artefacts/Standalone/FL Stream Generator.app/Contents/MacOS/FL Stream Generator"

# Plugin installation paths:
# VST3: ~/Library/Audio/Plug-Ins/VST3/
# AU: ~/Library/Audio/Plug-Ins/Components/
```

### Dependencies
```cmake
# Core JUCE modules
juce::juce_audio_processors
juce::juce_gui_extra
juce::juce_cryptography

# Network and SSL
OpenSSL::SSL
OpenSSL::Crypto
CURL

# Optional: Audio codec
${OPUS_LIBRARIES}
webm
```

## Plugin Architecture Differences

### FL Stream Plugin (Audio Effect)
- **Bus Layout**: Stereo Input → Stereo Output
- **Use Case**: Insert on audio tracks for voice collaboration
- **DAW Integration**: WebView interface
- **Processing**: Pass-through with voice overlay

### FL Stream Generator (Synthesizer)
- **Bus Layout**: Optional External Input + MIDI Input → Stereo Output  
- **Use Case**: Load as instrument with side-chain capabilities
- **DAW Integration**: Appears in instrument/generator lists
- **Processing**: Voice generation with external audio mixing

## Colyseus Integration Architecture

### Connection Flow
1. **HTTPS Matchmaking**: POST to `https://voice.latticeworks-ai.com/matchmake/joinOrCreate/my_room`
2. **Room Reservation**: Server returns roomId and sessionId
3. **WebSocket Connection**: SSL WebSocket to `/roomId?sessionId=sessionId`
4. **Protocol Handshake**: Binary Colyseus protocol for room join
5. **Voice Communication**: Push-to-talk with real-time audio streaming

### Colyseus Protocol Implementation
```cpp
namespace ColyseusProtocol {
    static const uint8_t HANDSHAKE = 9;
    static const uint8_t JOIN_ROOM = 10;
    static const uint8_t ERROR = 11;
    static const uint8_t ROOM_DATA = 13;        // Push-to-talk messages
    static const uint8_t ROOM_DATA_BYTES = 17;  // Audio data
}
```

### Threading Model
- **Audio Thread**: Real-time push-to-talk detection and audio transmission
- **WebSocket Thread**: SSL socket connection management and message processing
- **GUI Thread**: Connection status updates and user interaction

### Voice Processing Pipeline
1. **Input Monitoring**: Continuous audio level detection in `processBlock()`
2. **Push-to-Talk**: Parameter-driven "push" and "talk" message sending
3. **Audio Encoding**: Float samples to binary for WebSocket transmission
4. **Audio Mixing**: Incoming voice data mixed with output buffer
5. **Status Tracking**: Real-time connection state and error reporting

## Raw SSL WebSocket Implementation

### Proven Architecture (Working)
Based on successful `colyseus-vst` reference implementation:
- Manual HTTPS POST for Colyseus matchmaking using raw SSL sockets
- WebSocket handshake with proper masking for client frames
- Binary frame processing with Colyseus protocol constants
- Thread-safe audio message queue for real-time mixing

### Connection Management
```cpp
// Manual HTTP matchmaking
SSL_CTX* ssl_ctx = SSL_CTX_new(TLS_client_method());
// ... SSL socket creation and connection
std::string request = 
    "POST /matchmake/joinOrCreate/my_room HTTP/1.1\r\n"
    "Host: voice.latticeworks-ai.com\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: 2\r\n"
    "Connection: close\r\n\r\n"
    "{}";

// WebSocket connection to returned room
String wsPath = "/" + roomId + "?sessionId=" + sessionId;
```

## Development Workflows

### Testing Changes
1. Build plugins: `make -j4` 
2. Test standalone: `timeout 30s "./FLStreamPlugin_artefacts/Standalone/FL Stream Plugin.app/Contents/MacOS/FL Stream Plugin"`
3. Test generator: `timeout 30s "./FLStreamGenerator_artefacts/Standalone/FL Stream Generator.app/Contents/MacOS/FL Stream Generator"`
4. Verify connection: Look for `"sessionId"` in console output
5. Load VST3/AU in DAW for integration testing
6. Test push-to-talk parameter automation

### Voice Collaboration Testing  
- Connect multiple instances to same room name
- Test push-to-talk state changes with parameter automation
- Monitor audio level mixing from multiple clients
- Verify SSL certificate handling with voice.latticeworks-ai.com

### Network Debugging
- Console output shows detailed connection flow and errors
- HTTP matchmaking response parsing with JSON room data
- WebSocket handshake success/failure logging
- SSL connection error reporting with OpenSSL error strings

## Current Status (Sept 2025)

### ✅ Working Implementation Confirmed
- **Colyseus Matchmaking**: Successfully connects to `voice.latticeworks-ai.com` HTTPS endpoint
- **Room Reservation**: Receives proper JSON response with roomId/sessionId
- **SSL WebSocket**: Raw socket implementation handles secure connections
- **Push-to-Talk**: Parameter-driven voice activation with binary protocol
- **Audio Processing**: Real-time mixing of incoming voice data from other clients

### Recent Technical Achievements (v1.1.0)
- ✅ Added FL Stream Generator plugin with synthesizer capabilities
- ✅ External audio input support for side-chaining (like modern synths)
- ✅ Resolved all UTF-8 encoding issues and emoji characters
- ✅ Improved code documentation with technical specifications
- ✅ ASCII-only interface elements for cross-platform compatibility
- ✅ Enhanced bus configuration supporting both effect and generator modes
- ✅ Conditional compilation for plugin type differentiation

### Test Evidence
```
FL Stream: ColyseusRoomClient initialized with SSL WebSocket implementation
FL Stream: [OK] WebSocket handshake successful
FL Stream: [OK] Sent JOIN_ROOM message (Colyseus protocol format)
FL Stream: Matchmaking response: {
  "room": {"clients": 1, "roomId": "hcvmbgnyY", "sessionId": "vhl0Z7A_T"}
}
```

## Developer Principles

### Connection Requirements
- Always use HTTPS for Colyseus matchmaking (port 443)
- WebSocket connections must use WSS with proper SSL certificates  
- Never simulate or mock network functionality - use real connections
- Implement comprehensive error reporting and status tracking
- Test with actual voice.latticeworks-ai.com production server

### Development Philosophy
- Real implementation over simplified versions
- Surgical code changes rather than complete rewrites
- Evidence-based verification through actual testing
- No TODOs left in production code
- ASCII-only text elements for UTF-8 compatibility

### Code Quality Standards
- No emoji characters or Unicode symbols in source code
- Technical documentation over metareferential comments
- Specific functionality descriptions over generic terms
- Professional console output with ASCII status indicators
- Self-documenting code with clear purpose statements

## Version Information
- **Current Version**: 1.1.0
- **JUCE Version**: 7.x
- **CMake Minimum**: 3.22
- **C++ Standard**: 17
- **Target Platforms**: macOS, Windows, Linux
- **Plugin Formats**: VST3, AU, Standalone