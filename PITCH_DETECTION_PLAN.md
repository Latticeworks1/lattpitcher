# Real-Time Pitch Detection Application Plan

## Project Overview
Building a JUCE-based real-time pitch detection application that:
- Takes microphone input
- Detects fundamental frequency (pitch) 
- Converts frequency to musical note name
- Shows current note visually in real-time
- Displays tuning accuracy (cents deviation)

## Architecture Foundation

### Core Components Required
1. **Audio Input System** - Microphone capture and buffering
2. **Pitch Detection Engine** - Fundamental frequency analysis
3. **Musical Note Converter** - Frequency to note/cent conversion
4. **Real-Time GUI** - Visual display with smooth updates
5. **Performance Monitor** - Low-latency processing validation

## Implementation Strategy

### Phase 1: Audio Input Foundation
**Primary Pattern Source**: `AudioRecordingDemo.h`

**Key Implementation Points**:
- AudioDeviceManager setup for microphone access
- AudioIODeviceCallback implementation for real-time processing
- Runtime permissions for mobile microphone access
- Audio buffer handling for incoming samples
- Proper initialization/shutdown sequences

**Critical Code Patterns**:
```cpp
class PitchDetectorComponent : public juce::AudioAppComponent
{
public:
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;
};
```

### Phase 2: Pitch Detection Engine
**Primary Pattern Source**: `SimpleFFTDemo.h` + Custom Algorithm

**Algorithm Selection**: Autocorrelation or YIN algorithm (time-domain)
- More accurate for monophonic pitch detection
- Lower computational overhead than FFT
- Better for real-time performance

**Key Implementation Points**:
- Circular buffer for audio samples
- Autocorrelation function implementation
- Fundamental frequency extraction
- Noise gate for voice activity detection

**Critical Code Patterns**:
```cpp
class PitchDetector
{
public:
    float detectPitch(const float* audioData, int numSamples, double sampleRate);
    void setNoiseThreshold(float threshold);
    bool isVoiceActive() const;
private:
    std::vector<float> circularBuffer;
    float noiseThreshold = 0.01f;
};
```

### Phase 3: Musical Note Conversion
**Primary Pattern Source**: `AudioSynthesiserDemo.h` (MIDI concepts)

**Key Implementation Points**:
- Frequency to MIDI note conversion
- MIDI note to note name mapping
- Cents deviation calculation
- Octave detection and display

**Critical Code Patterns**:
```cpp
class NoteConverter
{
public:
    struct NoteInfo {
        juce::String noteName;
        int octave;
        float centsDeviation;
        int midiNoteNumber;
    };
    
    static NoteInfo frequencyToNote(float frequency);
    static float noteToFrequency(int midiNoteNumber);
};
```

### Phase 4: Real-Time GUI Integration  
**Primary Pattern Source**: `AudioPlaybackDemo.h` + `WidgetsDemo.h`

**Key Implementation Points**:
- Timer-based GUI updates (30-60 FPS)
- Note name display with large, readable font
- Frequency readout
- Visual tuning indicator (meter/needle)
- Color coding for in-tune/out-of-tune feedback

**Critical Code Patterns**:
```cpp
class PitchDisplayComponent : public juce::Component, 
                              public juce::Timer
{
public:
    void paint(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;
    
    void updatePitchInfo(const NoteConverter::NoteInfo& noteInfo);
    
private:
    juce::Label noteNameLabel;
    juce::Label frequencyLabel;
    TuningMeterComponent tuningMeter;
};
```

### Phase 5: Performance Optimization
**Primary Pattern Source**: `AudioLatencyDemo.h`

**Key Implementation Points**:
- Low-latency audio callback optimization
- Efficient pitch detection algorithm
- GUI update rate optimization
- Memory allocation minimization in audio thread

## Technical Implementation Details

### Audio Processing Chain
1. **Input**: Microphone → Audio buffer (512-1024 samples typical)
2. **Analysis**: Autocorrelation pitch detection
3. **Conversion**: Frequency → Note + Cents
4. **Display**: Timer-driven GUI updates

### Pitch Detection Algorithm Choice
**Autocorrelation Method**:
- Time-domain analysis (no FFT overhead)
- Excellent for monophonic sources
- Robust against harmonics
- Real-time friendly

**Alternative**: YIN algorithm for higher accuracy
- Enhanced autocorrelation with better octave detection
- Slightly more complex but superior results

### GUI Layout Strategy
```
┌─────────────────────────────────┐
│        Current Note             │
│           G#4                   │
│                                 │
│       Frequency: 415.3 Hz      │
│                                 │
│    ┌─────────────────────┐     │
│    │  Tuning Meter       │     │
│    │     ●----+----      │     │
│    │   -50¢  0¢  +50¢    │     │
│    └─────────────────────┘     │
│                                 │
│         +12 cents sharp         │
└─────────────────────────────────┘
```

### Performance Targets
- **Latency**: < 50ms input to display
- **Update Rate**: 30 FPS GUI updates
- **Accuracy**: ±5 cents for clean input
- **CPU Usage**: < 10% on target device

## File Structure Plan

### Core Application Files
- `PitchDetectorApp.cpp` - Main application entry
- `PitchDetectorComponent.h/cpp` - Main audio component
- `PitchDetector.h/cpp` - Pitch detection engine
- `NoteConverter.h/cpp` - Musical note conversion
- `PitchDisplayComponent.h/cpp` - Real-time GUI
- `TuningMeterComponent.h/cpp` - Visual tuning indicator

### Build Configuration
- `CMakeLists.txt` - Build system configuration
- Target: Desktop and mobile (iOS/Android)
- JUCE modules: `juce_core`, `juce_audio_devices`, `juce_audio_utils`, `juce_gui_basics`

## Development Phases

### MVP (Minimum Viable Product)
1. Basic microphone input
2. Simple pitch detection
3. Note name display
4. Basic tuning indicator

### Enhanced Features
1. Calibration settings (A440 vs other tuning)
2. Different temperaments
3. Visual waveform display
4. Recording/playback capability
5. Instrument-specific tuning modes

### Advanced Features  
1. Polyphonic pitch detection
2. Chord recognition
3. Historical tuning analysis
4. MIDI output capability

## Risk Mitigation

### Technical Risks
- **Latency issues**: Use smallest possible buffer sizes
- **Pitch detection accuracy**: Implement multiple algorithms for comparison
- **Mobile permissions**: Robust runtime permission handling
- **Performance on low-end devices**: CPU usage profiling and optimization

### Implementation Risks
- **JUCE version compatibility**: Stick to established patterns from examples
- **Cross-platform issues**: Test early and often on target platforms
- **Audio driver issues**: Robust error handling and fallback options

## Success Criteria

### Functional Requirements
- ✅ Real-time pitch detection with < 50ms latency
- ✅ Accurate note identification (±5 cents)
- ✅ Smooth visual feedback (30+ FPS)
- ✅ Works on desktop and mobile platforms

### Performance Requirements
- ✅ CPU usage < 10% during operation
- ✅ Memory usage < 50MB
- ✅ Battery efficient on mobile devices
- ✅ Stable operation for extended periods

## Next Steps
1. Examine AudioRecordingDemo.h implementation patterns
2. Create basic project structure with CMakeLists.txt
3. Implement minimal audio input component
4. Add basic pitch detection algorithm
5. Create simple note display GUI
6. Iterate and optimize for performance