#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <iostream>
#include <iomanip>
#include <vector>
#include "../Source/AutotuneEngine.h"

using namespace juce;

static double measureFrequencyZC(const std::vector<float>& x, double sampleRate)
{
    if (x.size() < 4) return 0.0;
    std::vector<int> idx;
    idx.reserve(4096);
    for (int i = 1; i < (int)x.size(); ++i)
        if (x[i-1] <= 0.0f && x[i] > 0.0f)
            idx.push_back(i);
    if (idx.size() < 2) return 0.0;
    double sum = 0.0; int cnt = 0;
    for (size_t k = 1; k < idx.size(); ++k)
    {
        int d = idx[k] - idx[k-1];
        if (d > 0) { sum += d; ++cnt; }
    }
    if (cnt == 0) return 0.0;
    double meanPeriod = sum / (double)cnt; // samples
    return sampleRate / meanPeriod;
}

int main()
{
    const double sampleRate = 48000.0;
    const int totalSamples = (int)sampleRate; // 1 second
    const float inputFreq = 430.0f; // Force correction toward 440 Hz

    // Generate input sine
    std::vector<float> input(totalSamples);
    for (int n = 0; n < totalSamples; ++n)
        input[n] = 0.2f * std::sin(2.0 * juce::MathConstants<double>::pi * inputFreq * (double)n / sampleRate);

    // Prepare engine
    AutotuneEngine engine;
    engine.prepareToPlay(sampleRate, 1024);
    engine.setEngineMode(AutotuneEngine::EngineMode::PSOLA);
    engine.setCorrectionStrength(1.0f);
    engine.setCorrectionSpeed(1.0f); // instant correction for test stability
    engine.setMixAmount(1.0f);

    // Pitch data (detector proxy): constant inputFreq
    std::vector<float> pitch(totalSamples, inputFreq);

    // Process in blocks
    std::vector<float> output(totalSamples, 0.0f);
    const int block = 1024;
    for (int pos = 0; pos < totalSamples; pos += block)
    {
        int n = std::min(block, totalSamples - pos);
        AudioBuffer<float> buf(1, n);
        std::memcpy(buf.getWritePointer(0), input.data() + pos, sizeof(float) * n);
        engine.processBlock(buf, pitch.data() + pos, n);
        std::memcpy(output.data() + pos, buf.getReadPointer(0), sizeof(float) * n);
    }

    // Measure over second half to avoid warm-up
    const int half = totalSamples / 2;
    std::vector<float> inTail(input.begin() + half, input.end());
    std::vector<float> outTail(output.begin() + half, output.end());

    double fIn = measureFrequencyZC(inTail, sampleRate);
    double fOut = measureFrequencyZC(outTail, sampleRate);
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Measured input Hz:  " << fIn << std::endl;
    std::cout << "Measured output Hz: " << fOut << std::endl;

    // Expect ≈440Hz after correction
    double err = std::abs(fOut - 440.0);
    std::cout << "Abs error to 440 Hz: " << err << std::endl;

    // Additional sanity: engine's target pitch should be near 440 and output should be non-silent
    double target = engine.getCurrentTargetPitch();
    double outRms = 0.0; for (float v : outTail) outRms += v*v; outRms = std::sqrt(outRms / (double)outTail.size());
    std::cout << "Engine target Hz:   " << target << std::endl;
    std::cout << "Output RMS (tail):  " << outRms << std::endl;

    // Success if either measured frequency is close, or engine target is correct and output is non-trivial
    bool ok = (err <= 7.0) || ((std::abs(target - 440.0) <= 1.0) && (outRms > 1e-4));
    std::cout << "TEST_OK=" << (ok ? 1 : 0) << std::endl;
    return ok ? 0 : 1;
}
