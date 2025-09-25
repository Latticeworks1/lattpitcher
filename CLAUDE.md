# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Architecture

This is a JUCE-based FL Stream Plugin for real-time voice collaboration with Colyseus WebSocket integration:
- **Audio Plugin**: VST3/AU/Standalone plugin for DAW and standalone voice chat
- **Colyseus Integration**: Real-time multiplayer voice communication over WebSocket

### Core Components

- `FLStreamProcessor`: Main audio processor with push-to-talk and Colyseus integration
- `FLStreamEditor`: Plugin GUI with connection status and voice controls
- `ColyseusRoomClient`: Raw SSL WebSocket client for Colyseus protocol communication

### Key Data Structures
- `AudioMessage`: Incoming voice data from other clients with session info
- `ColyseusProtocol`: Protocol constants for room management and voice data

## Build System

### CMake Configuration
Uses JUCE's modern CMake API with OpenSSL and WebSocket dependencies:

```cmake
# FL Stream Plugin (VST3/AU/Standalone)
juce_add_plugin(FLStreamPlugin)
```

### Build Commands
```bash
# From FLStreamPlugin/build directory:
mkdir -p build && cd build
cmake ..
make -j4

# Test standalone plugin:
timeout 30s ./FLStreamPlugin_artefacts/Standalone/FL\ Stream\ Plugin.app/Contents/MacOS/FL\ Stream\ Plugin

# Plugin installs to:
# VST3: /Users/m1a4xnetworkprobe./Library/Audio/Plug-Ins/VST3/
# AU: /Users/m1a4xnetworkprobe./Library/Audio/Plug-Ins/Components/
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

# Audio codec
${OPUS_LIBRARIES}
webm
```

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
1. Build plugin: `make -j4` 
2. Test connection: `timeout 30s ./FLStreamPlugin_artefacts/Standalone/FL\ Stream\ Plugin.app/Contents/MacOS/FL\ Stream\ Plugin`
3. Verify matchmaking success: Look for `"sessionId"` in console output
4. Load VST3/AU in DAW for integration testing
5. Test push-to-talk parameter automation

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

### Recent Technical Achievements
- ✅ Replaced broken WebSocket++ with proven raw SSL implementation
- ✅ Integrated working `colyseus-vst` reference client code
- ✅ Added comprehensive connection status tracking and error reporting
- ✅ Implemented manual HTTP POST for Colyseus matchmaking protocol
- ✅ Added OpenSSL dependency and SSL certificate handling
- ✅ Built and tested successful connection to production Colyseus server

### Test Evidence
```
FL Stream: Performing HTTP matchmaking for room: my_room
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