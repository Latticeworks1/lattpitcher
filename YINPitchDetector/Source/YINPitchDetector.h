#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <vector>
#include <cmath>

//==============================================================================
/**
 * YIN Pitch Detection Algorithm Implementation
 * 
 * Based on the mathematical formulation:
 * E(i) = Σ(x[n]² + x[n+i]²) - Signal energy function
 * H(i) = Σ(x[n] * x[n+i]) - Autocorrelation function  
 * temp1(i) = E(i) - 2H(i) - Pitch tracking metric
 * 
 * L_min = argmin(temp1(i)) - Optimal lag
 * f0 = Fs / L_min - Fundamental frequency
 */
class YINPitchDetector
{
public:
    struct PitchResult
    {
        float frequency = 0.0f;
        float confidence = 0.0f;
        bool isPitched = false;
        int periodInSamples = 0;
    };

    //==============================================================================
    YINPitchDetector();
    ~YINPitchDetector() = default;

    void setSampleRate(double newSampleRate);
    void setWindowSize(int newWindowSize);
    void setThresholds(float energyThreshold, float minLevel);

    PitchResult detectPitch(const float* audioData, int numSamples);

private:
    //==============================================================================
    // YIN Algorithm implementation
    
    /** Signal energy function: E(i) = Σ(x[n]² + x[n+i]²) */
    float calculateSignalEnergy(const float* signal, int windowSize, int lag);
    
    /** Autocorrelation function: H(i) = Σ(x[n] * x[n+i]) */
    float calculateAutocorrelation(const float* signal, int windowSize, int lag);
    
    /** Pitch tracking metric: temp1(i) = E(i) - 2H(i) */
    float calculatePitchMetric(const float* signal, int windowSize, int lag);
    
    /** Find minimum lag that satisfies threshold conditions */
    int findOptimalLag(const float* signal, int windowSize);
    
    /** Apply threshold checks for energy, boundary, and minimum level */
    bool passesThresholdChecks(float temp1Value, float energyValue, int lag, int windowSize);

    //==============================================================================
    // Parameters
    double sampleRate = 44100.0;
    int windowSize = 2048;
    
    // Threshold parameters (ε in the algorithm)
    float energyThreshold = 0.1f;   // Prevents spurious minima
    float minLevel = 0.01f;         // Minimum energy level
    
    // Working buffers
    std::vector<float> workBuffer;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (YINPitchDetector)
};