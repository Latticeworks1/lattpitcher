#include "SpectrumAnalyzer.h"

SpectrumAnalyzer::SpectrumAnalyzer()
    : forwardFFT(fftOrder), window(fftSize, dsp::WindowingFunction<float>::hann) {
    setOpaque(false);
    startTimer(60); // 60 FPS for smooth spectrum
}

SpectrumAnalyzer::~SpectrumAnalyzer() {
    stopTimer();
}

void SpectrumAnalyzer::paint(Graphics& g) {
    g.fillAll(Colour(0xff0a0a0a));
    
    // Draw spectrum background
    g.setColour(Colour(0xff1a1a1a));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 8.0f);
    
    // Draw frequency grid
    g.setColour(Colour(0xff333333));
    auto bounds = getLocalBounds().reduced(10);
    
    // Vertical frequency lines (1kHz, 2kHz, 5kHz, 10kHz, 20kHz)
    const float freqs[] = {1000, 2000, 5000, 10000, 20000};
    for (auto freq : freqs) {
        float x = bounds.getX() + bounds.getWidth() * jmap(std::log10(freq), std::log10(20.0f), std::log10(20000.0f), 0.0f, 1.0f);
        g.drawVerticalLine(static_cast<int>(x), bounds.getY(), bounds.getBottom());
    }
    
    // Draw spectrum data
    if (nextFFTBlockReady) {
        g.setColour(Colour(0xff00d4ff)); // Modern blue
        
        Path spectrumPath;
        auto width = bounds.getWidth();
        auto height = bounds.getHeight();
        
        for (int i = 1; i < fftSize / 2; ++i) {
            auto skewedProportionX = 1.0f - std::exp(std::log(1.0f - (float)i / (float)(fftSize / 2)) * 0.2f);
            auto fftX = jlimit(0.0f, 1.0f, skewedProportionX);
            auto level = jmap(jlimit(0.0f, 1.0f, jmap(20.0f * std::log10(scopeData[i]), -100.0f, 0.0f, 0.0f, 1.0f)), 0.0f, 1.0f, (float)height, 0.0f);
            
            if (i == 1) spectrumPath.startNewSubPath(bounds.getX() + fftX * width, bounds.getY() + level);
            else spectrumPath.lineTo(bounds.getX() + fftX * width, bounds.getY() + level);
        }
        
        g.strokePath(spectrumPath, PathStrokeType(2.0f));
        
        // Add glow effect
        g.setColour(Colour(0x3300d4ff));
        g.strokePath(spectrumPath, PathStrokeType(8.0f));
    }
    
    // Draw labels
    g.setColour(Colour(0xffaaaaaa));
    g.setFont(FontOptions{12.0f});
    g.drawText("SPECTRUM ANALYZER", bounds.getX(), bounds.getY() - 20, 200, 20, Justification::left);
    g.drawText("20Hz", bounds.getX(), bounds.getBottom() + 5, 50, 15, Justification::left);
    g.drawText("20kHz", bounds.getRight() - 50, bounds.getBottom() + 5, 50, 15, Justification::right);
}

void SpectrumAnalyzer::timerCallback() {
    if (nextFFTBlockReady) {
        drawNextFrameOfSpectrum();
        nextFFTBlockReady = false;
        repaint();
    }
}

void SpectrumAnalyzer::processAudioData(const AudioBuffer<float>& buffer) {
    if (buffer.getNumChannels() > 0) {
        auto* channelData = buffer.getReadPointer(0);
        
        for (auto i = 0; i < buffer.getNumSamples(); ++i)
            pushNextSampleIntoFifo(channelData[i]);
    }
}

void SpectrumAnalyzer::pushNextSampleIntoFifo(float sample) noexcept {
    if (fifoIndex == fftSize) {
        if (!nextFFTBlockReady) {
            zeromem(fftData, sizeof(fftData));
            memcpy(fftData, fifo, sizeof(fifo));
            nextFFTBlockReady = true;
        }
        fifoIndex = 0;
    }
    
    fifo[fifoIndex++] = sample;
}

void SpectrumAnalyzer::drawNextFrameOfSpectrum() {
    window.multiplyWithWindowingTable(fftData, fftSize);
    forwardFFT.performFrequencyOnlyForwardTransform(fftData);
    
    auto mindB = -100.0f;
    auto maxdB = 0.0f;
    
    for (int i = 0; i < fftSize / 2; ++i) {
        auto skewedProportionX = 1.0f - std::exp(std::log(1.0f - (float)i / (float)(fftSize / 2)) * 0.2f);
        auto fftDataIndex = jlimit(0, fftSize / 2 - 1, (int)(skewedProportionX * (fftSize / 2)));
        auto level = jmap(jlimit(mindB, maxdB, 20.0f * std::log10(fftData[fftDataIndex])), mindB, maxdB, 0.0f, 1.0f);
        
        scopeData[i] = level;
    }
}

void SpectrumAnalyzer::resized() {
    // Spectrum analyzer handles its own layout
}