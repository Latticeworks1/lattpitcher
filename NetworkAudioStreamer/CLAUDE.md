# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Architecture

NetworkAudioStreamer is a professional JUCE-based VST3/AU plugin for real-time, low-latency audio streaming between FL Studio instances using WebSockets and Opus codec. It provides ultra-low latency (5-10ms) audio collaboration across local networks and the internet.

### Core Components

- `NetworkAudioProcessor`: Main audio processor handling real-time audio I/O, parameter management, and network coordination
- `NetworkManager`: WebSocket server/client management with uWebSockets integration
- `OpusCodec`: Ultra-low latency audio encoding/decoding (5ms frame sizes)
- `NetworkThread`: Dedicated thread for network operations, separate from real-time audio thread
- `AudioPacket`: Network packet structure for audio data transmission
- `LockFreeQueue<T>`: Thread-safe circular buffer for audio packet queues
- `LatencyCompensation`: FL Studio Plugin Delay Compensation (PDC) integration
- `NetworkAudioEditor`: Professional VST GUI for network configuration

### Key Data Structures
- `AudioPacket`: Contains encoded audio data, sequence numbers, and metadata
- `ConnectionMode`: Server/Client/Inactive operation modes
- `ConnectionStatus`: Network connection state management
- `SequenceNumberManager`: Packet ordering and loss detection

## Build System

### CMake Configuration
Uses JUCE's modern CMake API with three targets:

```cmake
# VST3/AU Plugin for FL Studio
juce_add_plugin(NetworkAudioStreamerPlugin)

# Standalone Application for Testing  
juce_add_gui_app(NetworkAudioStreamerApp)

# Unit Test Suite
juce_add_console_app(NetworkAudioStreamerTests)
```

### Build Commands
```bash
# From NetworkAudioStreamer directory:
./build.sh

# Manual build:
mkdir build && cd build
cmake ..
make -j4

# Run standalone app:
./NetworkAudioStreamerApp_artefacts/NetworkAudioStreamerApp

# Run tests:
./NetworkAudioStreamerTests
```

### Dependencies
```bash
# macOS
brew install opus cmake

# Linux  
sudo apt-get install libopus-dev cmake build-essential

# Windows
vcpkg install opus:x64-windows
```

### JUCE Dependencies
```cmake
juce::juce_audio_basics
juce::juce_audio_devices
juce::juce_audio_formats  
juce::juce_audio_processors  # Plugin target only
juce::juce_audio_utils
juce::juce_dsp
juce::juce_core
juce::juce_events
juce::juce_gui_basics
```

## Audio Processing Architecture

### Real-time Processing Flow
1. **Audio Input**: FL Studio → `processBlock()` → input buffer
2. **Encoding**: Audio → Opus codec (5ms frames) → compressed packets
3. **Network Transmission**: Packets → LockFreeQueue → NetworkThread → WebSocket
4. **Network Reception**: WebSocket → NetworkThread → LockFreeQueue → packets
5. **Decoding**: Compressed packets → Opus decoder → audio buffer
6. **Audio Output**: Processed audio → FL Studio output

### Threading Architecture
- **Audio Thread**: Real-time audio processing in `processBlock()` - NEVER blocks
- **Network Thread**: WebSocket I/O, encoding/decoding, connection management
- **GUI Thread**: Parameter updates, statistics display, user interface updates
- **Lock-Free Communication**: Thread-safe queues between audio and network threads

### Network Protocol
- **Transport**: WebSocket over TCP for reliable delivery
- **Codec**: Opus (IETF RFC 6716) with 5ms frame sizes for ultra-low latency
- **Packet Structure**: Sequence numbers, audio data, metadata
- **Error Handling**: Forward Error Correction (FEC) and Packet Loss Concealment (PLC)

## Key JUCE Patterns

### Plugin Architecture
```
NetworkAudioProcessor (AudioProcessor)
└── NetworkAudioEditor (AudioProcessorEditor)
    └── [Professional VST GUI components]
```

### Parameter Management
```cpp
// AudioProcessorValueTreeState for parameter automation
parameterTreeState(*this, nullptr, "PARAMETERS", createParameterLayout())

// Parameter listeners for real-time updates
parameterTreeState.addParameterListener("mode", this);
parameterTreeState.addParameterListener("port", this);
```

### Memory Management
- RAII with `std::unique_ptr` for network components
- `JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR` macros on all classes
- Lock-free queues for thread-safe audio data transfer

## Development Workflows

### Testing Changes
1. Build all targets: `./build.sh`
2. Test standalone app: `./NetworkAudioStreamerApp --server` / `--client`
3. Load VST3 plugin in FL Studio from: `/Users/m1a4xnetworkprobe./Library/Audio/Plug-Ins/VST3/`
4. Run unit tests: `./NetworkAudioStreamerTests`
5. Test server/client connectivity with multiple FL Studio instances
6. Monitor network statistics and latency metrics

### Network Development
- Modify `NetworkManager` for protocol changes
- Use `NetworkThread` for non-real-time network operations
- Test with `LockFreeQueue` for thread-safe communication
- Validate with network monitoring and packet analysis

### Audio Development
- Extend `OpusCodec` for encoding optimizations
- Modify `NetworkAudioProcessor::processBlock()` for audio processing changes
- Use telemetry system for performance profiling
- Test across different sample rates (44.1kHz, 48kHz)

## Plugin Configuration

### Server Mode (Primary FL Studio Instance)
```
Mode: Server
Port: 9001 (default)
Status: "Connected" with client count
Network Audio: Enabled for transmission
```

### Client Mode (Secondary FL Studio Instances)  
```
Mode: Client
Server Address: 127.0.0.1 (local) or remote IP
Port: 9001 (must match server)
Status: "Connected" to server
```

### Audio Parameters
- **Input Gain**: 0-200% input signal level
- **Output Gain**: 0-200% output signal level  
- **Network Mix**: 0-100% local/network audio blend
- **Target Latency**: 5-100ms desired latency
- **Quality**: Opus complexity (0-10)

## Performance Characteristics

### Latency Profile
- **Codec Latency**: 5ms (Opus frame size)
- **Network Latency**: 1-50ms (depending on connection)
- **Buffer Latency**: 2-10ms (JUCE audio buffer)
- **Total Latency**: 8-65ms typical

### Resource Usage
- **CPU**: 1-5% per instance (depends on quality settings)
- **Memory**: ~50MB per instance
- **Network**: 64-512 kbps per stereo stream
- **Scalability**: 8+ simultaneous clients per server

## Platform-Specific Notes

### macOS
- Automatic plugin installation to user library directories
- Framework dependencies: Foundation, SystemConfiguration
- Microphone permissions handled in CMakeLists.txt

### Windows  
- Winsock2 integration for networking
- Visual Studio 2019+ required for building
- vcpkg for dependency management

### Linux
- ALSA/JACK audio backend support
- apt package dependencies for development
- Cross-platform WebSocket compatibility

## Third-Party Dependencies

### uWebSockets Integration
```bash
# Add as git submodule
git submodule add https://github.com/uNetworking/uWebSockets.git third_party/uWebSockets
cd third_party/uWebSockets
git submodule update --init --recursive
```

### Required Libraries
- **Opus**: Ultra-low latency audio codec (system package)
- **uWebSockets**: High-performance WebSocket library (submodule) 
- **JUCE**: Audio application framework (../JUCE directory)

## Testing Framework

### Unit Tests
Located in `Tests/` directory:
- `NetworkTests.cpp`: WebSocket connection testing
- `OpusCodecTests.cpp`: Audio encoding/decoding validation
- `LatencyTests.cpp`: Performance and timing verification
- `BufferTests.cpp`: Lock-free queue functionality

### Integration Testing
```bash
# Server instance testing
./NetworkAudioStreamerApp --server

# Client instance testing  
./NetworkAudioStreamerApp --client --server-addr=127.0.0.1

# FL Studio plugin testing with multiple instances
```

## Troubleshooting Common Issues

### Build Failures
- Ensure JUCE framework exists at `../JUCE`
- Verify Opus codec installation (`brew install opus` / `apt-get install libopus-dev`)
- Check uWebSockets submodule initialization

### Network Connection Issues
- Verify port availability (default: 9001)
- Check firewall settings for WebSocket connections
- Test with local loopback (127.0.0.1) first
- Ensure single server instance per port

### Audio Processing Issues  
- Check FL Studio buffer size settings (64-128 samples recommended)
- Verify sample rate compatibility (44.1/48kHz)
- Monitor CPU usage and network statistics
- Test with minimal quality settings first

## Code Organization

```
NetworkAudioStreamer/
├── Source/
│   ├── NetworkAudioProcessor.cpp/.h    // Main plugin processor
│   ├── NetworkAudioEditor.cpp/.h       // Plugin GUI
│   ├── NetworkManager.cpp/.h           // WebSocket networking  
│   ├── NetworkThread.cpp/.h            // Network operations thread
│   ├── OpusCodec.cpp/.h                // Audio encoding/decoding
│   ├── AudioPacket.cpp/.h              // Network packet structure
│   ├── LockFreeQueue.h                 // Thread-safe queues
│   ├── LatencyCompensation.cpp/.h      // FL Studio PDC integration
│   └── StandaloneApp.cpp/.h            // Testing application
├── Tests/                              // Unit test suite
├── third_party/uWebSockets/           // WebSocket library
├── CMakeLists.txt                     // Build configuration
├── build.sh                          // Build script
└── README.md                         // Comprehensive documentation
```

This codebase implements professional-grade real-time audio streaming with focus on ultra-low latency, thread safety, and FL Studio integration. All network operations are isolated from the real-time audio thread using lock-free data structures.