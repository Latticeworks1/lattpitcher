# Component Integration Strategy for Pitch Detection App

## Architecture Fusion Analysis

### Primary Base: AudioRecordingDemo (60% foundation)
**Rationale**: Provides complete microphone input pipeline, device management, and runtime permissions - exactly what we need.

**SURGICAL EXTRACTION PLAN:**
- ✅ **KEEP**: AudioDeviceManager setup (lines 308-312)
- ✅ **KEEP**: Runtime permissions (lines 271-277) 
- ✅ **KEEP**: audioDeviceIOCallbackWithContext signature (lines 145-167)
- ❌ **REMOVE**: AudioRecorder class entirely (lines 62-178)
- ❌ **REMOVE**: RecordingThumbnail class entirely (lines 181-238)
- ❌ **REMOVE**: File I/O operations
- 🔧 **MODIFY**: Audio callback to process samples for pitch detection instead of recording

### Secondary Integration: SimpleFFTDemo (30% patterns)
**Rationale**: Provides real-time audio analysis framework and GUI update patterns.

**SURGICAL EXTRACTION PLAN:**
- ✅ **KEEP**: FIFO buffer system (lines 133-150) - adapt buffer size
- ✅ **KEEP**: Timer-based updates (lines 58, 81, 123-131)
- ✅ **KEEP**: Atomic flags for thread safety (line 192)
- ❌ **REMOVE**: FFT processing entirely (lines 161-177)
- ❌ **REMOVE**: Spectrogram visualization
- 🔧 **MODIFY**: Buffer size from 1024 to 2048 for pitch detection
- 🔧 **MODIFY**: Processing function to autocorrelation algorithm

### Tertiary Patterns: AudioPlaybackDemo (10% structure)
**Rationale**: Provides AudioAppComponent integration patterns.

**SURGICAL EXTRACTION PLAN:**
- ✅ **KEEP**: AudioAppComponent inheritance structure
- ✅ **KEEP**: Component layout patterns
- ❌ **REMOVE**: File loading/playback functionality

## Hybrid Class Design

```cpp
class PitchDetectorApp final : public AudioAppComponent,  // AudioPlaybackDemo pattern
                              private Timer              // SimpleFFTDemo pattern
{
public:
    PitchDetectorApp();
    ~PitchDetectorApp() override;

    // AudioAppComponent overrides (AudioPlaybackDemo pattern)
    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock (const AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;

    // Component overrides  
    void paint (Graphics& g) override;
    void resized() override;

private:
    // Timer callback (SimpleFFTDemo pattern)
    void timerCallback() override;
    
    // Pitch detection methods (custom)
    void processSamplesForPitchDetection(const float* samples, int numSamples);
    float autocorrelationPitchDetection(const float* buffer, int size, double sampleRate);
    
    // FIFO buffer system (SimpleFFTDemo adapted)
    void pushNextSampleIntoFifo(float sample) noexcept;
    
    // Audio processing members
    static constexpr int bufferSize = 2048;
    float fifo[bufferSize];
    float processingBuffer[bufferSize];
    int fifoIndex = 0;
    std::atomic<bool> nextBlockReady { false };
    
    // Pitch detection results
    struct PitchInfo {
        float frequency;
        String noteName;
        int octave;
        float centsDeviation;
        bool isValid;
    };
    
    PitchInfo currentPitch;
    std::atomic<bool> newPitchDataReady { false };
    CriticalSection pitchDataLock;
    
    // GUI components
    Label noteNameLabel;
    Label frequencyLabel;
    Label centsLabel;
    Component tuningMeter;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PitchDetectorApp)
};
```

## Surgical Implementation Sequence

### Step 1: Create Base Structure (AudioRecordingDemo extraction)
```cpp
// File: PitchDetectorApp.h
// START with AudioRecordingDemo.h
// SURGICAL EDITS:

// 1. Remove lines 62-178 (AudioRecorder class)
// 2. Remove lines 181-238 (RecordingThumbnail class)  
// 3. Rename AudioRecordingDemo → PitchDetectorApp (line 241)
// 4. Remove recordButton, explanationLabel, chooser members
// 5. Add Timer inheritance: public Component, private Timer
// 6. Modify constructor: remove button setup, add Timer start
// 7. Modify audioDeviceIOCallbackWithContext implementation
```

### Step 2: Integrate FIFO System (SimpleFFTDemo extraction)
```cpp
// SURGICAL EDITS:

// 1. Copy lines 189-192 from SimpleFFTDemo (buffer members)
// 2. Copy lines 133-150 (pushNextSampleIntoFifo function)
// 3. Copy lines 123-131 (timerCallback function)
// 4. Modify buffer size: fftOrder=10, fftSize=1024 → bufferSize=2048
// 5. Remove FFT-specific code, add pitch detection placeholder
```

### Step 3: Add Pitch Detection Algorithm
```cpp
// NEW IMPLEMENTATION (custom autocorrelation):

float PitchDetectorApp::autocorrelationPitchDetection(const float* buffer, int size, double sampleRate)
{
    // Autocorrelation algorithm implementation
    std::vector<float> autocorr(size / 2);
    
    // Calculate autocorrelation
    for (int lag = 1; lag < size / 2; ++lag) {
        float sum = 0.0f;
        for (int i = 0; i < size - lag; ++i) {
            sum += buffer[i] * buffer[i + lag];
        }
        autocorr[lag] = sum;
    }
    
    // Find peak (fundamental frequency)
    int maxLag = 0;
    float maxVal = 0.0f;
    int minPitch = (int)(sampleRate / 800.0);  // 800 Hz max
    int maxPitch = (int)(sampleRate / 80.0);   // 80 Hz min
    
    for (int lag = minPitch; lag < maxPitch && lag < autocorr.size(); ++lag) {
        if (autocorr[lag] > maxVal) {
            maxVal = autocorr[lag];
            maxLag = lag;
        }
    }
    
    return maxLag > 0 ? (float)sampleRate / maxLag : 0.0f;
}
```

### Step 4: Add Musical Note Conversion
```cpp
// NEW IMPLEMENTATION (musical note logic):

struct NoteInfo {
    String noteName;
    int octave;  
    float centsDeviation;
};

NoteInfo frequencyToNote(float frequency) {
    if (frequency <= 0) return {"--", 0, 0.0f};
    
    // A4 = 440 Hz reference
    float A4 = 440.0f;
    float C0 = A4 * std::pow(2.0f, -4.75f); // C0 reference
    
    float halfStepsFromC0 = 12.0f * std::log2(frequency / C0);
    int midiNote = (int)std::round(halfStepsFromC0);
    
    float centsDeviation = (halfStepsFromC0 - midiNote) * 100.0f;
    
    int octave = midiNote / 12;
    int noteIndex = midiNote % 12;
    
    String noteNames[12] = {"C", "C#", "D", "D#", "E", "F", 
                           "F#", "G", "G#", "A", "A#", "B"};
    
    return {noteNames[noteIndex], octave, centsDeviation};
}
```

### Step 5: Create GUI Components
```cpp
// SURGICAL EDIT to resized() function:

void PitchDetectorApp::resized()
{
    auto area = getLocalBounds();
    
    // Large note name display
    noteNameLabel.setBounds(area.removeFromTop(120).reduced(8));
    noteNameLabel.setFont(FontOptions(72.0f, Font::bold));
    noteNameLabel.setJustificationType(Justification::centred);
    
    // Frequency display
    frequencyLabel.setBounds(area.removeFromTop(40).reduced(8));
    frequencyLabel.setFont(FontOptions(24.0f));
    frequencyLabel.setJustificationType(Justification::centred);
    
    // Tuning meter
    tuningMeter.setBounds(area.removeFromTop(80).reduced(8));
    
    // Cents deviation
    centsLabel.setBounds(area.removeFromTop(40).reduced(8));
    centsLabel.setFont(FontOptions(18.0f));
    centsLabel.setJustificationType(Justification::centred);
}
```

## Thread Safety Integration

### From AudioRecordingDemo pattern:
```cpp
// Audio thread → GUI thread communication
void PitchDetectorApp::processSamplesForPitchDetection(const float* samples, int numSamples)
{
    for (int i = 0; i < numSamples; ++i) {
        pushNextSampleIntoFifo(samples[i]);
        
        if (nextBlockReady.load()) {
            float frequency = autocorrelationPitchDetection(processingBuffer, bufferSize, getSampleRate());
            
            if (frequency > 0.0f) {
                const ScopedLock sl(pitchDataLock);
                currentPitch.frequency = frequency;
                auto noteInfo = frequencyToNote(frequency);
                currentPitch.noteName = noteInfo.noteName;
                currentPitch.octave = noteInfo.octave;
                currentPitch.centsDeviation = noteInfo.centsDeviation;
                currentPitch.isValid = true;
                newPitchDataReady = true;
            }
            
            nextBlockReady = false;
        }
    }
}

void PitchDetectorApp::timerCallback()
{
    if (newPitchDataReady.load()) {
        const ScopedLock sl(pitchDataLock);
        
        if (currentPitch.isValid) {
            noteNameLabel.setText(currentPitch.noteName + String(currentPitch.octave), dontSendNotification);
            frequencyLabel.setText(String(currentPitch.frequency, 1) + " Hz", dontSendNotification);
            centsLabel.setText(String(currentPitch.centsDeviation > 0 ? "+" : "") + 
                             String(currentPitch.centsDeviation, 0) + " cents", dontSendNotification);
        }
        
        newPitchDataReady = false;
        repaint();
    }
}
```

## Build System Integration

### CMakeLists.txt (minimal dependencies)
```cmake
cmake_minimum_required(VERSION 3.22)
project(PitchDetector)

add_subdirectory(../JUCE ../JUCE_build)

juce_add_gui_app(PitchDetector
    PRODUCT_NAME "Pitch Detector"
    VERSION "1.0.0"
)

target_sources(PitchDetector PRIVATE
    PitchDetectorApp.h
    PitchDetectorApp.cpp
)

target_link_libraries(PitchDetector PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_utils
    juce::juce_core
    juce::juce_events
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
```

## Performance Optimization Strategy

1. **Audio Thread Efficiency**:
   - Stack-allocated buffers only
   - Minimal computations in audio callback
   - Process in background when buffer is full

2. **GUI Update Efficiency**:
   - 60 FPS timer updates
   - Only repaint when new data available
   - Lock-free atomic flags for status

3. **Algorithm Efficiency**:
   - 2048 sample buffer (good compromise between latency/accuracy)
   - Autocorrelation only when voice detected
   - Restrict pitch range to human vocal range

## Next Action Items

1. Create project directory structure
2. Copy AudioRecordingDemo.h as base template  
3. Apply surgical edits in sequence
4. Add pitch detection algorithm
5. Test basic functionality
6. Optimize and refine GUI