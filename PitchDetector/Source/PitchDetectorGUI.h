#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include "PitchDetectionEngine.h"

using namespace juce;

//==============================================================================
class PitchDetectorGUI : public Component,
                         private Timer
{
public:
    PitchDetectorGUI();
    ~PitchDetectorGUI() override;
    
    // Component overrides
    void paint(Graphics& g) override;
    void resized() override;
    
    // Update from audio processing
    void updatePitchDisplay(float frequency, const NoteInfo& noteInfo);
    void updateAudioLevel(float level);
    void updateDebugInfo(const String& debugText);
    
    // Visual enhancements
    void drawTuningMeter(Graphics& g, const Rectangle<int>& area, const NoteInfo& noteInfo);
    void drawFrequencyHistory(Graphics& g, const Rectangle<int>& area);
    void drawStrobe(Graphics& g, const Rectangle<int>& area, float frequency);
    
    // Engine access
    PitchDetectionEngine& getEngine() { return engine; }
    
    // Controls access
    std::function<void()> onExportTelemetry;
    std::function<void()> onResetTelemetry;
    
private:
    void timerCallback() override;
    void initializeComponents();
    void updateTelemetryDisplay();
    void updateModeVisibility();
    
    // Core engine
    PitchDetectionEngine engine;
    
    // Display components
    Label noteNameLabel;
    Label frequencyLabel;
    Label centsLabel;
    Label audioLevelLabel;
    Label debugLabel;
    Label telemetryLabel;
    
    // Control components
    Slider noiseThresholdSlider;
    Label noiseThresholdLabel;
    Slider minFreqSlider;
    Label minFreqLabel;
    Slider maxFreqSlider;
    Label maxFreqLabel;
    Slider correlationThresholdSlider;
    Label correlationThresholdLabel;
    
    // Action buttons
    TextButton telemetryButton;
    TextButton resetTelemetryButton;
    
    // Collapsible panels
    TextButton debugToggleButton;
    TextButton telemetryToggleButton;
    bool showDebugPanel = false;
    bool showTelemetryPanel = true;
    
    // Mode toggle (Simple vs Advanced)
    TextButton modeToggleButton;
    bool simpleMode = true; // default to simple, decluttered UI
    
    // Current state
    float currentAudioLevel = 0.0f;
    String currentDebugInfo;
    
    // Visual state
    NoteInfo currentNoteInfo;
    float currentFrequency = 0.0f;
    std::vector<float> frequencyHistory;
    int maxHistorySize = 200;
    
    // Animation
    float strobePhase = 0.0f;
    Time lastUpdateTime;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PitchDetectorGUI)
};
