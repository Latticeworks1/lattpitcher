# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Architecture

This is a JUCE-based real-time pitch detection and autotune application with dual-target architecture:
- **Standalone Application**: Cross-platform GUI app for live pitch analysis
- **Audio Plugin**: VST3/AU plugin for DAW integration with automation support

### Unified Architecture (2-File System)

**Source/PitchDetector.h** - Unified header containing:
- `PitchDetectorConstants`: Namespace eliminating ALL magic numbers with meaningful constants
- `PitchDetectionEngine`: Multi-algorithm pitch detection (YIN, HPS, Cepstrum, Autocorrelation) with vocal optimization
- `AutotuneEngine`: Real-time pitch correction with PSOLA, Phase Vocoder, and LPC formant preservation
- `PitchDetectorGUI`: Professional VST-style glassmorphism interface (800x550) with rotary controls
- `PitchDetectorProcessor`: Audio plugin processor with VST automation parameters
- `PitchDetectorEditor`: Plugin editor wrapper managing GUI integration
- `StandalonePitchDetector`: Standalone app audio management with dynamic sample rate detection

**Source/PitchDetector.cpp** - Unified implementation containing all method definitions (2100+ lines)

### Key Data Structures
- `NoteInfo`: Musical note representation with cents deviation
- `AutotuneSettings`: Comprehensive autotune configuration with scales and correction parameters
- `TelemetryData`: Performance metrics and usage analytics
- `VibratoDetectionState`: Real-time vibrato analysis using two-state HMM

## Build System

### CMake Configuration
Simplified JUCE CMake build system with unified source architecture:

```cmake
# Unified source files - NO MORE FRAGMENTATION
set(COMMON_SOURCES
    Source/PitchDetector.cpp
)

# Build both targets
mkdir -p build && cd build
cmake ..
make -j4

# Plugin-only build with local copy
cmake -DLOCAL_PLUGIN_COPY_DIR=ON ..
make PitchDetectorPlugin

# Include experimental tests
cmake -DBUILD_PVTEST=ON -DENABLE_TESTS=ON ..
make && ctest
```

### Target Outputs
- **Standalone**: `build/PitchDetectorApp_artefacts/PitchDetectorApp`
- **VST3**: `~/Library/Audio/Plug-Ins/VST3/Pitch Detector.vst3`
- **AU**: `~/Library/Audio/Plug-Ins/Components/Pitch Detector.component`
- **Packaging**: `make package_plugins` creates distributable archives

### JUCE Dependencies
Core audio processing stack with DSP and plugin support:
```cmake
juce::juce_audio_basics       # Core audio types
juce::juce_audio_devices      # Hardware interface
juce::juce_audio_processors   # Plugin framework (plugin target only)
juce::juce_dsp               # Advanced signal processing
juce::juce_gui_basics        # UI components
```

## Audio Processing Architecture

### Real-time Processing Flow
1. **Audio Input**: Microphone/DAW → `processBlock()` → FIFO buffer (`PROCESSING_FIFO_SIZE = 8192`)
2. **Pitch Detection**: Multi-algorithm analysis with confidence scoring using dynamic sample rates
3. **Autotune Processing**: Scale-aware pitch correction with formant preservation
4. **GUI Updates**: Thread-safe result transfer via `CriticalSection` at `GUI_REFRESH_RATE = 60`
5. **Telemetry**: Performance monitoring with exportable JSON data

### Threading Model
- **Audio Thread**: Real-time processing in `processBlock()` with sub-2ms latency
- **GUI Thread**: Timer-driven display updates maintaining thread safety
- **Parameter Thread**: VST automation via `AudioProcessorValueTreeState`

### Autotune Processing Pipeline
1. **Input Analysis**: Confidence-weighted multi-algorithm pitch detection
2. **Scale Mapping**: Support for Chromatic, Major, Minor, Pentatonic, Blues, Dorian scales
3. **Correction Processing**: PSOLA for natural vocals, Phase Vocoder for instruments
4. **Formant Preservation**: Advanced LPC analysis with bandwidth expansion
5. **Vibrato Handling**: Real-time detection with adaptive correction strength

## Advanced Features

### LPC Formant Preservation
Production-quality vocal processing with:
- 16th-order LPC analysis on 20ms Hamming windows
- Zero-frequency resonator epoch detection
- Bandwidth expansion for stability (ρ=0.98)
- Formant mapping with 25% movement limits
- 10ms crossfade for smooth transitions

### Vibrato Detection System
Real-time vibrato analysis using two-state HMM:
- 6 cents RMS minimum threshold with 0.6 periodicity requirement
- Circular buffering for <40μs processing per 64-sample block
- Adaptive correction attenuation (kv=0.6, svib=0.5)
- Hysteresis-based state transitions (6-frame minimum)

### Scale-Aware Processing
Intelligent musical correction with:
- Custom scale definition support (12-tone boolean arrays)
- Automatic key detection capabilities
- Reference pitch adjustment (A4: 400-480Hz range)
- Glide time control for natural note transitions

## Development Workflows

### Testing Changes
```bash
# Build and test both targets
make -j4
./build/PitchDetectorApp_artefacts/PitchDetectorApp

# Test plugin in DAW
open ~/Library/Audio/Plug-Ins/VST3/

# Run comprehensive tests
ctest --verbose
./build/LPCFormantTest  # Formant preservation validation
```

### Parameter Development
VST automation parameters are managed through `AudioProcessorValueTreeState`:
- Correction strength/speed sliders with proper automation curves
- Scale selection with DAW recall
- Neural mode toggles with real-time switching
- Mix controls with wet/dry processing

### Performance Profiling
Built-in telemetry system tracks:
- Processing time per block (avg/max metrics)
- Detection confidence and frequency ranges
- Note detection patterns and frequency binning
- Session duration and sample throughput

## Platform-Specific Notes

### macOS
- Microphone permissions handled automatically via CMakeLists.txt
- Native title bar and system integration
- Universal binary support for M1/Intel

### Plugin Development
- VST3/AU formats with full automation support
- 800x550 professional interface optimized for DAW workflow
- State save/restore with parameter persistence
- Cross-platform plugin validation via `pluginval`

## Code Patterns

### Magic Number Elimination
All hardcoded values moved to `PitchDetectorConstants` namespace:
```cpp
namespace PitchDetectorConstants {
    static constexpr double DEFAULT_SAMPLE_RATE = 44100.0;
    static constexpr int DEFAULT_BUFFER_SIZE = 512;
    static constexpr int PROCESSING_FIFO_SIZE = 8192;
    static constexpr int EDITOR_WIDTH = 800;
    static constexpr int EDITOR_HEIGHT = 550;
    // ... comprehensive constants for all formerly magic numbers
}
```

### Thread Safety
All audio processing uses proper JUCE patterns with dynamic sample rates:
```cpp
// Audio thread processing with dynamic rates
void processBlock(AudioBuffer<float>& buffer, MidiBuffer&) override
{
    auto currentRate = getSampleRate();  // NO MORE HARDCODED 44100!
    pushSamplesToFifo(buffer.getReadPointer(0), buffer.getNumSamples());
}

// GUI thread updates  
void timerCallback() override
{
    const ScopedLock lock(resultLock);
    // Update display from latest results
}
```

### Memory Management
- RAII with smart pointers for all major components
- `JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR` on all classes
- Circular buffers for real-time processing without allocation

### Parameter Handling
VST parameters use type-safe attachments:
```cpp
parameterTreeState.createAndAddParameter(
    std::make_unique<AudioParameterFloat>("correctionStrength", 
                                         "Correction Strength", 0.0f, 1.0f, 0.8f));
```