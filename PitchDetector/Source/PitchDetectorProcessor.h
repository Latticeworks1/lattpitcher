#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "PitchDetectionEngine.h"

using namespace juce;

//==============================================================================
class PitchDetectorProcessor : public AudioProcessor
{
public:
    PitchDetectorProcessor();
    ~PitchDetectorProcessor() override;
    
    // AudioProcessor interface
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(AudioBuffer<float>& buffer, MidiBuffer& midiMessages) override;
    
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    
    const String getName() const override { return "Pitch Detector"; }
    
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int index) override { ignoreUnused(index); }
    const String getProgramName(int index) override { ignoreUnused(index); return {}; }
    void changeProgramName(int index, const String& newName) override { ignoreUnused(index, newName); }
    
    void getStateInformation(MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;
    
    AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    
    // Access to detection results
    PitchDetectionEngine& getEngine() { return engine; }
    const PitchDetectionEngine& getEngine() const { return engine; }
    
    // Current detection results (thread-safe access)
    struct DetectionResult
    {
        float frequency = 0.0f;
        NoteInfo noteInfo;
        float audioLevel = 0.0f;
        bool hasNewData = false;
    };
    
    DetectionResult getLatestResult();
    
private:
    // Core detection engine
    PitchDetectionEngine engine;
    
    // Audio processing
    static constexpr int fifoSize = 8192;
    float fifo[fifoSize];
    float processingBuffer[fifoSize];
    int fifoIndex = 0;
    bool nextBlockReady = false;
    
    // Thread-safe result sharing
    DetectionResult latestResult;
    CriticalSection resultLock;
    
    // Debug counters
    int audioBlockCount = 0;
    
    void processAudioBlock();
    void pushSamplesToFifo(const float* samples, int numSamples);
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PitchDetectorProcessor)
};