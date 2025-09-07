#include "PitchDetectorProcessor.h"
#include "PitchDetectorEditor.h"

//==============================================================================
PitchDetectorProcessor::PitchDetectorProcessor()
    : AudioProcessor(BusesProperties()
                    .withInput("Input", AudioChannelSet::mono(), true)
                    .withOutput("Output", AudioChannelSet::mono(), true)),
      parameterTreeState(*this, nullptr, "Parameters", createParameterLayout())
{
    std::fill(fifo, fifo + fifoSize, 0.0f);
    std::fill(processingBuffer, processingBuffer + fifoSize, 0.0f);
    
    // Initialize autotune engines
    autotuneEngine = std::make_unique<AutotuneEngine>();
    neuralAutotuneEngine = std::make_unique<NeuralSpectralAutotuneEngine>();
    pitchBuffer.resize(fifoSize, 0.0f);
    
    // Connect VST parameters to engine setters
    parameterTreeState.addParameterListener("neuralIntensity", this);
    parameterTreeState.addParameterListener("correctionStrength", this);
    parameterTreeState.addParameterListener("correctionSpeed", this);
    parameterTreeState.addParameterListener("mixAmount", this);
    parameterTreeState.addParameterListener("rootNote", this);
}

PitchDetectorProcessor::~PitchDetectorProcessor()
{
}

//==============================================================================
AudioProcessorValueTreeState::ParameterLayout PitchDetectorProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<RangedAudioParameter>> parameters;

    // Core autotune parameters for DAW automation
    parameters.push_back(std::make_unique<AudioParameterFloat>(
        "neuralIntensity", "Neural Intensity", 
        NormalisableRange<float>(0.0f, 2.0f, 0.01f), 1.0f));
    
    parameters.push_back(std::make_unique<AudioParameterFloat>(
        "correctionStrength", "Correction Strength", 
        NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.8f));
    
    parameters.push_back(std::make_unique<AudioParameterFloat>(
        "correctionSpeed", "Correction Speed", 
        NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));
    
    parameters.push_back(std::make_unique<AudioParameterFloat>(
        "mixAmount", "Mix Amount", 
        NormalisableRange<float>(0.0f, 1.0f, 0.01f), 1.0f));
    
    parameters.push_back(std::make_unique<AudioParameterInt>(
        "rootNote", "Root Note", 0, 11, 0));

    return { parameters.begin(), parameters.end() };
}

//==============================================================================
void PitchDetectorProcessor::parameterChanged(const String& parameterID, float newValue)
{
    if (parameterID == "neuralIntensity" && neuralAutotuneEngine)
        neuralAutotuneEngine->setCorrectionIntensity(newValue);
    else if (parameterID == "correctionStrength" && autotuneEngine)
        autotuneEngine->setCorrectionStrength(newValue);
    else if (parameterID == "correctionSpeed" && autotuneEngine)
        autotuneEngine->setCorrectionSpeed(newValue);
    else if (parameterID == "mixAmount" && autotuneEngine)
        autotuneEngine->setMixAmount(newValue);
    else if (parameterID == "rootNote" && autotuneEngine)
        autotuneEngine->setRootNote((int)newValue);
}

//==============================================================================
void PitchDetectorProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    // Reset processing state
    fifoIndex = 0;
    nextBlockReady = false;
    audioBlockCount = 0;
    
    // Initialize autotune engines
    if (autotuneEngine)
    {
        autotuneEngine->prepareToPlay(sampleRate, samplesPerBlock);
        // TESTING: Force PhaseVocoder engine to evaluate PV improvements
        autotuneEngine->setEngineMode(AutotuneEngine::EngineMode::PhaseVocoder);
    }
    if (neuralAutotuneEngine)
    {
        neuralAutotuneEngine->prepareToPlay(sampleRate, samplesPerBlock);
    }
}

void PitchDetectorProcessor::releaseResources()
{
    // Nothing to clean up
}

//==============================================================================
void PitchDetectorProcessor::processBlock(AudioBuffer<float>& buffer, MidiBuffer& midiMessages)
{
    // SECURITY: Critical error handling for audio processing
    try {
        ignoreUnused(midiMessages);
        
        ScopedNoDenormals noDenormals;
        
        // SECURITY: Validate buffer integrity
        if (buffer.getNumSamples() <= 0 || buffer.getNumSamples() > 16384) {
            jassertfalse; // Debug alert for invalid buffer size
            return; // Reject oversized buffers that could cause problems
        }
        
        auto totalNumInputChannels = getBusesLayout().getMainInputChannelSet().size();
        auto totalNumOutputChannels = getBusesLayout().getMainOutputChannelSet().size();
        
        // SECURITY: Sanity check channel counts
        if (totalNumInputChannels < 0 || totalNumInputChannels > 32 ||
            totalNumOutputChannels < 0 || totalNumOutputChannels > 32) {
            jassertfalse; // Debug alert for suspicious channel configuration
            return;
        }
    
    // Clear output channels
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());
    
    if (wantsToRecord)
    {
        int startSample = recordedInput.getNumSamples();
        recordedInput.setSize(recordedInput.getNumChannels(), recordedInput.getNumSamples() + buffer.getNumSamples(), true);
        for (int i = 0; i < buffer.getNumChannels(); ++i)
        {
            recordedInput.copyFrom(i, startSample, buffer, i, 0, buffer.getNumSamples());
        }
    }

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
        
        if (gui)
        {
            auto& engine = gui->getEngine();
            auto& telemetry = engine.getTelemetry();
            telemetry.totalAudioSamples += numSamples;

            // Push samples to FIFO for processing
            pushSamplesToFifo(channelData, numSamples);

            // Update latest result with audio level
            {
                ScopedLock lock(resultLock);
                latestResult.audioLevel = rms;
            }

            // Process if we have a full buffer ready
            if (nextBlockReady) {
                processAudioBlock();
                nextBlockReady = false;
            }

            if (latestResult.hasNewData)
            {
                gui->updatePitchDisplay(latestResult.frequency, latestResult.noteInfo);
            }
            gui->updateAudioLevel(latestResult.audioLevel);
        }
        
        // Apply autotune processing or passthrough
        if (totalNumOutputChannels > 0) {
            if (autotuneEnabled) {
                // Copy input to output buffer first
                buffer.copyFrom(0, 0, channelData, numSamples);
                
                if (useNeuralAutotune && neuralAutotuneEngine) {
                    // 🚀 USE OUR REVOLUTIONARY NEURAL SPECTRAL AUTOTUNE! 🚀
                    neuralAutotuneEngine->processBlock(buffer);
                } else if (autotuneEngine) {
                    // Fallback to standard autotune
                    float currentPitch = latestResult.frequency;
                    std::fill(pitchBuffer.begin(), pitchBuffer.begin() + numSamples, currentPitch);
                    autotuneEngine->processBlock(buffer, pitchBuffer.data(), numSamples);
                }
            } else {
                // Passthrough mode - copy input to output
                buffer.copyFrom(0, 0, channelData, numSamples);
            }
        }
    }

        if (wantsToRecord)
        {
            int startSample = recordedOutput.getNumSamples();
            recordedOutput.setSize(recordedOutput.getNumChannels(), recordedOutput.getNumSamples() + buffer.getNumSamples(), true);
            for (int i = 0; i < buffer.getNumChannels(); ++i)
            {
                recordedOutput.copyFrom(i, startSample, buffer, i, 0, buffer.getNumSamples());
            }
        }
        
    } catch (const std::exception& e) {
        // SECURITY: Graceful degradation on critical errors
        jassertfalse; // Debug alert for exception in audio processing
        
        // Clear output buffer to prevent audio artifacts from corrupted processing
        buffer.clear();
        
        // Reset processing state to prevent cascading failures
        fifoIndex = 0;
        nextBlockReady = false;
        
        // Log error for debugging (in release builds, this helps identify issues)
        DBG("PitchDetectorProcessor: Exception in processBlock - " << e.what());
        
    } catch (...) {
        // SECURITY: Handle any other unexpected exceptions
        jassertfalse; // Debug alert for unknown exception
        buffer.clear(); // Ensure safe output
        fifoIndex = 0;
        nextBlockReady = false;
        DBG("PitchDetectorProcessor: Unknown exception in processBlock");
    }
}

//==============================================================================
void PitchDetectorProcessor::pushSamplesToFifo(const float* samples, int numSamples)
{
    // SECURITY: Input validation to prevent buffer overflows
    if (samples == nullptr) {
        jassertfalse; // Debug alert for null pointer
        return;
    }
    
    if (numSamples <= 0 || numSamples > 16384) { // Reasonable max block size limit
        jassertfalse; // Debug alert for invalid size
        return;
    }
    
    for (int i = 0; i < numSamples; ++i) {
        // SECURITY: Bounds check fifoIndex before access
        if (fifoIndex >= 0 && fifoIndex < fifoSize) {
            // SECURITY: Validate sample value to prevent audio exploits
            float sample = samples[i];
            if (std::isfinite(sample)) { // Reject NaN/inf values
                fifo[fifoIndex] = jlimit(-10.0f, 10.0f, sample); // Clamp to safe range
            } else {
                fifo[fifoIndex] = 0.0f; // Safe fallback for invalid samples
            }
        }
        
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
    if (!gui) return;
    auto& engine = gui->getEngine();

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
void PitchDetectorProcessor::getStateInformation(MemoryBlock& destData)
{
    // Use parameter tree state for VST automation compatibility
    auto state = parameterTreeState.copyState();
    
    // Add non-automated parameters
    if (gui) {
        auto& engine = gui->getEngine();
        state.setProperty("noiseThreshold", engine.getNoiseThreshold(), nullptr);
        state.setProperty("minFrequency", engine.getMinFrequency(), nullptr);
        state.setProperty("maxFrequency", engine.getMaxFrequency(), nullptr);
        state.setProperty("correlationThreshold", engine.getCorrelationThreshold(), nullptr);
    }
    state.setProperty("autotuneEnabled", autotuneEnabled, nullptr);
    state.setProperty("useNeuralAutotune", useNeuralAutotune, nullptr);
    
    std::unique_ptr<XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void PitchDetectorProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    
    if (xml.get() != nullptr) {
        // Restore parameter tree state (handles VST automation)
        auto state = ValueTree::fromXml(*xml);
        if (state.isValid())
            parameterTreeState.replaceState(state);
        
        // Restore non-automated parameters
        if (gui) {
            auto& engine = gui->getEngine();
            engine.setNoiseThreshold((float)state.getProperty("noiseThreshold", 0.005));
            
            float minFreq = (float)state.getProperty("minFrequency", 60.0);
            float maxFreq = (float)state.getProperty("maxFrequency", 1000.0);
            engine.setFrequencyRange(minFreq, maxFreq);
            
            engine.setCorrelationThreshold((float)state.getProperty("correlationThreshold", 0.3));
        }
        
        autotuneEnabled = state.getProperty("autotuneEnabled", true);
        useNeuralAutotune = state.getProperty("useNeuralAutotune", true);
    }
}

//==============================================================================
AudioProcessorEditor* PitchDetectorProcessor::createEditor()
{
    return new PitchDetectorEditor(*this);
}

void PitchDetectorProcessor::startStopRecording()
{
    if (wantsToRecord)
    {
        saveRecording();
        wantsToRecord = false;
    }
    else
    {
        recordedInput.setSize(getBusesLayout().getMainInputChannelSet().size(), 0);
        recordedOutput.setSize(getBusesLayout().getMainOutputChannelSet().size(), 0);
        wantsToRecord = true;
    }
}

void PitchDetectorProcessor::saveRecording()
{
    auto file = File::getSpecialLocation(File::userDocumentsDirectory).getChildFile("PitchDetectorRecording.wav");
    auto fileStream = std::unique_ptr<FileOutputStream>(file.createOutputStream());

    if (fileStream)
    {
        WavAudioFormat wavFormat;
        std::unique_ptr<AudioFormatWriter> writer(wavFormat.createWriterFor(fileStream.get(), getSampleRate(), recordedInput.getNumChannels(), 16, {}, 0));

        if (writer)
        {
            fileStream.release(); // The writer will delete the stream
            writer->writeFromAudioSampleBuffer(recordedInput, 0, recordedInput.getNumSamples());
        }
    }

    auto file2 = File::getSpecialLocation(File::userDocumentsDirectory).getChildFile("PitchDetectorRecording_processed.wav");
    auto fileStream2 = std::unique_ptr<FileOutputStream>(file2.createOutputStream());

    if (fileStream2)
    {
        WavAudioFormat wavFormat;
        std::unique_ptr<AudioFormatWriter> writer(wavFormat.createWriterFor(fileStream2.get(), getSampleRate(), recordedOutput.getNumChannels(), 16, {}, 0));

        if (writer)
        {            fileStream2.release(); // The writer will delete the stream

            writer->writeFromAudioSampleBuffer(recordedOutput, 0, recordedOutput.getNumSamples());
        }
    }
}

//==============================================================================
// This creates new instances of the plugin..
AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PitchDetectorProcessor();
}
