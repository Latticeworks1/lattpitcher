# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Architecture

This is a JUCE-based real-time pitch detection audio application with dual-target architecture:
- **Standalone Application**: Cross-platform GUI app for live pitch analysis
- **Audio Plugin**: VST3/AU plugin for DAW integration

### Core Components

- `PitchDetectionEngine`: Core pitch detection algorithms (YIN, HPS, Cepstrum, Autocorrelation)
- `PitchDetectorGUI`: Professional VST-style glassmorphism interface with neural autotune controls
- `PitchDetectorProcessor`: Audio plugin processor with autotune and neural processing
- `PitchDetectorEditor`: Plugin editor wrapper (800x550 professional VST interface)
- `AutotuneEngine`: Real-time pitch correction with scale-aware processing
- `NeuralSpectralAutotuneEngine`: Advanced neural-network-based spectral synthesis
- `StandalonePitchDetector`: Standalone app audio management component

### Key Data Structures
- `NoteInfo`: Musical note representation with cents deviation
- `DetectionResult`: Real-time pitch detection results
- `TelemetryData`: Performance metrics and usage analytics

## Build System

### CMake Configuration
Uses JUCE's modern CMake API with dual targets:

```cmake
# Standalone GUI Application
juce_add_gui_app(PitchDetectorApp)

# Audio Plugin (VST3/AU/Standalone)
juce_add_plugin(PitchDetectorPlugin)
```

### Build Commands
```bash
# From PitchDetector directory:
mkdir -p build && cd build
cmake ..
make

# Run standalone app:
./PitchDetectorApp_artefacts/PitchDetectorApp

# Plugin builds to:
# VST3: /Users/m1a4xnetworkprobe./Library/Audio/Plug-Ins/VST3
# AU: /Users/m1a4xnetworkprobe./Library/Audio/Plug-Ins/Components
```

### JUCE Dependencies
```cmake
juce::juce_audio_basics
juce::juce_audio_devices  
juce::juce_audio_formats
juce::juce_audio_utils
juce::juce_audio_processors  # Plugin target only
juce::juce_core
juce::juce_events
juce::juce_gui_basics
```

## Audio Processing Architecture

### Real-time Processing Flow
1. **Audio Input**: Microphone → `AudioDeviceManager` → processing buffer
2. **FIFO Buffering**: Thread-safe audio data transfer via circular buffer
3. **Pitch Detection**: Multiple algorithms with stability filtering
4. **GUI Updates**: Timer-based UI refresh at 60fps
5. **Telemetry**: Performance metrics collection

### Threading Model
- **Audio Thread**: Real-time audio processing in `processBlock()` with autotune processing
- **GUI Thread**: Timer-driven display updates via `timerCallback()` at 60fps
- **Critical Sections**: Thread-safe result passing between audio and GUI
- **Neural Processing**: Advanced spectral synthesis with evolutionary adaptation

### Autotune Processing Pipeline
1. **Input Analysis**: Multi-algorithm pitch detection (YIN, HPS, Cepstrum)
2. **Scale Mapping**: User-defined musical scales (Chromatic, Major, Minor, etc.)
3. **Pitch Correction**: Real-time frequency shifting with formant preservation
4. **Neural Enhancement**: Optional spectral synthesis with cochlear modeling
5. **Output Mixing**: Wet/dry blend with original signal

## Key JUCE Patterns

### Component Hierarchy
```
PitchDetectorApplication (JUCEApplication)
├── MainWindow (DocumentWindow)
    └── StandalonePitchDetector (AudioAppComponent)
        └── PitchDetectorGUI (Component, Timer)
```

### Audio Plugin Structure
```
PitchDetectorProcessor (AudioProcessor)
└── PitchDetectorEditor (AudioProcessorEditor)
    └── PitchDetectorGUI (Component)
```

### Memory Management
- RAII with `std::unique_ptr` for window management
- `JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR` macros on all classes
- Automatic component cleanup via JUCE parent-child relationships

## Development Workflows

### Testing Changes
1. Build both targets to ensure compatibility: `make -j4`
2. Test standalone app: `open "PitchDetectorApp_artefacts/Debug/Pitch Detector.app"`
3. Load VST3 plugin in DAW from: `/Users/m1a4xnetworkprobe./Library/Audio/Plug-Ins/VST3/`
4. Load AU plugin in DAW from: `/Users/m1a4xnetworkprobe./Library/Audio/Plug-Ins/Components/`
5. Verify interface responsiveness and visual feedback
6. Test autotune processing with various musical scales
7. Monitor neural processing indicators for real-time feedback

### Audio Algorithm Development  
- Modify `PitchDetectionEngine` for core algorithm changes
- Use telemetry system for performance profiling
- Test across different sample rates (44.1kHz, 48kHz, 96kHz)
- Validate with various audio sources (instruments, voice, synthetic)

### GUI Development
- Professional VST-style interface with Serum-inspired glassmorphism design
- 800x550 optimized layout for both standalone and plugin modes
- Rotary knobs with proper mouse drag sensitivity and visual feedback
- Real-time neural processing indicators with animated metrics
- Dynamic neon glow effects for active processing modes
- Manual label positioning for professional VST workflow

## Current Interface Layout (VST/Standalone)

### Professional Glassmorphism Design (800x550)
```
🔥 MAIN DISPLAY CARD (100px)
├── Note Name Display (60px) - Large centered note/octave 
├── Frequency | Cents Deviation (20px) - Live pitch analysis
└── Audio Level Meter (20px) - Input signal monitoring

🔥 MASTER PROCESSING TOGGLES (60px)
├── 🧠 Neural Mode ON/OFF - Advanced spectral synthesis
└── 🎵 Autotune ON/OFF - Real-time pitch correction

🔥 PRIMARY CONTROL KNOBS (60px)
├── Neural Intensity - Processing strength (0.0-2.0)
├── Correction Strength - Pitch correction amount (0.0-1.0)
├── Correction Speed - Response time (0.0-1.0)
└── Mix Amount - Wet/dry blend (0.0-1.0)

🔥 SECONDARY MUSICAL CONTROLS (40px)
├── Scale Selection - Chromatic/Major/Minor/Blues/etc.
├── Root Note - Tonal center (C-B)
└── Cochlear Sensitivity - Neural filter response (0.0-1.0)

🔥 LIVE NEURAL INDICATORS (35px)
├── Pitch Certainty - Detection confidence
├── Cochlear Excitation - Biological filter simulation
├── Evolution Generation - Algorithm adaptation cycles
└── Adaptation Level - Learning progress
```

### Recent Interface Fixes (Sept 2025)
- ✅ Fixed VST editor size from 400x300 to professional 800x550
- ✅ Removed unnecessary record button from plugin version
- ✅ Fixed all slider responsiveness issues (removed problematic reduced() calls)
- ✅ Converted all controls to rotary knobs with Serum-style colors
- ✅ Fixed label positioning conflicts with attachToComponent()
- ✅ Added manual label positioning for all controls
- ✅ Fixed missing audioLevelLabel positioning in layout
- ✅ Hidden debug/telemetry labels properly in VST mode
- ✅ Added dynamic neon glow effects for active processing modes
- ✅ Implemented proper glassmorphism background rendering

### Control Styling
- **Colors**: NEON_ACCENT (cyan), NEON_PURPLE, GLASS_BG, GLASS_BORDER
- **Drag Sensitivity**: 120 pixels for continuous controls, 80 for discrete
- **Visual Feedback**: Real-time button state updates with emoji indicators
- **Background**: Serum-inspired dark gradient with subtle horizontal lines

## Platform-Specific Notes

### macOS
- Microphone permissions required (handled in CMakeLists.txt)
- Plugin installation paths configured for user library
- Native title bar enabled for consistent system integration

### Mobile Considerations  
- Fullscreen mode enabled for iOS/Android builds
- Touch-friendly control sizing in responsive layouts
- reflect, plan, think, research, act, reflect on action, think, proceed or loop back. Always use batch scripting when you notice patterns where you can make the change surgiclaly and automatically at once without memssing things up . mac native automatically handle mac or windows or whatver linux autoatmaitcal , one clean tiny code for ultimate developer