# JUCE Code Pattern Extraction for Pitch Detection App

## Critical Patterns Identified

### 1. AudioRecordingDemo.h - Microphone Input Foundation

**🎯 SNIPE: AudioIODeviceCallback Implementation**
```cpp
class PitchDetector final : public AudioIODeviceCallback
{
public:
    void audioDeviceAboutToStart (AudioIODevice* device) override
    {
        sampleRate = device->getCurrentSampleRate();
        // Initialize pitch detection buffers based on sample rate
    }

    void audioDeviceStopped() override
    {
        sampleRate = 0;
    }

    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples, const AudioIODeviceCallbackContext& context) override
    {
        // SNIPE: Direct audio processing - no file writing overhead
        if (numInputChannels > 0 && inputChannelData[0] != nullptr)
        {
            processSamplesForPitchDetection(inputChannelData[0], numSamples);
        }

        // Clear output buffers (no audio output needed)
        for (int i = 0; i < numOutputChannels; ++i)
            if (outputChannelData[i] != nullptr)
                FloatVectorOperations::clear (outputChannelData[i], numSamples);
    }

private:
    double sampleRate = 0.0;
    void processSamplesForPitchDetection(const float* samples, int numSamples);
};
```

**🎯 SNIPE: Runtime Permissions Pattern**
```cpp
// FROM: AudioRecordingDemo lines 271-277
RuntimePermissions::request (RuntimePermissions::recordAudio,
                             [this] (bool granted)
                             {
                                 int numInputChannels = granted ? 2 : 0;
                                 audioDeviceManager.initialise (numInputChannels, 2, nullptr, true, {}, nullptr);
                             });
```

**🎯 SNIPE: AudioDeviceManager Setup**
```cpp
// FROM: AudioRecordingDemo lines 279-280
audioDeviceManager.addAudioCallback (&pitchDetector);
// Clean shutdown in destructor:
audioDeviceManager.removeAudioCallback (&pitchDetector);
```

### 2. SimpleFFTDemo.h - Real-Time Audio Analysis Patterns

**🎯 SNIPE: FIFO Buffer System (Adapt for Pitch Detection)**
```cpp
// FROM: SimpleFFTDemo lines 133-150
class PitchDetectionBuffer
{
    static constexpr int bufferSize = 2048; // Power of 2 for autocorrelation efficiency
    
    void pushNextSampleIntoFifo (float sample) noexcept
    {
        if (fifoIndex == bufferSize)
        {
            if (!nextBlockReady)
            {
                // Copy FIFO to processing buffer
                std::memcpy(processingBuffer, fifo, bufferSize * sizeof(float));
                nextBlockReady = true;
            }
            fifoIndex = 0;
        }
        fifo[fifoIndex++] = sample;
    }

private:
    float fifo[bufferSize];
    float processingBuffer[bufferSize];
    int fifoIndex = 0;
    bool nextBlockReady = false;
};
```

**🎯 SNIPE: Timer-Based GUI Updates**
```cpp
// FROM: SimpleFFTDemo lines 58, 81, 123-131
class PitchDetectorComponent : public Component, private Timer
{
public:
    PitchDetectorComponent() 
    {
        startTimerHz(60); // 60 FPS updates
    }
    
    void timerCallback() override
    {
        if (newPitchDataReady)
        {
            updatePitchDisplay();
            newPitchDataReady = false;
            repaint();
        }
    }
};
```

**🎯 SNIPE: Real-Time Processing Flag Pattern**
```cpp
// FROM: SimpleFFTDemo lines 125-131
// Use atomic flag for thread-safe communication between audio and GUI threads
std::atomic<bool> nextBlockReady { false };
std::atomic<bool> newPitchDataReady { false };
```

### 3. AudioPlaybackDemo.h - GUI Integration Patterns

**🎯 SNIPE: AudioAppComponent Structure**
```cpp
// FROM: AudioPlaybackDemo (inferred from AudioRecordingDemo usage)
class PitchDetectorApp : public AudioAppComponent
{
public:
    PitchDetectorApp()
    {
        #ifndef JUCE_DEMO_RUNNER
        RuntimePermissions::request (RuntimePermissions::recordAudio,
                                     [this] (bool granted)
                                     {
                                         int numInputChannels = granted ? 1 : 0; // Mono input
                                         setAudioChannels (numInputChannels, 0); // No output
                                     });
        #else
        setAudioChannels (1, 0);
        #endif
    }

    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override
    {
        pitchDetector.setSampleRate(sampleRate);
        pitchDetector.prepareBuffers(samplesPerBlockExpected);
    }

    void getNextAudioBlock (const AudioSourceChannelInfo& bufferToFill) override
    {
        if (bufferToFill.buffer->getNumChannels() > 0)
        {
            const auto* channelData = bufferToFill.buffer->getReadPointer (0, bufferToFill.startSample);
            pitchDetector.processBlock(channelData, bufferToFill.numSamples);
        }
        bufferToFill.clearActiveBufferRegion();
    }

    void releaseResources() override
    {
        pitchDetector.reset();
    }
};
```

**🎯 SNIPE: Component Layout Pattern**
```cpp
// FROM: AudioRecordingDemo lines 296-304
void resized() override
{
    auto area = getLocalBounds();
    
    noteNameLabel.setBounds (area.removeFromTop (120).reduced (8));     // Large note display
    frequencyLabel.setBounds (area.removeFromTop (40).reduced (8));     // Frequency readout  
    tuningMeter.setBounds (area.removeFromTop (80).reduced (8));        // Visual tuning indicator
    centsLabel.setBounds (area.removeFromTop (40).reduced (8));         // Cents deviation
}
```

## Key Pattern Hybridization Strategy

### Core Architecture Fusion
```cpp
class PitchDetectorComponent : public AudioAppComponent,  // From AudioPlaybackDemo pattern
                              private Timer              // From SimpleFFTDemo pattern
{
    // Combine AudioRecordingDemo's mic input + SimpleFFTDemo's analysis + custom pitch detection
};
```

### Critical Code Stitching Points

1. **Audio Callback Chain**: 
   - AudioRecordingDemo's `audioDeviceIOCallbackWithContext` → Custom pitch processing
   - Remove file writing, add pitch analysis

2. **Buffer Management**:
   - SimpleFFTDemo's FIFO system → Adapted for autocorrelation windows
   - Keep circular buffer pattern, change processing algorithm

3. **GUI Updates**:
   - SimpleFFTDemo's timer callback → Note/frequency display updates
   - Remove spectrogram, add musical note visualization

4. **Permissions & Setup**:
   - AudioRecordingDemo's runtime permissions → Direct copy
   - AudioDeviceManager setup → Simplified (no output channels)

### Surgical Edit Strategy

**Phase 1 - Foundation (AudioRecordingDemo base):**
```cpp
// SNIPE: Remove AudioRecorder class entirely
// SNIPE: Remove RecordingThumbnail class entirely  
// SNIPE: Keep AudioDeviceManager setup pattern
// SNIPE: Keep runtime permissions exactly as-is
// SNIPE: Modify audioDeviceIOCallbackWithContext for pitch processing
```

**Phase 2 - Analysis (SimpleFFTDemo patterns):**
```cpp
// SNIPE: Copy FIFO buffer system, adapt buffer size for pitch detection
// SNIPE: Copy Timer pattern for GUI updates
// SNIPE: Remove FFT, replace with autocorrelation algorithm
// SNIPE: Keep atomic flag pattern for thread safety
```

**Phase 3 - GUI (Custom + Layout patterns):**
```cpp
// SNIPE: AudioPlaybackDemo's component structure
// SNIPE: AudioRecordingDemo's resized() layout pattern
// SNIPE: Custom pitch display components
```

## Pitch Detection Algorithm Integration Point

**Replace SimpleFFTDemo's FFT processing with:**
```cpp
// INSTEAD OF: forwardFFT.performFrequencyOnlyForwardTransform (fftData);
float detectedFrequency = autocorrelationPitchDetection(processingBuffer, bufferSize, sampleRate);
NoteInfo noteInfo = NoteConverter::frequencyToNote(detectedFrequency);

// Thread-safe update to GUI thread
{
    const ScopedLock sl (pitchDataLock);
    currentNoteInfo = noteInfo;
    newPitchDataReady = true;
}
```

## Memory Management Patterns

**From AudioRecordingDemo:**
- `const ScopedLock sl (writerLock);` → `const ScopedLock sl (pitchDataLock);`
- Thread-safe data sharing between audio and GUI threads

**From SimpleFFTDemo:**
- Stack-allocated audio buffers (no dynamic allocation in audio thread)
- `zeromem()` and `memcpy()` for efficient buffer operations

## Build Dependencies Consolidation

**Required JUCE modules (intersection of all examples):**
- `juce_audio_basics` ✅
- `juce_audio_devices` ✅  
- `juce_audio_utils` ✅
- `juce_core` ✅
- `juce_gui_basics` ✅
- `juce_events` ✅

**Not needed:**
- `juce_dsp` (SimpleFFTDemo) - using custom autocorrelation
- `juce_audio_formats` (AudioRecordingDemo) - no file I/O
- `juce_gui_extra` - basic GUI only

## Next Implementation Steps

1. **Create base project structure** using AudioRecordingDemo as template
2. **Strip out recording functionality**, keep audio input pipeline  
3. **Integrate SimpleFFTDemo's buffer system** with custom pitch detection
4. **Add musical note conversion logic**
5. **Create pitch display GUI components**
6. **Test and optimize for real-time performance**