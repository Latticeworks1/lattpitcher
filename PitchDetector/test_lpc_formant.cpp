#include "Source/AutotuneEngine.h"
#include <iostream>
#include <vector>
#include <cmath>

int main()
{
    std::cout << "=== LPC Formant Preservation Test ===" << std::endl;
    
    // Create AutotuneEngine instance
    AutotuneEngine engine;
    
    // Test LPC mode toggle
    std::cout << "Initial LPC mode: " << (engine.isLPCFormantModeEnabled() ? "ON" : "OFF") << std::endl;
    
    engine.setLPCFormantMode(true);
    std::cout << "After enabling: " << (engine.isLPCFormantModeEnabled() ? "ON" : "OFF") << std::endl;
    
    engine.setLPCFormantMode(false);
    std::cout << "After disabling: " << (engine.isLPCFormantModeEnabled() ? "ON" : "OFF") << std::endl;
    
    // Test formant scaling beta parameter
    std::cout << "Formant scaling beta: " << engine.getFormantScalingBeta() << std::endl;
    
    // Prepare engine for processing
    const double sampleRate = 48000.0;
    const int blockSize = 1024;
    engine.prepareToPlay(sampleRate, blockSize);
    
    std::cout << "Engine prepared for " << sampleRate << "Hz, block size " << blockSize << std::endl;
    
    // Test with synthetic vocal-like signal
    std::vector<float> testSignal(blockSize);
    std::vector<float> pitchData(blockSize);
    
    // Generate test signal: fundamental + harmonics (simulating vocal)
    const float f0 = 220.0f; // A3
    for (int i = 0; i < blockSize; ++i)
    {
        float t = i / sampleRate;
        // Fundamental + 2nd and 3rd harmonics (simplified vocal spectrum)
        testSignal[i] = 0.5f * std::sin(2.0f * M_PI * f0 * t) +
                       0.3f * std::sin(2.0f * M_PI * f0 * 2.0f * t) +
                       0.2f * std::sin(2.0f * M_PI * f0 * 3.0f * t);
        pitchData[i] = f0; // Constant pitch for test
    }
    
    // Create audio buffer (JUCE style)
    juce::AudioBuffer<float> buffer(1, blockSize);
    std::copy(testSignal.begin(), testSignal.end(), buffer.getWritePointer(0));
    
    // Test regular PSOLA
    std::cout << "Testing regular PSOLA..." << std::endl;
    engine.setLPCFormantMode(false);
    
    auto bufferCopy = buffer;
    engine.processBlock(bufferCopy, pitchData.data(), blockSize);
    
    // Calculate RMS of output
    float rms = 0.0f;
    for (int i = 0; i < blockSize; ++i)
    {
        float sample = bufferCopy.getReadPointer(0)[i];
        rms += sample * sample;
    }
    rms = std::sqrt(rms / blockSize);
    std::cout << "Regular PSOLA output RMS: " << rms << std::endl;
    
    // Test LPC formant preservation
    std::cout << "Testing LPC formant preservation..." << std::endl;
    engine.setLPCFormantMode(true);
    
    bufferCopy = buffer; // Reset buffer
    engine.processBlock(bufferCopy, pitchData.data(), blockSize);
    
    // Calculate RMS of LPC output
    rms = 0.0f;
    for (int i = 0; i < blockSize; ++i)
    {
        float sample = bufferCopy.getReadPointer(0)[i];
        rms += sample * sample;
    }
    rms = std::sqrt(rms / blockSize);
    std::cout << "LPC formant preservation output RMS: " << rms << std::endl;
    
    std::cout << "=== Test completed successfully! ===" << std::endl;
    
    return 0;
}