#include "MainComponent.h"

//==============================================================================
MainComponent::MainComponent()
    : audioBuffer(1, 48000 * 2) // 1 channel, 2 seconds at 48kHz
{
    analysisBuffer.resize(analysisWindowSize * 2);
    
    // Set up audio
    setAudioChannels (1, 0); // 1 input channel, 0 output channels
    
    // Set up timer for GUI updates (60 FPS)
    startTimer(16);
    
    setSize (800, 600);
}

MainComponent::~MainComponent()
{
    shutdownAudio();
}

//==============================================================================
void MainComponent::prepareToPlay (int samplesPerBlockExpected, double sampleRate)
{
    currentSampleRate = sampleRate;
    pitchDetector.setSampleRate(sampleRate);
    pitchDetector.setWindowSize(analysisWindowSize);
    pitchDetector.setThresholds(0.1f, 0.01f);
}

void MainComponent::getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill)
{
    // Clear output buffer (we're not generating output)
    bufferToFill.clearActiveBufferRegion();
    
    // Get input audio data
    auto* inputBuffer = bufferToFill.buffer;
    const int numSamples = bufferToFill.numSamples;
    
    if (inputBuffer->getNumChannels() > 0)
    {
        // Get input channel data
        auto channelData = inputBuffer->getReadPointer(0);
        
        // Create audio buffer for processing
        juce::AudioBuffer<float> tempInputBuffer(1, numSamples);
        tempInputBuffer.copyFrom(0, 0, channelData, numSamples);
        
        // Push audio data to circular buffer
        juce::dsp::AudioBlock<float> inputBlock(tempInputBuffer);
        audioBuffer.push(inputBlock);
        
        // Get latest audio window for analysis
        juce::AudioBuffer<float> tempAnalysisBuffer(1, (int)analysisBuffer.size());
        juce::dsp::AudioBlock<float> analysisBlock(tempAnalysisBuffer);
        audioBuffer.getLatest(analysisBlock);
        
        // Copy to our analysis buffer
        for (size_t i = 0; i < analysisBuffer.size(); ++i)
            analysisBuffer[i] = tempAnalysisBuffer.getSample(0, (int)i);
        
        // Perform pitch detection
        auto pitchResult = pitchDetector.detectPitch(analysisBuffer.data(), (int)analysisBuffer.size());
        
        // Update current pitch (thread-safe)
        {
            juce::ScopedLock lock(pitchLock);
            currentPitch = pitchResult;
        }
    }
}

void MainComponent::releaseResources()
{
    // No resources to release
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    // Background
    g.fillAll (juce::Colour::fromRGB(20, 25, 35));
    
    // Get current pitch data (thread-safe)
    YINPitchDetector::PitchResult pitch;
    {
        juce::ScopedLock lock(pitchLock);
        pitch = currentPitch;
    }
    
    // Smooth the display values
    if (pitch.isPitched)
    {
        displayedFrequency = displayedFrequency * 0.8f + pitch.frequency * 0.2f;
        displayedConfidence = displayedConfidence * 0.8f + pitch.confidence * 0.2f;
        isPitched = true;
    }
    else
    {
        displayedConfidence *= 0.9f; // Fade out confidence
        if (displayedConfidence < 0.1f)
            isPitched = false;
    }
    
    // Title
    g.setColour (juce::Colours::white);
    g.setFont (juce::Font(36.0f, juce::Font::bold));
    g.drawText ("YIN Pitch Detector", 0, 20, getWidth(), 50, juce::Justification::centred);
    
    // Main pitch display
    auto displayArea = getLocalBounds().reduced(50).withTop(100);
    
    if (isPitched && displayedConfidence > 0.3f)
    {
        // Frequency display
        g.setColour (juce::Colour::fromHSV(0.6f, 0.8f, 1.0f, 1.0f)); // Cyan
        g.setFont (juce::Font(72.0f, juce::Font::bold));
        
        juce::String freqText = juce::String(displayedFrequency, 1) + " Hz";
        g.drawText (freqText, displayArea.withHeight(100), juce::Justification::centred);
        
        // Note name (approximate)
        float a4 = 440.0f;
        float semitonesFromA4 = 12.0f * std::log2(displayedFrequency / a4);
        int semitoneIndex = juce::roundToInt(semitonesFromA4) % 12;
        if (semitoneIndex < 0) semitoneIndex += 12;
        
        juce::StringArray noteNames = { "A", "A#", "B", "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#" };
        juce::String noteName = noteNames[semitoneIndex];
        
        int octave = 4 + (int)std::floor((semitonesFromA4 + 9) / 12.0f);
        juce::String noteText = noteName + juce::String(octave);
        
        g.setFont (juce::Font(48.0f, juce::Font::bold));
        g.drawText (noteText, displayArea.withTop(200).withHeight(80), juce::Justification::centred);
        
        // Confidence bar
        auto confidenceArea = displayArea.withTop(320).withHeight(40);
        g.setColour (juce::Colours::darkgrey);
        g.drawRect (confidenceArea, 2);
        
        float confidenceWidth = confidenceArea.getWidth() * displayedConfidence;
        juce::Colour confidenceColor = juce::Colour::fromHSV(displayedConfidence * 0.3f, 0.8f, 1.0f, 1.0f);
        g.setColour (confidenceColor);
        g.fillRect (confidenceArea.withWidth((int)confidenceWidth));
        
        g.setColour (juce::Colours::lightgrey);
        g.setFont (juce::Font(16.0f));
        g.drawText ("Confidence: " + juce::String((int)(displayedConfidence * 100)) + "%", 
                   confidenceArea.withY(confidenceArea.getBottom() + 10), juce::Justification::centred);
    }
    else
    {
        // No pitch detected
        g.setColour (juce::Colours::grey);
        g.setFont (juce::Font(48.0f));
        g.drawText ("No Pitch Detected", displayArea, juce::Justification::centred);
    }
    
    // Technical info
    g.setColour (juce::Colours::lightgrey);
    g.setFont (juce::Font(14.0f));
    
    juce::String techInfo = "Sample Rate: " + juce::String((int)currentSampleRate) + " Hz | ";
    techInfo += "Window Size: " + juce::String(analysisWindowSize) + " samples | ";
    techInfo += "Period: " + juce::String(pitch.periodInSamples) + " samples";
    
    g.drawText (techInfo, 10, getHeight() - 30, getWidth() - 20, 20, juce::Justification::centred);
}

void MainComponent::resized()
{
    // Layout components if needed
}

//==============================================================================
void MainComponent::timerCallback()
{
    // Trigger repaint for smooth animations
    repaint();
}