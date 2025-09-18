#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>
#include "CircularBuffer.h"
#include "YINPitchDetector.h"

//==============================================================================
class MainComponent : public juce::AudioAppComponent,
                      public juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    //==============================================================================
    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;

    //==============================================================================
    void paint (juce::Graphics& g) override;
    void resized() override;

    //==============================================================================
    void timerCallback() override;

private:
    //==============================================================================
    // Audio processing
    YINPitchDetector pitchDetector;
    CircularBuffer audioBuffer;
    std::vector<float> analysisBuffer;
    
    // Current pitch detection results
    YINPitchDetector::PitchResult currentPitch;
    juce::CriticalSection pitchLock;
    
    // GUI state
    float displayedFrequency = 0.0f;
    float displayedConfidence = 0.0f;
    bool isPitched = false;
    
    // Animated pitch visualization
    std::vector<float> pitchHistory;
    std::vector<float> confidenceHistory;
    std::vector<float> targetPitches;
    int historySize = 200;
    float animationPhase = 0.0f;
    
    // Visual effects
    float glowIntensity = 0.0f;
    float particlePhase = 0.0f;
    
    // Analysis parameters
    int analysisWindowSize = 2048;
    double currentSampleRate = 44100.0;
    
    // Musical scale for autotune-like visualization
    std::vector<float> chromaticScale = {261.63f, 277.18f, 293.66f, 311.13f, 329.63f, 349.23f, 369.99f, 392.00f, 415.30f, 440.00f, 466.16f, 493.88f}; // C4-B4
    
    void updatePitchHistory(float frequency, float confidence);
    void drawAnimatedPitchCurve(juce::Graphics& g, juce::Rectangle<int> area);
    void drawAutotuneLikeVisualization(juce::Graphics& g, juce::Rectangle<int> area);
    float findNearestNote(float frequency);
    juce::Colour getFrequencyColor(float frequency, float confidence);
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};