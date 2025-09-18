#include "YINPitchDetector.h"

//==============================================================================
YINPitchDetector::YINPitchDetector()
{
    workBuffer.resize(windowSize * 2); // Ensure enough space for lag calculations
}

//==============================================================================
void YINPitchDetector::setSampleRate(double newSampleRate)
{
    sampleRate = newSampleRate;
}

void YINPitchDetector::setWindowSize(int newWindowSize)
{
    windowSize = newWindowSize;
    workBuffer.resize(windowSize * 2);
}

void YINPitchDetector::setThresholds(float energyThreshold_, float minLevel_)
{
    energyThreshold = energyThreshold_;
    minLevel = minLevel_;
}

//==============================================================================
float YINPitchDetector::calculateSignalEnergy(const float* signal, int windowSize, int lag)
{
    /*
     * Signal energy function for lag i:
     * E(i) = Σ(n=0 to N-1) [x[n]² + x[n+i]²]
     * 
     * This measures the combined energy of the signal at position n
     * and the signal shifted by lag i
     */
    
    float energy = 0.0f;
    
    for (int n = 0; n < windowSize; ++n)
    {
        if (n + lag < windowSize * 2) // Bounds check for shifted signal
        {
            float xn = signal[n];
            float xn_plus_i = signal[n + lag];
            energy += (xn * xn) + (xn_plus_i * xn_plus_i);
        }
    }
    
    return energy;
}

float YINPitchDetector::calculateAutocorrelation(const float* signal, int windowSize, int lag)
{
    /*
     * Autocorrelation function for lag i:
     * H(i) = Σ(n=0 to N-1) [x[n] * x[n+i]]
     * 
     * This measures how similar the signal is to itself
     * when shifted by lag i samples
     */
    
    float autocorr = 0.0f;
    
    for (int n = 0; n < windowSize; ++n)
    {
        if (n + lag < windowSize * 2) // Bounds check for shifted signal
        {
            autocorr += signal[n] * signal[n + lag];
        }
    }
    
    return autocorr;
}

float YINPitchDetector::calculatePitchMetric(const float* signal, int windowSize, int lag)
{
    /*
     * Pitch tracking metric:
     * temp1(i) = E(i) - 2H(i)
     * 
     * This combines energy and autocorrelation to find periodicity.
     * Lower values indicate better pitch candidates.
     */
    
    float energy = calculateSignalEnergy(signal, windowSize, lag);
    float autocorr = calculateAutocorrelation(signal, windowSize, lag);
    
    return energy - (2.0f * autocorr);
}

bool YINPitchDetector::passesThresholdChecks(float temp1Value, float energyValue, int lag, int windowSize)
{
    /*
     * Threshold checks from YIN algorithm:
     * 1. Energy threshold: temp1 > ε * E(L_min) → fail
     * 2. Boundary condition: L_min = 1 or L_min = N → fail  
     * 3. Minimum level: E(L_min) < min_level → fail
     */
    
    // 1. Energy threshold check
    if (temp1Value > energyThreshold * energyValue)
        return false;
    
    // 2. Boundary condition check
    if (lag <= 1 || lag >= windowSize)
        return false;
    
    // 3. Minimum level check
    if (energyValue < minLevel)
        return false;
    
    return true;
}

int YINPitchDetector::findOptimalLag(const float* signal, int windowSize)
{
    /*
     * Find the lag that minimizes temp1(i) = E(i) - 2H(i)
     * L_min = argmin(i ∈ {1,...,N}) {E(i) - 2H(i)}
     */
    
    float minTemp1 = std::numeric_limits<float>::max();
    int optimalLag = -1;
    
    // Search from lag 1 to windowSize-1 (avoiding boundary conditions)
    for (int lag = 1; lag < windowSize; ++lag)
    {
        float temp1 = calculatePitchMetric(signal, windowSize, lag);
        
        if (temp1 < minTemp1)
        {
            minTemp1 = temp1;
            optimalLag = lag;
        }
    }
    
    // Verify the optimal lag passes threshold checks
    if (optimalLag > 0)
    {
        float energy = calculateSignalEnergy(signal, windowSize, optimalLag);
        if (!passesThresholdChecks(minTemp1, energy, optimalLag, windowSize))
        {
            optimalLag = -1; // Failed threshold tests
        }
    }
    
    return optimalLag;
}

//==============================================================================
YINPitchDetector::PitchResult YINPitchDetector::detectPitch(const float* audioData, int numSamples)
{
    PitchResult result;
    
    // Ensure we have enough samples
    if (numSamples < windowSize * 2)
    {
        return result; // Return default (no pitch detected)
    }
    
    // Copy audio data to work buffer for processing
    std::copy(audioData, audioData + std::min(numSamples, (int)workBuffer.size()), workBuffer.begin());
    
    // Find the optimal lag using YIN algorithm
    int optimalLag = findOptimalLag(workBuffer.data(), windowSize);
    
    if (optimalLag > 0)
    {
        // Calculate pitch frequency: f0 = Fs / L_min
        result.frequency = static_cast<float>(sampleRate) / static_cast<float>(optimalLag);
        result.periodInSamples = optimalLag;
        result.isPitched = true;
        
        // Calculate confidence based on how well the lag explains periodicity
        float temp1Min = calculatePitchMetric(workBuffer.data(), windowSize, optimalLag);
        float energy = calculateSignalEnergy(workBuffer.data(), windowSize, optimalLag);
        
        // Confidence inversely related to temp1 value (lower temp1 = higher confidence)
        result.confidence = std::max(0.0f, 1.0f - (temp1Min / (energy + 1e-10f)));
    }
    
    return result;
}