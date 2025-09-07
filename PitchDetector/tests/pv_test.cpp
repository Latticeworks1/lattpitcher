#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "../Source/AutotuneEngine.h"

using namespace juce;

static double measureFrequency(const std::vector<float>& x, double sampleRate)
{
    // Zero-crossing frequency estimate (positive-going crossings)
    std::vector<int> idx;
    idx.reserve(2048);
    for (int i = 1; i < (int)x.size(); ++i)
    {
        if (x[i-1] <= 0.0f && x[i] > 0.0f)
            idx.push_back(i);
    }
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
    const float inputFreq = 430.0f; // Not exactly a semitone to force shift toward 440

    // Generate input sine
    std::vector<float> input(totalSamples);
    for (int n = 0; n < totalSamples; ++n)
        input[n] = 0.2f * std::sin(2.0 * MathConstants<double>::pi * inputFreq * (double)n / sampleRate);

    // Prepare engine
    AutotuneEngine engine;
    engine.prepareToPlay(sampleRate, 1024);
    engine.setEngineMode(AutotuneEngine::EngineMode::PhaseVocoder);
    engine.setCorrectionStrength(1.0f);
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

    // Measure
    double fIn = measureFrequency(input, sampleRate);
    double fOut = measureFrequency(output, sampleRate);
    Logger::outputDebugString("Measured input Hz:  " + String(fIn, 2));
    Logger::outputDebugString("Measured output Hz: " + String(fOut, 2));

    // Expected target is closer to A4 = 440 than Ab4 = 415.3, so expect ≈440Hz
    double err = std::abs(fOut - 440.0);
    Logger::outputDebugString("Abs error to 440 Hz: " + String(err, 2));

    // Print success boolean (<= 5 Hz tolerance)
    bool ok = err <= 5.0;
    Logger::outputDebugString(String("TEST_OK=") + (ok ? "1" : "0"));
    return ok ? 0 : 1;
}
