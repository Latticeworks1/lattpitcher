#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_dsp/juce_dsp.h>

using namespace juce;

class SpectrumAnalyzer : public Component, public Timer {
public:
    SpectrumAnalyzer();
    ~SpectrumAnalyzer() override;
    
    void paint(Graphics& g) override;
    void resized() override;
    void timerCallback() override;
    void processAudioData(const AudioBuffer<float>& buffer);
    
private:
    static constexpr int fftOrder = 11;
    static constexpr int fftSize = 1 << fftOrder;
    
    dsp::FFT forwardFFT;
    dsp::WindowingFunction<float> window;
    
    float fifo[fftSize];
    float fftData[2 * fftSize];
    int fifoIndex = 0;
    bool nextFFTBlockReady = false;
    float scopeData[fftSize / 2];
    
    void pushNextSampleIntoFifo(float sample) noexcept;
    void drawNextFrameOfSpectrum();
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumAnalyzer)
};