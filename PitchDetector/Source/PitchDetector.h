#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <vector>
#include <memory>
#include <array>
#include <map>

using namespace juce;

//==============================================================================
// SYSTEM CONSTANTS - NO MORE MAGIC NUMBERS
//==============================================================================

namespace PitchDetectorConstants {
    // Audio Processing
    static constexpr double DEFAULT_SAMPLE_RATE = 44100.0;
    static constexpr int DEFAULT_BUFFER_SIZE = 512;
    static constexpr int PROCESSING_FIFO_SIZE = 8192;
    static constexpr int GUI_FIFO_SIZE = 64;
    static constexpr int PITCH_DETECTION_WINDOW = 4096;
    
    // Pitch Detection Parameters  
    static constexpr float DEFAULT_NOISE_THRESHOLD = 0.005f;
    static constexpr float VOCAL_MIN_FREQUENCY = 80.0f;    // Lowest vocal fundamental
    static constexpr float VOCAL_MAX_FREQUENCY = 1200.0f;  // Highest useful vocal range
    static constexpr float DEFAULT_CORRELATION_THRESHOLD = 0.3f;
    static constexpr float STABILITY_THRESHOLD = 0.05f;    // 5% frequency deviation
    static constexpr int STABILITY_WINDOW = 3;             // Frames for stability
    
    // Autotune Processing
    static constexpr int MAX_DELAY_SAMPLES = 4096;
    static constexpr int AUTOTUNE_WINDOW_SIZE = 2048;
    static constexpr int AUTOTUNE_OVERLAP_SIZE = 1024;
    static constexpr float DEFAULT_CORRECTION_STRENGTH = 0.8f;
    static constexpr float DEFAULT_CORRECTION_SPEED = 0.5f;
    static constexpr float DEFAULT_MIX_AMOUNT = 1.0f;
    static constexpr float DEFAULT_REFERENCE_PITCH = 440.0f; // A4
    
    // GUI Layout
    static constexpr int MIN_WINDOW_WIDTH = 300;
    static constexpr int MIN_WINDOW_HEIGHT = 250;
    static constexpr int DEFAULT_WINDOW_WIDTH = 500;
    static constexpr int DEFAULT_WINDOW_HEIGHT = 400;
    static constexpr int CIRCULAR_TUNER_SIZE = 140;
    static constexpr float TUNER_RADIUS = 60.0f;
    static constexpr float TUNER_NEEDLE_LENGTH = 45.0f;
    static constexpr int GUI_UPDATE_RATE_HZ = 60;
    static constexpr int STANDALONE_TIMER_HZ = 30;
    
    // Visual Constants
    static const uint32 NEON_ACCENT_ARGB = 0xff00ffff;
    static const uint32 NEON_PURPLE_ARGB = 0xffaa00ff;  
    static const uint32 GLASS_BG_ARGB = 0x30ffffff;
    static const uint32 GLASS_BORDER_ARGB = 0x60ffffff;
    
    // Audio Analysis
    static constexpr float CENTS_PER_OCTAVE = 1200.0f;
    static constexpr int SEMITONES_PER_OCTAVE = 12;
    static constexpr float CENTS_DISPLAY_RANGE = 50.0f; // ±50 cents for needle
}

//==============================================================================
// CORE DATA STRUCTURES
//==============================================================================

struct NoteInfo
{
    String noteName;
    int octave;
    float centsDeviation;
    bool isValid;
    
    NoteInfo() : octave(0), centsDeviation(0.0f), isValid(false) {}
};

struct DetectionResult
{
    float frequency = 0.0f;
    float audioLevel = 0.0f;
    NoteInfo noteInfo;
    bool hasNewData = false;
};

struct TelemetryData
{
    Time sessionStart;
    int64 sessionDurationSeconds = 0;
    String platformInfo;
    String audioDeviceInfo;
    double sampleRateInfo = 0.0;
    int bufferSizeInfo = 0;
    
    int64 totalAudioSamples = 0;
    int totalProcessingCycles = 0;
    double avgProcessingTimeMs = 0.0;
    double maxProcessingTimeMs = 0.0;
    std::vector<double> recentProcessingTimes;
    
    int totalDetectionAttempts = 0;
    int successfulDetections = 0;
    float avgDetectionConfidence = 0.0f;
    float minDetectedFreq = 0.0f;
    float maxDetectedFreq = 0.0f;
    std::vector<float> recentFrequencies;
    std::vector<float> recentConfidences;
    
    std::map<String, int> noteDetections;
    std::map<int, int> frequencyBins;
    
    void reset()
    {
        sessionStart = Time::getCurrentTime();
        sessionDurationSeconds = 0;
        totalAudioSamples = 0;
        totalProcessingCycles = 0;
        avgProcessingTimeMs = 0.0;
        maxProcessingTimeMs = 0.0;
        totalDetectionAttempts = 0;
        successfulDetections = 0;
        avgDetectionConfidence = 0.0f;
        minDetectedFreq = 0.0f;
        maxDetectedFreq = 0.0f;
        noteDetections.clear();
        frequencyBins.clear();
        recentProcessingTimes.clear();
        recentFrequencies.clear();
        recentConfidences.clear();
    }
};

enum class ScaleType
{
    Chromatic = 0,
    Major,
    Minor,
    Pentatonic,
    Blues,
    Dorian,
    Custom
};

struct AutotuneSettings
{
    float correctionStrength = PitchDetectorConstants::DEFAULT_CORRECTION_STRENGTH;
    float correctionSpeed = PitchDetectorConstants::DEFAULT_CORRECTION_SPEED;
    ScaleType scaleType = ScaleType::Chromatic;
    int rootNote = 0;
    float referencePitch = PitchDetectorConstants::DEFAULT_REFERENCE_PITCH;
    
    bool formantCorrection = true;
    float vibratoPreservation = 0.7f;
    float humanization = 0.1f;
    float mixAmount = PitchDetectorConstants::DEFAULT_MIX_AMOUNT;
    
    bool autoKeyDetection = false;
    float glideTime = 0.1f;
    
    std::array<bool, 12> customScale = {true, false, true, false, true, true, false, true, false, true, false, true};
};

//==============================================================================
// PITCH DETECTION ENGINE
//==============================================================================

class PitchDetectionEngine
{
public:
    PitchDetectionEngine();
    ~PitchDetectionEngine() = default;
    
    float detectPitch(const float* buffer, int size, double sampleRate);
    NoteInfo frequencyToNote(float frequency);
    
    float detectPitchYin(const float* buffer, int size, double sampleRate);
    float detectPitchHPS(const float* buffer, int size, double sampleRate);
    float detectPitchCepstrum(const float* buffer, int size, double sampleRate);
    
    void setNoiseThreshold(float threshold) { noiseThreshold = threshold; }
    void setFrequencyRange(float minFreq, float maxFreq) { minFrequency = minFreq; maxFrequency = maxFreq; }
    void setCorrelationThreshold(float threshold) { correlationThresholdFactor = threshold; }
    
    float getNoiseThreshold() const { return noiseThreshold; }
    float getMinFrequency() const { return minFrequency; }
    float getMaxFrequency() const { return maxFrequency; }
    float getCorrelationThreshold() const { return correlationThresholdFactor; }
    
    void setVocalOptimization(bool enabled) { vocalOptimization = enabled; }
    void setFormantAwareness(float awareness) { formantAwareness = jlimit(0.0f, 1.0f, awareness); }
    void setHarmonicSupport(float support) { harmonicSupport = jlimit(0.0f, 1.0f, support); }
    bool isVocalOptimized() const { return vocalOptimization; }
    
    float detectPitchWithConfidence(const float* buffer, int size, double sampleRate, float& confidence);
    std::vector<float> getHarmonicContent(const float* buffer, int size, double sampleRate, float fundamental);
    
    void initializeTelemetry();
    TelemetryData& getTelemetry() { return telemetry; }
    const TelemetryData& getTelemetry() const { return telemetry; }
    String exportTelemetryJson() const;
    
private:
    float noiseThreshold = PitchDetectorConstants::DEFAULT_NOISE_THRESHOLD;
    float minFrequency = PitchDetectorConstants::VOCAL_MIN_FREQUENCY;
    float maxFrequency = PitchDetectorConstants::VOCAL_MAX_FREQUENCY;
    float correlationThresholdFactor = PitchDetectorConstants::DEFAULT_CORRELATION_THRESHOLD;
    
    bool vocalOptimization = true;
    float formantAwareness = 0.7f;
    float harmonicSupport = 0.5f;
    
    enum DetectionMethod { Autocorrelation, YIN, HPS, Cepstrum, Hybrid };
    DetectionMethod currentMethod = Hybrid;
    
    std::vector<float> recentDetections;
    float stabilityThreshold = PitchDetectorConstants::STABILITY_THRESHOLD;
    int stabilityWindow = PitchDetectorConstants::STABILITY_WINDOW;
    
    int detectionCount = 0;
    int failedDetections = 0;
    
    TelemetryData telemetry;
    
    float autocorrelationPitchDetection(const float* buffer, int size, double sampleRate);
    float applyStabilityFilter(float newFrequency);
    float interpolatePeak(const std::vector<float>& data, int peakIndex);
    void preprocess(const float* input, float* output, int size);
    
    float detectVocalPitch(const float* buffer, int size, double sampleRate);
    float analyzeFormantContent(const float* buffer, int size, double sampleRate);
    float weighByHarmonics(float frequency, const float* buffer, int size, double sampleRate);
    std::vector<float> findFormantPeaks(const float* buffer, int size, double sampleRate);
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PitchDetectionEngine)
};

//==============================================================================
// AUTOTUNE ENGINE
//==============================================================================

class AutotuneEngine
{
public:
    AutotuneEngine();
    ~AutotuneEngine() = default;
    
    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock);
    void processBlock(AudioBuffer<float>& buffer, const float* pitchData, int numSamples);
    void reset();
    
    void updateSettings(const AutotuneSettings& newSettings) { settings = newSettings; }
    const AutotuneSettings& getSettings() const { return settings; }
    
    void setCorrectionStrength(float strength) { settings.correctionStrength = jlimit(0.0f, 1.0f, strength); }
    void setCorrectionSpeed(float speed) { settings.correctionSpeed = jlimit(0.0f, 1.0f, speed); }
    void setScaleType(ScaleType scale) { settings.scaleType = scale; }
    void setRootNote(int root) { settings.rootNote = root % 12; }
    void setReferencePitch(float pitch) { settings.referencePitch = jlimit(400.0f, 480.0f, pitch); }
    void setMixAmount(float mix) { settings.mixAmount = jlimit(0.0f, 1.0f, mix); }

    enum class EngineMode { Auto, PSOLA, PhaseVocoder };
    void setEngineMode(EngineMode m) { engineMode = m; }
    EngineMode getEngineMode() const { return engineMode; }
    void setVoicingThreshold(float t) { voicingThreshold = jlimit(0.0f, 1.0f, t); }
    float getVoicingThreshold() const { return voicingThreshold; }
    
    void setCustomScale(const std::array<bool, 12>& scale) { settings.customScale = scale; }
    std::array<bool, 12> getActiveScale() const;
    
    float getCurrentTargetPitch() const { return currentTargetPitch; }
    float getCurrentCorrectionAmount() const { return currentCorrectionAmount; }
    bool isCorrectingPitch() const { return pitchCorrectionActive; }
    
    bool isVibratoDetected() const { return vibratoState.isVibratoActive; }
    float getVibratoStrength() const { return vibratoState.currentStrength; }
    
    void setLPCFormantMode(bool enabled) { lpcFormantMode = enabled; }
    bool isLPCFormantModeEnabled() const { return lpcFormantMode; }
    float getFormantScalingBeta() const { return formantScalingBeta; }
    
private:
    // Core processing
    float calculateTargetPitch(float detectedPitch);
    float applyCorrectionSmoothing(float targetPitch, float currentPitch);
    void processPitchCorrection(float* audioData, int numSamples, float targetPitch);
    
    // Scale utilities
    float snapToScale(float frequency) const;
    int frequencyToMidiNote(float frequency) const;
    float midiNoteToFrequency(int midiNote) const;
    bool isNoteInScale(int midiNote) const;
    int findNearestScaleNote(int midiNote) const;
    
    // Pitch shifting
    void initializePitchShifter();
    void processPSOLA(float* audioData, int numSamples, float pitchRatio);
    void processPhaseVocoder(float* audioData, int numSamples, float pitchRatio);
    
    // State
    AutotuneSettings settings;
    double sampleRate = PitchDetectorConstants::DEFAULT_SAMPLE_RATE;
    int blockSize = PitchDetectorConstants::DEFAULT_BUFFER_SIZE;

    EngineMode engineMode = EngineMode::Auto;
    float voicingThreshold = 0.6f;
    
    bool lpcFormantMode = false;
    float formantScalingBeta = 0.35f;
    static constexpr float maxFormantMovement = 0.25f;
    static constexpr int crossfadeLengthMs = 10;
    
    float currentTargetPitch = 0.0f;
    float previousTargetPitch = 0.0f;
    float currentCorrectionAmount = 0.0f;
    bool pitchCorrectionActive = false;
    
    float targetPitchSmoothingState = 0.0f;
    float correctionSmoothingState = 0.0f;
    
    static constexpr int maxDelayInSamples = PitchDetectorConstants::MAX_DELAY_SAMPLES;
    std::vector<float> delayBuffer;
    std::vector<float> windowBuffer;
    std::vector<float> overlapBuffer;
    int delayWriteIndex = 0;
    
    struct VibratoDetectionState
    {
        static constexpr float Amin = 6.0f;
        static constexpr float Pmin = 0.6f;
        static constexpr int Tvib = 6;
        static constexpr float kv = 0.6f;
        static constexpr float svib = 0.5f;
        
        static constexpr int bufferSize = 2048;
        std::vector<float> centsBuffer;
        std::vector<float> filteredBuffer;
        int writeIndex = 0;
        
        float highPassState = 0.0f;
        float bandpassLowState = 0.0f;
        float bandpassHighState = 0.0f;
        
        static constexpr int featureWindowSamples = 441;
        int sampleCounter = 0;
        float rmsAmplitude = 0.0f;
        float autocorrPeriodicity = 0.0f;
        
        enum class HMMState { GenericDeviation, NaturalVibrato };
        HMMState currentState = HMMState::GenericDeviation;
        HMMState previousState = HMMState::GenericDeviation;
        int stateFrameCount = 0;
        
        bool isVibratoActive = false;
        float currentStrength = 0.0f;
        
        VibratoDetectionState()
        {
            centsBuffer.resize(bufferSize, 0.0f);
            filteredBuffer.resize(bufferSize, 0.0f);
        }
    } vibratoState;
    
    int64 totalProcessedSamples = 0;
    int correctionEventsCount = 0;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutotuneEngine)
};

//==============================================================================
// GUI COMPONENTS
//==============================================================================

class CircularPitchTuner : public Component
{
public:
    CircularPitchTuner();
    void paint(Graphics& g) override;
    void resized() override;
    
    void updatePitch(float frequency, const NoteInfo& noteInfo);
    void setTargetNote(const String& noteName);
    
private:
    float currentCents = 0.0f;
    String currentNote = "--";
    String targetNote = "A";
    bool hasValidPitch = false;
    
    static constexpr float radius = PitchDetectorConstants::TUNER_RADIUS;
    static constexpr float needleLength = PitchDetectorConstants::TUNER_NEEDLE_LENGTH;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CircularPitchTuner)
};

class AutotuneControls : public Component
{
public:
    AutotuneControls();
    ~AutotuneControls() override;

    void paint(Graphics& g) override;
    void resized() override;
    void setProcessor(AudioProcessor* proc);

private:
    TextButton autotuneButton;
    Slider strengthSlider;
    Slider speedSlider; 
    Slider mixSlider;
    
    Label strengthLabel;
    Label speedLabel;
    Label mixLabel;
    Label titleLabel;
    
    AudioProcessor* processor = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutotuneControls)
};

class PitchDetectorGUI : public Component, private Timer
{
public:
    PitchDetectorGUI();
    ~PitchDetectorGUI() override;
    
    void setupStandaloneAudio();
    void shutdownStandaloneAudio();
    
    void paint(Graphics& g) override;
    void resized() override;
    
    static constexpr int MIN_WIDTH = PitchDetectorConstants::MIN_WINDOW_WIDTH;
    static constexpr int MIN_HEIGHT = PitchDetectorConstants::MIN_WINDOW_HEIGHT;
    static constexpr int DEFAULT_WIDTH = PitchDetectorConstants::DEFAULT_WINDOW_WIDTH;
    static constexpr int DEFAULT_HEIGHT = PitchDetectorConstants::DEFAULT_WINDOW_HEIGHT;
    
    void updatePitchDisplay(float frequency, const NoteInfo& noteInfo);
    void updateAudioLevel(float level);
    
    void updateDebugInfo(const String& debugText) { /* Not implemented */ }
    void setAudioDeviceInfo(const String& deviceName, double sampleRate, int bufferSize) { /* Not implemented */ }
    
    PitchDetectionEngine& getEngine() { return engine; }
    
    std::function<void()> onExportTelemetry;
    std::function<void()> onResetTelemetry;
    
    // Public for StandaloneApp access
    static constexpr int fifoSize = PitchDetectorConstants::GUI_FIFO_SIZE;
    float pitchFifo[fifoSize];
    float levelFifo[fifoSize];
    std::atomic<int> fifoWriteIndex { 0 };
    std::atomic<int> fifoReadIndex { 0 };
    
private:
    void timerCallback() override;
    void initializeComponents();
    void processAudioInput(const float* inputData, int numSamples);
    
    PitchDetectionEngine engine;
    
    double currentSampleRate = PitchDetectorConstants::DEFAULT_SAMPLE_RATE;
    int currentBufferSize = PitchDetectorConstants::DEFAULT_BUFFER_SIZE;
    
    CircularPitchTuner circularTuner;
    Label noteNameLabel;
    Label frequencyLabel;
    Label centsLabel;
    Label audioLevelLabel;
    
    TextButton exportButton;
    TextButton resetButton;
    
    const Colour NEON_ACCENT = Colour(PitchDetectorConstants::NEON_ACCENT_ARGB);
    const Colour NEON_PURPLE = Colour(PitchDetectorConstants::NEON_PURPLE_ARGB);
    const Colour GLASS_BG = Colour(PitchDetectorConstants::GLASS_BG_ARGB);
    const Colour GLASS_BORDER = Colour(PitchDetectorConstants::GLASS_BORDER_ARGB);
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PitchDetectorGUI)
};

//==============================================================================
// AUDIO PROCESSOR
//==============================================================================

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

    void startStopRecording();
    void saveRecording();

    void setAutotuneEnabled(bool enabled) { autotuneEnabled = enabled; }
    bool isAutotuneEnabled() const { return autotuneEnabled; }
    AutotuneEngine* getAutotuneEngine() { return autotuneEngine.get(); }
    
    AudioProcessorValueTreeState& getParameterTreeState() { return parameterTreeState; }
    void parameterChanged(const String& parameterID, float newValue) override;

    bool isUsingNeuralAutotune() const { return useNeuralAutotune; }

private:
    static AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void pushSamplesToFifo(const float* samples, int numSamples);
    void processAudioBlock();

    std::atomic<bool> wantsToRecord { false };
    AudioBuffer<float> recordedInput;
    AudioBuffer<float> recordedOutput;

    static constexpr int fifoSize = PitchDetectorConstants::PITCH_DETECTION_WINDOW;
    float fifo[fifoSize];
    float processingBuffer[fifoSize];
    int fifoIndex = 0;
    std::atomic<bool> nextBlockReady { false };
    int audioBlockCount = 0;

    CriticalSection resultLock;
    DetectionResult latestResult;

    std::unique_ptr<AutotuneEngine> autotuneEngine;
    std::vector<float> pitchBuffer;
    bool autotuneEnabled = true;
    
    AudioProcessorValueTreeState parameterTreeState;
    bool useNeuralAutotune = false;
    
public:
    PitchDetectorGUI* gui = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PitchDetectorProcessor)
};

//==============================================================================
// PLUGIN EDITOR
//==============================================================================

class PitchDetectorEditor : public AudioProcessorEditor
{
public:
    PitchDetectorEditor(PitchDetectorProcessor& p);
    ~PitchDetectorEditor() override;

    void paint (Graphics&) override;
    void resized() override;
    
private:
    PitchDetectorProcessor& audioProcessor;
    PitchDetectorGUI gui;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PitchDetectorEditor)
};

//==============================================================================
// STANDALONE APPLICATION
//==============================================================================

class StandalonePitchDetector : public AudioAppComponent,
                                private Timer
{
public:
    StandalonePitchDetector();
    ~StandalonePitchDetector() override;
    
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock(const AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;
    
    void paint(Graphics& g) override;
    void resized() override;
    
private:
    void timerCallback() override;
    void initializePermissions();
    void checkPermissionStatus();
    void updatePermissionUI();
    void requestMicrophonePermission();
    void setupAudioWithPermission();
    void exportTelemetryData();
    void resetTelemetryData();
    void showAudioSettings();
    
    enum PermissionState { Unknown, Granted, Denied, NotDetermined };
    PermissionState currentPermissionState = Unknown;
    
    static constexpr int fifoSize = PitchDetectorConstants::PROCESSING_FIFO_SIZE;
    float fifo[fifoSize];
    float processingBuffer[fifoSize];
    int fifoIndex = 0;
    bool nextBlockReady = false;
    
    PitchDetectorGUI gui;
    PitchDetectionEngine engine;
    
    Label permissionStatusLabel;
    TextButton permissionButton;
    TextButton audioSettingsButton;
    
    int audioBlockCount = 0;
    std::atomic<float> currentAudioLevel{0.0f};
    bool audioSetupFailed = true;
    bool disableAudio = false;
    
    void processAudioBlock();
    void pushSamplesToFifo(const float* samples, int numSamples);
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StandalonePitchDetector)
};