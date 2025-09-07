#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <vector>
#include <memory>
#include "PitchDetectionEngine.h"

using namespace juce;

//==============================================================================
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

//==============================================================================
struct AutotuneSettings
{
    // Core correction parameters
    float correctionStrength = 0.8f;        // 0.0-1.0: How much to correct pitch
    float correctionSpeed = 0.5f;           // 0.0-1.0: Speed of correction (0=slow/natural, 1=instant/robotic)
    ScaleType scaleType = ScaleType::Chromatic;
    int rootNote = 0;                       // 0=C, 1=C#, 2=D, etc.
    float referencePitch = 440.0f;          // A4 reference frequency
    
    // Advanced parameters
    bool formantCorrection = true;          // Preserve vocal formants during pitch shift
    float vibratoPreservation = 0.7f;      // 0.0-1.0: How much vibrato to preserve
    float humanization = 0.1f;             // 0.0-1.0: Random pitch variation for naturalness
    float mixAmount = 1.0f;                 // 0.0-1.0: Wet/dry mix
    
    // Musical intelligence
    bool autoKeyDetection = false;          // Automatically detect key from audio
    float glideTime = 0.1f;                // Seconds: Time to glide between corrected notes
    
    // Custom scale (12 booleans for each semitone)
    std::array<bool, 12> customScale = {true, false, true, false, true, true, false, true, false, true, false, true}; // Major scale default
};

//==============================================================================
class AutotuneEngine
{
public:
    AutotuneEngine();
    ~AutotuneEngine() = default;
    
    // Core processing
    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock);
    void processBlock(AudioBuffer<float>& buffer, const float* pitchData, int numSamples);
    void reset();
    
    // Parameter control
    void updateSettings(const AutotuneSettings& newSettings) { settings = newSettings; }
    const AutotuneSettings& getSettings() const { return settings; }
    
    // Individual parameter setters for real-time control
    void setCorrectionStrength(float strength) { settings.correctionStrength = jlimit(0.0f, 1.0f, strength); }
    void setCorrectionSpeed(float speed) { settings.correctionSpeed = jlimit(0.0f, 1.0f, speed); }
    void setScaleType(ScaleType scale) { settings.scaleType = scale; }
    void setRootNote(int root) { settings.rootNote = root % 12; }
    void setReferencePitch(float pitch) { settings.referencePitch = jlimit(400.0f, 480.0f, pitch); }
    void setMixAmount(float mix) { settings.mixAmount = jlimit(0.0f, 1.0f, mix); }

    // Engine and voicing selection
    enum class EngineMode { Auto, PSOLA, PhaseVocoder };
    void setEngineMode(EngineMode m) { engineMode = m; }
    EngineMode getEngineMode() const { return engineMode; }
    void setVoicingThreshold(float t) { voicingThreshold = jlimit(0.0f, 1.0f, t); }
    float getVoicingThreshold() const { return voicingThreshold; }
    
    // Scale management
    void setCustomScale(const std::array<bool, 12>& scale) { settings.customScale = scale; }
    std::array<bool, 12> getActiveScale() const;
    
    // Analysis
    float getCurrentTargetPitch() const { return currentTargetPitch; }
    float getCurrentCorrectionAmount() const { return currentCorrectionAmount; }
    bool isCorrectingPitch() const { return pitchCorrectionActive; }
    
    // Vibrato detection
    bool isVibratoDetected() const { return vibratoState.isVibratoActive; }
    float getVibratoStrength() const { return vibratoState.currentStrength; }
    
    // LPC formant preservation
    void setLPCFormantMode(bool enabled) { lpcFormantMode = enabled; }
    bool isLPCFormantModeEnabled() const { return lpcFormantMode; }
    float getFormantScalingBeta() const { return formantScalingBeta; }
    
private:
    // Core algorithms
    float calculateTargetPitch(float detectedPitch);
    float applyCorrectionSmoothing(float targetPitch, float currentPitch);
    void processPitchCorrection(float* audioData, int numSamples, float targetPitch);
    
    // Scale utilities
    float snapToScale(float frequency) const;
    int frequencyToMidiNote(float frequency) const;
    float midiNoteToFrequency(int midiNote) const;
    bool isNoteInScale(int midiNote) const;
    int findNearestScaleNote(int midiNote) const;
    
    // Pitch shifting algorithms
    void initializePitchShifter();
    void processPSOLA(float* audioData, int numSamples, float pitchRatio);
    void processPhaseVocoder(float* audioData, int numSamples, float pitchRatio);
    
    // LPC formant preservation algorithms
    void detectEpochsZFR(const float* signal, int length, std::vector<int>& epochs);
    void performLPCAnalysis(const float* signal, int windowStart, int windowSize, std::vector<float>& coeffs, float& gain);
    void applyBandwidthExpansion(std::vector<float>& coeffs, float rho = 0.98f);
    void inverseFilter(const float* signal, int length, const std::vector<float>& coeffs, float* residual);
    void mapFormants(const std::vector<float>& originalFormants, std::vector<float>& mappedFormants, float alpha, float beta);
    void reapplyVocalTract(float* residual, int length, const std::vector<float>& modifiedCoeffs, float* output);
    void crossfadeFrames(float* buffer1, const float* buffer2, int length, int crossfadeLength);
    
    // Enhanced LPC processing methods
    void processHybridPSOLAWithLPC(float* audioData, int numSamples, float pitchRatio);
    void extractFormantsFromLPC(const std::vector<float>& coeffs, std::vector<float>& formants);
    void reconstructLPCFromFormants(const std::vector<float>& formants, std::vector<float>& coeffs, float gain);
    void applyPSOLAToResidual(float* residual, int length, float pitchRatio, int epochPos);
    void processPSOLAFallback(float* audioData, int numSamples, float pitchRatio);
    
    // Smoothing and interpolation
    void updateSmoothingFilters();
    float applySmoothingFilter(float input, float& smoothingState, float smoothingTime);
    
    // Vibrato detection algorithms
    void processVibratoDetection(float detectedPitch);
    void updateVibratoFeatures(float centsValue);
    float computeAutocorrelationPeriodicity();
    float applyVibratoAdaptiveCorrection(float originalStrength) const;
    
    // Settings and state
    AutotuneSettings settings;
    double sampleRate = 44100.0;
    int blockSize = 512;

    EngineMode engineMode = EngineMode::Auto;
    float voicingThreshold = 0.6f; // confidence to pick PSOLA in Auto
    
    // LPC formant preservation state
    bool lpcFormantMode = false;
    float formantScalingBeta = 0.35f;  // β ∈ [0.2, 0.5] from spec
    static constexpr float maxFormantMovement = 0.25f;  // 25% per 200 cents
    static constexpr int crossfadeLengthMs = 10;  // 10ms crossfade
    
    // Processing state
    float currentTargetPitch = 0.0f;
    float previousTargetPitch = 0.0f;
    float currentCorrectionAmount = 0.0f;
    bool pitchCorrectionActive = false;
    
    // Smoothing filters
    float targetPitchSmoothingState = 0.0f;
    float correctionSmoothingState = 0.0f;
    
    // Pitch shifting buffers
    static constexpr int maxDelayInSamples = 4096;
    std::vector<float> delayBuffer;
    std::vector<float> windowBuffer;
    std::vector<float> overlapBuffer;
    int delayWriteIndex = 0;
    
    // Vibrato detection state
    struct VibratoDetectionState
    {
        // Production specification parameters
        static constexpr float Amin = 6.0f;      // cents RMS minimum
        static constexpr float Pmin = 0.6f;      // periodicity threshold
        static constexpr int Tvib = 6;           // frames (60ms) hysteresis
        static constexpr float kv = 0.6f;        // vibrato attenuation factor
        static constexpr float svib = 0.5f;      // speed reduction during vibrato
        
        // Circular buffers for real-time processing (efficient <40μs per 64-sample block)
        static constexpr int bufferSize = 2048;  // ~46ms at 44.1kHz
        std::vector<float> centsBuffer;          // cents trace circular buffer
        std::vector<float> filteredBuffer;       // filtered cents for vibrato analysis
        int writeIndex = 0;
        
        // Filter states
        float highPassState = 0.0f;              // high-pass filter state (1-2s time constant)
        float bandpassLowState = 0.0f;           // bandpass filter low state (3-9Hz)
        float bandpassHighState = 0.0f;          // bandpass filter high state
        
        // Feature computation (every 10ms)
        static constexpr int featureWindowSamples = 441; // 10ms at 44.1kHz
        int sampleCounter = 0;
        float rmsAmplitude = 0.0f;               // A: RMS amplitude
        float autocorrPeriodicity = 0.0f;        // P: autocorrelation periodicity
        
        // Two-state HMM
        enum class HMMState { GenericDeviation, NaturalVibrato };
        HMMState currentState = HMMState::GenericDeviation;
        HMMState previousState = HMMState::GenericDeviation;
        int stateFrameCount = 0;                 // frames in current state
        
        // Output state
        bool isVibratoActive = false;
        float currentStrength = 0.0f;           // vibrato strength for adaptive correction
        
        VibratoDetectionState()
        {
            centsBuffer.resize(bufferSize, 0.0f);
            filteredBuffer.resize(bufferSize, 0.0f);
        }
    } vibratoState;
    
    // PSOLA state
    struct PSOLAState
    {
        std::vector<float> grainBuffer;
        std::vector<int> pitchMarks;
        int lastPitchMark = 0;
        float grainPhase = 0.0f;
        int grainSize = 1024;

        // Streaming PSOLA state (cross-block continuity)
        std::vector<float> inBuffer;     // rolling input buffer
        int inReadPos = 0;               // consumed input samples
        std::vector<int> marks;          // absolute indices into inBuffer
        bool initialized = false;
        int baseInMark = 0;              // first input mark reference
        double baseOutPos = 0.0;         // corresponding output reference (samples)
        double outWritePos = 0.0;        // current write head in output OLA buffer
        double lastApproxPeriod = 200.0; // in samples
        std::vector<float> olaBuffer;    // persistent OLA for PSOLA
        int olaReadPos = 0;              // how many samples already emitted
    } psolaState;

    // Phase Vocoder state
    struct PhaseVocoderState
    {
        int fftOrder = 10;                // 2^10 = 1024
        int fftSize = 1 << fftOrder;      // 1024
        int hopSize = 256;                // 75% overlap

        juce::dsp::FFT fft { fftOrder };
        std::vector<float> window;        // Hann

        std::vector<float> analysisFrame; // time-domain windowed
        std::vector<std::complex<float>> analysisSpec;
        std::vector<float> prevPhase;     // radians
        std::vector<float> phaseAcc;      // radians
        std::vector<float> envMag;        // smoothed log-magnitude envelope (analysis)

        std::vector<std::complex<float>> synthSpec;
        std::vector<float> synthFrame;    // time-domain frame

        std::vector<float> olaBuffer;     // persistent OLA buffer (sum of windowed frames)
        std::vector<float> olaWindowSum;  // persistent buffer of window sums for normalization
        int olaWritePos = 0;              // write cursor within OLA buffer
        int olaProduced = 0;              // how many samples have been emitted from head
        bool initialized = false;         // first-frame guard

        void prepare()
        {
            window.resize(fftSize);
            analysisFrame.assign(fftSize, 0.0f);
            synthFrame.assign(fftSize, 0.0f);
            analysisSpec.assign(fftSize, {0.0f, 0.0f});
            synthSpec.assign(fftSize, {0.0f, 0.0f});
            prevPhase.assign(fftSize, 0.0f);
            phaseAcc.assign(fftSize, 0.0f);
            envMag.assign(fftSize/2, 0.0f);
            olaBuffer.assign(fftSize * 8, 0.0f);
            olaWindowSum.assign(fftSize * 8, 0.0f);

            for (int n = 0; n < fftSize; ++n)
                window[n] = 0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi * n / (fftSize - 1)));

            olaWritePos = 0;
            olaProduced = 0;
            initialized = false;
        }
    } pv;
    
    // LPC formant preservation state
    struct LPCFormantState
    {
        static constexpr int lpcOrder = 16;  // 16th order LPC at 48kHz
        static constexpr int windowSizeMs = 20;  // 20ms Hamming windows
        static constexpr int hopSizeMs = 5;      // 5ms hop
        static constexpr float stabilityRho = 0.98f;  // bandwidth expansion
        
        std::vector<float> hammingWindow;
        std::vector<float> lpcCoeffs;
        std::vector<float> residualBuffer;
        std::vector<float> vocalTractBuffer;
        std::vector<int> epochPositions;
        std::vector<float> currentFormants;
        std::vector<float> targetFormants;
        
        // Zero-frequency resonator state for epoch detection
        float zfrState1 = 0.0f;
        float zfrState2 = 0.0f;
        
        // Crossfade buffers
        std::vector<float> crossfadeBuffer;
        int crossfadeLength = 0;
        
        void prepare(double sampleRate)
        {
            int windowSize = static_cast<int>(sampleRate * windowSizeMs / 1000.0);
            crossfadeLength = static_cast<int>(sampleRate * crossfadeLengthMs / 1000.0);
            
            hammingWindow.resize(windowSize);
            for (int i = 0; i < windowSize; ++i)
                hammingWindow[i] = 0.54f - 0.46f * std::cos(2.0f * juce::MathConstants<float>::pi * i / (windowSize - 1));
            
            lpcCoeffs.resize(lpcOrder + 1);
            residualBuffer.resize(windowSize * 2);  // extra space for processing
            vocalTractBuffer.resize(windowSize * 2);
            epochPositions.reserve(100);
            currentFormants.resize(5);  // track up to 5 formants
            targetFormants.resize(5);
            crossfadeBuffer.resize(crossfadeLength * 2);
        }
    } lpcState;
    
    // Performance monitoring
    int64 totalProcessedSamples = 0;
    int correctionEventsCount = 0;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutotuneEngine)
};
