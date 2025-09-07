#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "PitchDetectionEngine.h"
#include "AutotuneEngine.h"
#include "NeuralSpectralAutotuneEngine.h"

using namespace juce;

//==============================================================================
#include "PitchDetectorGUI.h"

class PitchDetectorProcessor : public AudioProcessor, 
                                public AudioProcessorValueTreeState::Listener
{
public:
    PitchDetectorProcessor();
    ~PitchDetectorProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (AudioBuffer<float>&, MidiBuffer&) override;

    AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const String getName() const override { return "Pitch Detector"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const String getProgramName (int) override { return {}; }
    void changeProgramName (int, const String&) override {}

    void getStateInformation (MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    void setGui(PitchDetectorGUI* g) { gui = g; }

    void startStopRecording();
    void saveRecording();

    // Autotune control
    void setAutotuneEnabled(bool enabled) { autotuneEnabled = enabled; }
    bool isAutotuneEnabled() const { return autotuneEnabled; }
    AutotuneEngine* getAutotuneEngine() { return autotuneEngine.get(); }
    NeuralSpectralAutotuneEngine* getNeuralAutotuneEngine() { return neuralAutotuneEngine.get(); }
    void setUseNeuralAutotune(bool useNeural) { useNeuralAutotune = useNeural; }
    bool isUsingNeuralAutotune() const { return useNeuralAutotune; }
    
    // VST Parameter Automation
    AudioProcessorValueTreeState& getParameterTreeState() { return parameterTreeState; }
    void parameterChanged(const String& parameterID, float newValue) override;

private:
    static AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void pushSamplesToFifo(const float* samples, int numSamples);
    void processAudioBlock();

    PitchDetectorGUI* gui = nullptr;

    std::atomic<bool> wantsToRecord { false };
    AudioBuffer<float> recordedInput;
    AudioBuffer<float> recordedOutput;

    // FIFO buffer for audio processing
    static constexpr int fifoSize = 4096;
    float fifo[fifoSize];
    float processingBuffer[fifoSize];
    int fifoIndex = 0;
    std::atomic<bool> nextBlockReady { false };
    int audioBlockCount = 0;

    // Thread-safe result
    CriticalSection resultLock;
    DetectionResult latestResult;

    // Autotune processing
    std::unique_ptr<AutotuneEngine> autotuneEngine;
    std::unique_ptr<NeuralSpectralAutotuneEngine> neuralAutotuneEngine;
    std::vector<float> pitchBuffer; // Store pitch data for autotune processing
    bool autotuneEnabled = true;   // Enable autotune by default
    bool useNeuralAutotune = true; // Use our proprietary neural algorithm
    
    // VST Parameter State
    AudioProcessorValueTreeState parameterTreeState;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PitchDetectorProcessor)
};