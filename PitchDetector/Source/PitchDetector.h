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
#include <complex>

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
    
    // YIN Algorithm Parameters - Optimized for vocal detection
    static constexpr int YIN_FRAME_SIZE = 4096;            // N = 4096 samples (better frequency resolution)
    static constexpr int YIN_HOP_SIZE = 256;               // H = 256 samples
    static constexpr double YIN_REFERENCE_FREQ = 440.0;    // f_ref = 440 Hz (A4)
    static constexpr float YIN_EPSILON = 1e-12f;           // Numerical safety for divisions
    static constexpr float YIN_VOICING_THRESHOLD = 0.35f;  // Lower threshold for better sensitivity
    static constexpr float YIN_MIN_FREQ = 80.0f;           // Vocal range minimum
    static constexpr float YIN_MAX_FREQ = 800.0f;          // Vocal range maximum (fundamental)
    
    // Causal Smoothing Parameters
    static constexpr float LAMBDA_MIN = 0.2f;              // Minimum smoothing coefficient
    static constexpr float LAMBDA_MAX = 0.9f;              // Maximum smoothing coefficient
    
    // MIDI Emission Parameters
    static constexpr float MIDI_GATE_ON_THRESHOLD = 0.6f;  // θ_on for note activation
    static constexpr float MIDI_GATE_OFF_THRESHOLD = 0.4f; // θ_off for note deactivation
    static constexpr int MIDI_MIN_VELOCITY = 1;            // Minimum MIDI velocity
    static constexpr int MIDI_MAX_VELOCITY = 127;          // Maximum MIDI velocity
    
    // Phase Vocoder Parameters
    static constexpr int PV_FFT_SIZE = 2048;               // STFT frame size
    static constexpr int PV_HOP_SIZE = 256;                // Analysis hop size
    static constexpr int PV_OVERLAP_SIZE = 1792;           // Overlap size (N - H)
    static constexpr float PV_ALGORITHMIC_LATENCY_MS = 37.5f; // (N-H)/Fs at 48kHz
    static constexpr int LAGRANGE_INTERPOLATOR_TAPS = 8;   // 8-tap interpolation
    
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

struct YinResult
{
    float frequency = 0.0f;         // f_in[k] - instantaneous fundamental frequency
    float voicingStrength = 0.0f;   // v[k] - voicing strength (1 - C_k(τ0))
    float centsInput = 0.0f;        // c_in[k] - input in cents relative to reference
    float centsTarget = 0.0f;       // c_tgt[k] - target after chromatic snapping
    float centsDeviation = 0.0f;    // Δc[k] - deviation before smoothing
    float centsSmoothed = 0.0f;     // Δĉ[k] - smoothed deviation
    float targetFrequency = 0.0f;   // f̂_tgt[k] - smoothed target frequency
    float pitchRatio = 1.0f;        // r[k] - pitch shift ratio
    bool isVoiced = false;          // Whether signal is considered voiced
    int frameIndex = 0;             // k - frame index for debugging
};

struct NoteHistoryEntry
{
    String noteName;                // Note name (C4, D#5, etc.)
    float frequency = 0.0f;         // Detected frequency
    float centsDeviation = 0.0f;    // Cents from perfect pitch
    float voicingStrength = 0.0f;   // Detection confidence
    Time timestamp;                 // When the note was detected
    float duration = 0.0f;          // How long the note lasted (in seconds)
    bool isActive = false;          // Whether note is currently playing
    
    NoteHistoryEntry() : timestamp(Time::getCurrentTime()) {}
    NoteHistoryEntry(const String& note, float freq, float cents, float voicing)
        : noteName(note), frequency(freq), centsDeviation(cents), 
          voicingStrength(voicing), timestamp(Time::getCurrentTime()) {}
};

struct PianoRollNote
{
    int midiNote = 60;              // MIDI note number (C4 = 60)
    float startTime = 0.0f;         // Start time in seconds
    float duration = 0.0f;          // Duration in seconds  
    float velocity = 1.0f;          // Note velocity (0.0-1.0)
    float pitchAccuracy = 0.0f;     // Cents deviation from perfect
    bool isActive = false;          // Currently playing
    
    PianoRollNote() = default;
    PianoRollNote(int note, float start, float vel = 1.0f, float accuracy = 0.0f)
        : midiNote(note), startTime(start), velocity(vel), pitchAccuracy(accuracy), isActive(true) {}
};

struct SpectrogramData
{
    std::vector<float> magnitudes;  // FFT magnitude data
    Time timestamp;                 // When this frame was captured
    float maxMagnitude = 0.0f;      // Peak magnitude for scaling
    double sampleRate = PitchDetectorConstants::DEFAULT_SAMPLE_RATE; // Actual sample rate when recorded
    
    SpectrogramData() : timestamp(Time::getCurrentTime()) {}
    SpectrogramData(const std::vector<float>& mags, float maxMag, double sr = PitchDetectorConstants::DEFAULT_SAMPLE_RATE)
        : magnitudes(mags), timestamp(Time::getCurrentTime()), maxMagnitude(maxMag), sampleRate(sr) {}
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
    
    // Advanced YIN with complete analysis pipeline
    YinResult detectPitchYinAdvanced(const float* buffer, int size, double sampleRate, int frameIndex);
    float quadraticInterpolation(float yMinus1, float y0, float yPlus1, int peakIndex);
    void applyCausalSmoothing(YinResult& result);
    void updateMidiState(const YinResult& result);
    
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
    
    // YIN algorithm state
    float previousSmoothedDeviation = 0.0f;  // Δĉ[k-1] for causal smoothing
    bool previousNoteState = false;          // Previous MIDI note state
    int currentMidiNote = -1;                // Current MIDI note number (-1 = no note)
    
    // Fixed-size YIN buffers - no dynamic allocation
    static constexpr int MAX_YIN_LAG = static_cast<int>(PitchDetectorConstants::DEFAULT_SAMPLE_RATE / PitchDetectorConstants::YIN_MIN_FREQ) + 1;
    std::array<float, MAX_YIN_LAG> yinDifferenceFunction;   // d_k(τ) buffer
    std::array<float, MAX_YIN_LAG> yinCumulativeMean;       // C_k(τ) buffer
    std::array<float, 4096> hannWindow;  // Precomputed Hann window (matches YIN_FRAME_SIZE)
    
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
    
    // Phase vocoder utilities
    void initializePhaseVocoder();
    float principalArgument(float phase);  // princarg() phase unwrapping
    void lagrangeInterpolate8(const float* input, float* output, int outputLength, const float* timeMap);
    
    // State
    AutotuneSettings settings;
    double sampleRate = PitchDetectorConstants::DEFAULT_SAMPLE_RATE;
    int blockSize = PitchDetectorConstants::DEFAULT_BUFFER_SIZE;

    EngineMode engineMode = EngineMode::Auto;
    float voicingThreshold = 0.6f;
    
    // Phase Vocoder STFT state
    std::unique_ptr<juce::dsp::FFT> analysisFFT;
    std::unique_ptr<juce::dsp::FFT> synthesisFFT;
    std::vector<std::complex<float>> analysisFrame;    // X_k[m]
    std::vector<std::complex<float>> synthesisFrame;   // Y_k[m]
    std::vector<float> analysisWindow;                 // Precomputed Hann window
    std::vector<float> synthesisWindow;                // Synthesis window
    std::vector<float> previousPhase;                  // ∠X_{k-1}[m] for phase unwrapping
    std::vector<float> synthesisPhase;                 // ∠Y_k[m] accumulated phase
    std::vector<float> instantaneousFreq;              // ω_k[m] per bin
    std::vector<float> overlapAddBuffer;               // Overlap-add synthesis buffer
    std::vector<float> temporaryBuffer;                // For resampling intermediate signal z[n]
    int analysisFrameCounter = 0;                      // k - current frame index
    
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
        
        VibratoDetectionState() : centsBuffer(bufferSize, 0.0f), filteredBuffer(bufferSize, 0.0f)
        {
            // Pre-allocate fixed-size buffers - no resize() calls
        }
    } vibratoState;
    
    int64 totalProcessedSamples = 0;
    int correctionEventsCount = 0;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutotuneEngine)
};

//==============================================================================
// GUI COMPONENTS
//==============================================================================

class AutoTunePitchDisplay : public Component
{
public:
    AutoTunePitchDisplay();
    void paint(Graphics& g) override;
    void resized() override;
    
    void updatePitch(float frequency, const NoteInfo& noteInfo);
    void setTargetNote(const String& noteName);
    void setCorrectionStrength(float strength) { correctionStrength = strength; }
    void setScaleType(int scale) { currentScale = scale; }
    
private:
    float currentCents = 0.0f;
    float targetCents = 0.0f;
    float correctionStrength = 0.8f;
    String currentNote = "--";
    String targetNote = "A";
    int currentScale = 0; // 0=Chromatic, 1=Major, 2=Minor, etc.
    bool hasValidPitch = false;
    bool isCorrectingPitch = false;
    
    // Auto-Tune style pitch correction display
    void drawPitchWheel(Graphics& g, Rectangle<float> area);
    void drawScaleGrid(Graphics& g, Rectangle<float> area);
    void drawCorrectionIndicator(Graphics& g, Rectangle<float> area);
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutoTunePitchDisplay)
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

    using SliderAttachment = AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SliderAttachment> strengthAttachment;
    std::unique_ptr<SliderAttachment> speedAttachment;
    std::unique_ptr<SliderAttachment> mixAttachment;
    std::unique_ptr<ButtonAttachment> autotuneEnableAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutotuneControls)
};

class PianoRollDisplay : public Component, private Timer
{
public:
    PianoRollDisplay();
    ~PianoRollDisplay() override;
    
    void paint(Graphics& g) override;
    void resized() override;
    
    void addNote(int midiNote, float startTime, float velocity, float pitchAccuracy);
    void updateCurrentNote(int midiNote, float accuracy);
    void endCurrentNote();
    void clearHistory();
    float getCurrentTime() const { return currentTime; }

    // Recording API
    void startRecording() { isRecording = true; recordedNotes.clear(); recordingStartTime = currentTime; }
    void stopRecording() { isRecording = false; }
    bool isRecordingActive() const { return isRecording; }
    const std::vector<PianoRollNote>& getRecordedNotes() const { return recordedNotes; }
    bool exportRecordingToCSV(const File& file) const;
    
    void setTimeRange(float seconds) { timeRangeSeconds = seconds; }
    void setNoteRange(int minNote, int maxNote) { minMidiNote = minNote; maxMidiNote = maxNote; }
    
private:
    void timerCallback() override;
    void drawPianoKeys(Graphics& g, Rectangle<int> keyArea);
    void drawPianoRoll(Graphics& g, Rectangle<int> rollArea);
    void drawGridLines(Graphics& g, Rectangle<int> area);
    Colour getNoteColour(float pitchAccuracy, float velocity);
    int frequencyToMidiNote(float frequency);
    String getMidiNoteName(int midiNote) const;
    bool isBlackKey(int midiNote);
    
    std::vector<PianoRollNote> pianoRollNotes;
    float timeRangeSeconds = 10.0f;     // Time range to display
    int minMidiNote = 48;               // C3
    int maxMidiNote = 84;               // C6
    float currentTime = 0.0f;           // Current playback position
    bool isRecording = false;           // Recording flag
    float recordingStartTime = 0.0f;    // Recording start offset
    std::vector<PianoRollNote> recordedNotes; // Persistent recording store
    
    static constexpr int PIANO_KEY_WIDTH = 60;
    static constexpr int NOTE_HEIGHT = 12;
    static constexpr float PIXELS_PER_SECOND = 60.0f;
    
    Time sessionStartTime;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRollDisplay)
};

// Small strip showing recent cents deviation over time
class PitchTrackingStrip : public Component
{
public:
    PitchTrackingStrip() { setInterceptsMouseClicks(false, false); }
    ~PitchTrackingStrip() override = default;

    void paint(Graphics& g) override;
    void resized() override {}

    void addCents(float cents);
    void clear() { sampleCount = 0; writeIndex = 0; }
    void setRange(float centsRange) { range = jmax(10.0f, centsRange); }

private:
    // Fixed-size circular buffer - NO dynamic allocation
    static constexpr int MAX_HISTORY_SAMPLES = 512;
    std::array<float, MAX_HISTORY_SAMPLES> historyBuffer;
    std::atomic<int> writeIndex{0};
    std::atomic<int> sampleCount{0};
    float range = 50.0f;       // +/- cents range for scaling

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PitchTrackingStrip)
};

class SpectrogramDisplay : public Component, private Timer
{
public:
    SpectrogramDisplay();
    ~SpectrogramDisplay() override;
    
    void paint(Graphics& g) override;
    void resized() override;
    
    void addSpectrogramData(const float* audioBuffer, int bufferSize, double sampleRate);
    void clearHistory();
    
    void setFrequencyRange(float minFreq, float maxFreq) { minFrequency = minFreq; maxFrequency = maxFreq; }
    void setTimeRange(float seconds) { timeRangeSeconds = seconds; }
    
private:
    void timerCallback() override;
    void performFFT(const float* audioBuffer, int bufferSize);
    Colour getSpectrogramColour(float magnitude, float maxMag);
    float frequencyToBin(float frequency, double sampleRate, int fftSize);
    
    std::vector<SpectrogramData> spectrogramHistory;
    std::unique_ptr<juce::dsp::FFT> spectrogramFFT;
    std::vector<float> fftBuffer;
    std::vector<float> windowBuffer;
    
    float minFrequency = 80.0f;         // Minimum frequency to display
    float maxFrequency = 2000.0f;       // Maximum frequency to display
    float timeRangeSeconds = 5.0f;      // Time range for waterfall display
    int maxHistoryFrames = 200;         // Maximum spectrogram frames to keep
    
    static constexpr int FFT_SIZE = 1024;
    static constexpr int WATERFALL_HEIGHT = 3;  // Height of each waterfall line
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrogramDisplay)
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
    
    void updateDebugInfo([[maybe_unused]] const String& debugText) { /* Not implemented */ }
    void setAudioDeviceInfo([[maybe_unused]] const String& deviceName, [[maybe_unused]] double sampleRate, [[maybe_unused]] int bufferSize) { /* Not implemented */ }
    
    PitchDetectionEngine& getEngine() { return engine; }
    
    std::function<void()> onExportTelemetry;
    std::function<void()> onResetTelemetry;
    
    // Public for StandaloneApp access
    static constexpr int fifoSize = PitchDetectorConstants::GUI_FIFO_SIZE;
    float pitchFifo[fifoSize];
    float levelFifo[fifoSize];
    std::atomic<int> fifoWriteIndex{0};
    std::atomic<int> fifoReadIndex{0};
    
    SpectrogramDisplay spectrogram;
    
    // Piano roll access methods for recording
    void clearPianoRollHistory() { pianoRoll.clearHistory(); }
    void startPianoRollRecording() { pianoRoll.startRecording(); }
    void stopPianoRollRecording() { pianoRoll.stopRecording(); }
    const std::vector<PianoRollNote>& getPianoRollRecording() const { return pianoRoll.getRecordedNotes(); }
    bool exportPianoRollToCSV(const File& file) const { return pianoRoll.exportRecordingToCSV(file); }
    
private:
    void timerCallback() override;
    void initializeComponents();
    void processAudioInput(const float* inputData, int numSamples);
    
    PitchDetectionEngine engine;
    
    double currentSampleRate = PitchDetectorConstants::DEFAULT_SAMPLE_RATE;
    int currentBufferSize = PitchDetectorConstants::DEFAULT_BUFFER_SIZE;
    
    AutoTunePitchDisplay autoTuneDisplay;
    PitchTrackingStrip trackingStrip;
    PianoRollDisplay pianoRoll;
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
    std::atomic<int> fifoIndex{0};
    std::atomic<bool> nextBlockReady{false};
    std::atomic<int> audioBlockCount{0};

    CriticalSection resultLock;
    DetectionResult latestResult;
    
    PitchDetectionEngine pitchEngine;
    std::unique_ptr<AutotuneEngine> autotuneEngine;
    std::vector<float> pitchBuffer;
    bool autotuneEnabled = true;
    
    AudioProcessorValueTreeState parameterTreeState;
    
public:
    AudioProcessorValueTreeState& getValueTreeState() { return parameterTreeState; }
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
    AutotuneControls autotuneControls;
    TextButton recordButton;
    TextButton stopButton;
    TextButton exportCsvButton;

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
    std::atomic<int> fifoIndex{0};
    std::atomic<bool> nextBlockReady{false};
    
    PitchDetectorGUI gui;
    PitchDetectionEngine engine;
    
    Label permissionStatusLabel;
    TextButton permissionButton;
    TextButton audioSettingsButton;
    
    std::atomic<int> audioBlockCount{0};
    std::atomic<float> currentAudioLevel{0.0f};
    bool audioSetupFailed = true;
    bool disableAudio = false;
    
    void processAudioBlock();
    void pushSamplesToFifo(const float* samples, int numSamples);
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StandalonePitchDetector)
};
