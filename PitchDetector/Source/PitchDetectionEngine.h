#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <vector>
#include <map>

using namespace juce;

//==============================================================================
struct NoteInfo
{
    String noteName;
    int octave;
    float centsDeviation;
    bool isValid;
    
    NoteInfo() : octave(0), centsDeviation(0.0f), isValid(false) {}
};

//==============================================================================
struct TelemetryData
{
    // Session info
    Time sessionStart;
    int64 sessionDurationSeconds = 0;
    String platformInfo;
    String audioDeviceInfo;
    double sampleRateInfo = 0.0;
    int bufferSizeInfo = 0;
    
    // Performance metrics
    int64 totalAudioSamples = 0;
    int totalProcessingCycles = 0;
    double avgProcessingTimeMs = 0.0;
    double maxProcessingTimeMs = 0.0;
    std::vector<double> recentProcessingTimes;
    
    // Detection metrics
    int totalDetectionAttempts = 0;
    int successfulDetections = 0;
    float avgDetectionConfidence = 0.0f;
    float minDetectedFreq = 0.0f;
    float maxDetectedFreq = 0.0f;
    std::vector<float> recentFrequencies;
    std::vector<float> recentConfidences;
    
    // Pattern analysis
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

//==============================================================================
class PitchDetectionEngine
{
public:
    PitchDetectionEngine();
    ~PitchDetectionEngine() = default;
    
    // Core detection
    float detectPitch(const float* buffer, int size, double sampleRate);
    NoteInfo frequencyToNote(float frequency);
    
    // Advanced detection methods
    float detectPitchYin(const float* buffer, int size, double sampleRate);
    float detectPitchHPS(const float* buffer, int size, double sampleRate); // Harmonic Product Spectrum
    float detectPitchCepstrum(const float* buffer, int size, double sampleRate);
    
    // Parameters
    void setNoiseThreshold(float threshold) { noiseThreshold = threshold; }
    void setFrequencyRange(float minFreq, float maxFreq) { minFrequency = minFreq; maxFrequency = maxFreq; }
    void setCorrelationThreshold(float threshold) { correlationThresholdFactor = threshold; }
    
    float getNoiseThreshold() const { return noiseThreshold; }
    float getMinFrequency() const { return minFrequency; }
    float getMaxFrequency() const { return maxFrequency; }
    float getCorrelationThreshold() const { return correlationThresholdFactor; }
    
    // Telemetry
    void initializeTelemetry();
    TelemetryData& getTelemetry() { return telemetry; }
    const TelemetryData& getTelemetry() const { return telemetry; }
    String exportTelemetryJson() const;
    
private:
    // Detection parameters
    float noiseThreshold = 0.005f;
    float minFrequency = 60.0f;
    float maxFrequency = 1000.0f;
    float correlationThresholdFactor = 0.3f;
    
    // Algorithm selection
    enum DetectionMethod { Autocorrelation, YIN, HPS, Cepstrum, Hybrid };
    DetectionMethod currentMethod = Hybrid;
    
    // Stability and smoothing
    std::vector<float> recentDetections;
    float stabilityThreshold = 0.05f; // 5% frequency deviation threshold
    int stabilityWindow = 3; // Number of recent detections to consider
    
    // Statistics
    int detectionCount = 0;
    int failedDetections = 0;
    
    // Telemetry
    TelemetryData telemetry;
    
    // Internal methods
    float autocorrelationPitchDetection(const float* buffer, int size, double sampleRate);
    float applyStabilityFilter(float newFrequency);
    float interpolatePeak(const std::vector<float>& data, int peakIndex);
    void preprocess(const float* input, float* output, int size); // Apply windowing and normalization
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PitchDetectionEngine)
};