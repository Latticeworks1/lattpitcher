#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "PitchDetectionEngine.h"

// Forward declarations
class PitchDetectorProcessor;

using namespace juce;

//==============================================================================
struct DetectionResult
{
    float frequency = 0.0f;
    float audioLevel = 0.0f;
    NoteInfo noteInfo;
    bool hasNewData = false;
};

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
    void setAudioDeviceInfo(const String& deviceName, double sampleRate, int bufferSize);
    
    // Visual enhancements
    void drawTuningMeter(Graphics& g, const Rectangle<int>& area, const NoteInfo& noteInfo);
    void drawFrequencyHistory(Graphics& g, const Rectangle<int>& area);
    void drawStrobe(Graphics& g, const Rectangle<int>& area, float frequency);
    
    // Engine access
    PitchDetectionEngine& getEngine() { return engine; }
    
    // Processor access (for autotune control)
    void setProcessor(AudioProcessor* proc);
    
    // Controls access
    std::function<void()> onExportTelemetry;
    std::function<void()> onResetTelemetry;
    
private:
    void timerCallback() override;
    void initializeComponents();
    void updateTelemetryDisplay();
    void updateModeVisibility();
    void updateStatusBar();
    void initializeAutotuneControls();
    void updateAutotuneDisplay();
    void initializeNeuralControls();
    void updateNeuralProcessingDisplay();
    
    // 🔥 FIRE GLASSMORPHISM HELPERS 🔥
    void drawGlassCard(Graphics& g, Rectangle<int> area, float cornerRadius = 16.0f);
    void drawNeonGlow(Graphics& g, Rectangle<int> area, Colour glowColor, float intensity = 1.0f);
    void drawLiquidBackground(Graphics& g, Rectangle<int> area);
    
    // Custom Glass Components
    class GlassKnob;
    class GlassButton;
    class NeonMeter;
    
    // Core engine
    PitchDetectionEngine engine;
    
    // VST Parameter attachments
    std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment> neuralIntensityAttachment;
    std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment> correctionStrengthAttachment;
    std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment> correctionSpeedAttachment;
    std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment> mixSliderAttachment;
    std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment> rootNoteAttachment;
    
    // Processor reference for autotune control
    AudioProcessor* processor = nullptr;
    
    // Display components
    Label noteNameLabel;
    Label frequencyLabel;
    Label centsLabel;
    Label audioLevelLabel;
    Label debugLabel;
    Label telemetryLabel;
    Label statusBarLabel;
    
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
    
    // Autotune controls
    TextButton autotuneToggleButton;
    Slider correctionStrengthSlider;
    Label correctionStrengthLabel;
    Slider correctionSpeedSlider; 
    Label correctionSpeedLabel;
    ComboBox scaleTypeBox;
    Label scaleTypeLabel;
    Slider rootNoteSlider;
    Label rootNoteLabel;
    Slider mixSlider;
    Label mixLabel;
    TextButton formantToggleButton;
    
    // 🚀 NEURAL SPECTRAL SYNTHESIS CONTROLS 🚀
    TextButton neuralModeToggleButton;
    Label neuralStatusLabel;
    Slider neuralIntensitySlider;
    Label neuralIntensityLabel;
    Slider cochlearSensitivitySlider;
    Label cochlearSensitivityLabel;
    Slider evolutionaryRateSlider;
    Label evolutionaryRateLabel;
    Slider temporalCoherenceSlider;
    Label temporalCoherenceLabel;
    
    // Neural processing indicators
    Label pitchCertaintyLabel;
    Label cochlearExcitationLabel;
    Label evolutionaryGenLabel;
    Label adaptationLevelLabel;
    
    // Collapsible panels
    TextButton debugToggleButton;
    TextButton telemetryToggleButton;
    TextButton autotuneTogglePanel;
    TextButton neuralTogglePanel;
    bool showDebugPanel = false;
    bool showTelemetryPanel = true;
    bool showAutotunePanel = true;
    bool showNeuralPanel = true;  // Show our revolutionary algorithm by default
    
    // Mode toggle (Simple vs Advanced)
    TextButton modeToggleButton;
    bool simpleMode = false; // Start with advanced mode to show autotune controls
    
    // 🔥 FIRE LIQUID GLASS UI SYSTEM 🔥
    static constexpr float UI_SCALE_FACTOR = 1.0f;
    static constexpr int GLASS_CARD_HEIGHT = static_cast<int>(60 * UI_SCALE_FACTOR);
    static constexpr int GLASS_KNOB_SIZE = static_cast<int>(50 * UI_SCALE_FACTOR);
    static constexpr int GLASS_BUTTON_HEIGHT = static_cast<int>(45 * UI_SCALE_FACTOR);
    static constexpr int GLASS_PADDING = static_cast<int>(12 * UI_SCALE_FACTOR);
    static constexpr int GLASS_RADIUS = static_cast<int>(16 * UI_SCALE_FACTOR);
    static constexpr int GLASS_BLUR = static_cast<int>(8 * UI_SCALE_FACTOR);
    
    // Liquid Glass Colors (iOS Style)
    static const Colour GLASS_BG;        // rgba(255,255,255,0.08) 
    static const Colour GLASS_BORDER;    // rgba(255,255,255,0.18)
    static const Colour NEON_ACCENT;     // #00D4FF (Neon cyan)
    static const Colour NEON_PURPLE;     // #8B5CF6 (Electric purple)
    static const Colour GLASS_TEXT;      // rgba(255,255,255,0.95)
    
    // Current state
    float currentAudioLevel = 0.0f;
    String currentDebugInfo;
    String currentDeviceName;
    double currentSampleRate = 0.0;
    int currentBufferSize = 0;
    
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
