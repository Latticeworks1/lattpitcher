# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Architecture

The relay_server is a high-performance C++ audio relay service designed to facilitate real-time audio streaming between multiple NetworkAudioStreamer clients. It uses a dual-protocol architecture with WebSocket for control signaling and UDP for ultra-low latency audio packet relaying.

### Core Components

- `RelayServer`: Main server orchestrating WebSocket and UDP components
- `SessionManager`: Thread-safe session and client state management
- `WSControlServer`: WebSocket server handling client control messages (join/leave)
- `UDPAudioRelay`: High-performance UDP audio packet forwarding
- `AudioPacket`: Structured binary audio data with session/user identification
- `WSSession`: Individual WebSocket client connection handler

### Architecture Pattern

The server implements a pub-sub relay pattern where:
1. Clients connect via WebSocket to join/leave audio sessions
2. Audio data flows directly via UDP packets with session/user headers
3. Server automatically learns client UDP endpoints from first audio packet
4. All audio packets are relayed to other clients in the same session

### Key Data Structures

- `AudioPacket`: Binary audio data with session/user identification and 1920-byte maximum payload
- `Session`: Container managing multiple clients within an audio session
- `Client`: Individual user state with WebSocket connection and UDP endpoint
- `AudioPacketHeader`: Packed binary header (sessionId, userId, sequence, timestamp, dataSize, channels)

## Build System

### CMake Configuration
Modern CMake with C++17 standard:

```cmake
# Primary executable target
add_executable(relay_server RelayServer.cpp)

# Dependencies: Boost.Asio, nlohmann/json, pthread
target_link_libraries(relay_server Boost::system nlohmann_json::nlohmann_json pthread)
```

### Build Commands
```bash
# Development build
mkdir build && cd build
cmake ..
make

# Production deployment build
./deploy_server.sh

# Manual static build for VPS deployment
mkdir build_deploy && cd build_deploy
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-static-libgcc -static-libstdc++" -DBoost_USE_STATIC_LIBS=ON
make -j$(nproc)
```

### Dependencies
```bash
# macOS
brew install boost nlohmann-json cmake

# Linux
sudo apt-get install libboost-all-dev nlohmann-json3-dev cmake build-essential

# Fedora/CentOS
sudo dnf install boost-devel json-devel cmake gcc-c++
```

## Network Protocol Architecture

### Dual-Protocol Design
- **WebSocket (TCP:8080)**: Control plane for session management (join/leave operations)
- **UDP (9001)**: Data plane for audio packet relaying with minimal latency

### WebSocket Control Messages
```json
// Join session
{"type": "join", "sessionId": 12345, "userId": 67890}

// Leave session  
{"type": "leave", "sessionId": 12345, "userId": 67890}

// Error response
{"type": "error", "message": "description"}
```

### UDP Audio Packet Structure
```cpp
struct AudioPacketHeader {
    uint32_t sessionId;    // Audio session identifier
    uint32_t userId;       // Sending user identifier  
    uint32_t sequence;     // Packet sequence number
    uint32_t timestamp;    // Audio timestamp
    uint16_t dataSize;     // Audio payload size (≤1920 bytes)
    uint8_t channels;      // Audio channel count
    uint8_t reserved;      // Future use
} __attribute__((packed));
```

### Session Management Flow
1. Client opens WebSocket connection to port 8080
2. Client sends `join` message with sessionId/userId
3. Client begins sending UDP audio packets to port 9001
4. Server learns client's UDP endpoint from first audio packet
5. Server relays audio packets to all other clients in same session
6. Client sends `leave` message or disconnects WebSocket

## Threading and Concurrency

### Thread Architecture
- **Main Thread**: Boost.Asio event loop handling both WebSocket and UDP operations
- **Asynchronous I/O**: All network operations use Boost.Asio async patterns
- **Thread Safety**: `SessionManager` uses `std::mutex` for safe concurrent access

### Memory Management
- RAII with `std::shared_ptr` for WebSocket session lifecycle
- Stack-allocated audio packet buffers for zero-allocation UDP processing
- Weak references to prevent circular dependencies in session management

## Development Workflows

### Local Testing
```bash
# Build and run server
mkdir build && cd build
cmake .. && make
./relay_server

# Server runs on:
# WebSocket Control: ws://localhost:8080
# UDP Audio Relay: udp://localhost:9001
```

### Production Deployment
```bash
# Create deployable package
./deploy_server.sh

# Deploy to VPS
scp -r relay_deploy/ user@your-vps.com:~/
ssh user@your-vps.com
cd relay_deploy && ./start_relay.sh
```

### Testing with NetworkAudioStreamer Clients
1. Start relay server: `./relay_server`
2. Configure NetworkAudioStreamer plugin clients to connect to relay server IP
3. Set same sessionId in multiple clients to create audio session
4. Verify audio routing between clients through server logs

## Configuration

### Server Configuration (`deploy_config.json`)
```json
{
  "server": {
    "websocket_port": 8080,     // WebSocket control port
    "udp_port": 9001,           // UDP audio relay port  
    "max_sessions": 100,        // Maximum concurrent sessions
    "max_clients_per_session": 8 // Maximum clients per session
  },
  "audio": {
    "max_packet_size": 2048,    // Maximum UDP packet size
    "packet_timeout_ms": 5000   // Client timeout threshold
  }
}
```

### Network Requirements
- **Port 8080/TCP**: WebSocket control (must be accessible to clients)
- **Port 9001/UDP**: Audio relay (must be accessible to clients)
- **Firewall**: Both ports must allow incoming connections
- **Bandwidth**: ~512kbps per stereo audio stream at 48kHz

## Platform-Specific Notes

### Linux Production Deployment
- Use `systemd` service for automatic startup
- Configure firewall: `ufw allow 8080/tcp && ufw allow 9001/udp`
- Static linking recommended for VPS deployment compatibility

### macOS Development
- Boost installed via Homebrew: `/opt/homebrew/include`
- Automatic dependency discovery through pkg-config
- XCode toolchain for native compilation

### Performance Characteristics
- **Latency**: ~1-5ms relay latency (UDP forwarding)
- **Throughput**: 1000+ audio packets/second per session
- **Memory**: ~10MB baseline + ~1KB per active client
- **CPU**: <1% on modern hardware for typical loads

This relay server is designed for production deployment on VPS infrastructure to enable internet-based real-time audio collaboration between NetworkAudioStreamer plugin instances.