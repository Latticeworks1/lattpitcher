#include "PitchDetectorProcessor.h"
#include "PitchDetectorEditor.h"

//==============================================================================
PitchDetectorProcessor::PitchDetectorProcessor()
    : AudioProcessor(BusesProperties()
                    .withInput("Input", AudioChannelSet::mono(), true)
                    .withOutput("Output", AudioChannelSet::mono(), true))
{
    std::fill(fifo, fifo + fifoSize, 0.0f);
    std::fill(processingBuffer, processingBuffer + fifoSize, 0.0f);
}

PitchDetectorProcessor::~PitchDetectorProcessor()
{
}

//==============================================================================
void PitchDetectorProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    ignoreUnused(samplesPerBlock);
    
    // Initialize engine with current sample rate
    auto& telemetry = engine.getTelemetry();
    telemetry.sampleRateInfo = sampleRate;
    telemetry.bufferSizeInfo = fifoSize;
    
    // Reset processing state
    fifoIndex = 0;
    nextBlockReady = false;
    audioBlockCount = 0;
}

void PitchDetectorProcessor::releaseResources()
{
    // Nothing to clean up
}

//==============================================================================
void PitchDetectorProcessor::processBlock(AudioBuffer<float>& buffer, MidiBuffer& midiMessages)
{
    ignoreUnused(midiMessages);
    
    ScopedNoDenormals noDenormals;
    
    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();
    
    // Clear output channels
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());
    
    if (totalNumInputChannels > 0) {
        audioBlockCount++;
        
        const float* channelData = buffer.getReadPointer(0);
        int numSamples = buffer.getNumSamples();
        
        // Calculate RMS for audio level display
        float rms = 0.0f;
        for (int i = 0; i < numSamples; ++i) {
            rms += channelData[i] * channelData[i];
        }
        rms = std::sqrt(rms / numSamples);
        
        // Update telemetry
        auto& telemetry = engine.getTelemetry();
        telemetry.totalAudioSamples += numSamples;
        
        // Push samples to FIFO for processing
        pushSamplesToFifo(channelData, numSamples);
        
        // Update latest result with audio level
        {
            ScopedLock lock(resultLock);
            latestResult.audioLevel = rms;
        }
        
        // Copy input to output (passthrough)
        if (totalNumOutputChannels > 0) {
            buffer.copyFrom(0, 0, channelData, numSamples);
        }
        
        // Process if we have a full buffer ready
        if (nextBlockReady) {
            processAudioBlock();
            nextBlockReady = false;
        }
    }
    
    // Clear any unused output channels
    for (int i = totalNumInputChannels; i < totalNumOutputChannels; ++i) {
        buffer.clear(i, 0, buffer.getNumSamples());
    }
}

//==============================================================================
void PitchDetectorProcessor::pushSamplesToFifo(const float* samples, int numSamples)
{
    for (int i = 0; i < numSamples; ++i) {
        fifo[fifoIndex] = samples[i];
        
        if (++fifoIndex == fifoSize) {
            // Copy to processing buffer and signal ready
            std::copy(fifo, fifo + fifoSize, processingBuffer);
            fifoIndex = 0;
            nextBlockReady = true;
        }
    }
}

void PitchDetectorProcessor::processAudioBlock()
{
    double sampleRate = getSampleRate();
    if (sampleRate <= 0.0) return;
    
    // Detect pitch using the engine
    float frequency = engine.detectPitch(processingBuffer, fifoSize, sampleRate);
    NoteInfo noteInfo = engine.frequencyToNote(frequency);
    
    // Update thread-safe result
    {
        ScopedLock lock(resultLock);
        latestResult.frequency = frequency;
        latestResult.noteInfo = noteInfo;
        latestResult.hasNewData = true;
    }
}

//==============================================================================
PitchDetectorProcessor::DetectionResult PitchDetectorProcessor::getLatestResult()
{
    ScopedLock lock(resultLock);
    DetectionResult result = latestResult;
    latestResult.hasNewData = false;
    return result;
}

//==============================================================================
void PitchDetectorProcessor::getStateInformation(MemoryBlock& destData)
{
    // Save current engine parameters
    std::unique_ptr<XmlElement> xml(new XmlElement("PitchDetectorSettings"));
    
    xml->setAttribute("noiseThreshold", engine.getNoiseThreshold());
    xml->setAttribute("minFrequency", engine.getMinFrequency());
    xml->setAttribute("maxFrequency", engine.getMaxFrequency());
    xml->setAttribute("correlationThreshold", engine.getCorrelationThreshold());
    
    copyXmlToBinary(*xml, destData);
}

void PitchDetectorProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    // Restore engine parameters
    std::unique_ptr<XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    
    if (xml.get() != nullptr && xml->hasTagName("PitchDetectorSettings")) {
        engine.setNoiseThreshold((float)xml->getDoubleAttribute("noiseThreshold", 0.005));
        
        float minFreq = (float)xml->getDoubleAttribute("minFrequency", 60.0);
        float maxFreq = (float)xml->getDoubleAttribute("maxFrequency", 1000.0);
        engine.setFrequencyRange(minFreq, maxFreq);
        
        engine.setCorrelationThreshold((float)xml->getDoubleAttribute("correlationThreshold", 0.3));
    }
}

//==============================================================================
AudioProcessorEditor* PitchDetectorProcessor::createEditor()
{
    return new PitchDetectorEditor(*this);
}

//==============================================================================
// This creates new instances of the plugin
AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PitchDetectorProcessor();
}