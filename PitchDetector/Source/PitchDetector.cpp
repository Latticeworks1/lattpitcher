#include "PitchDetector.h"
#include <algorithm>
#include <cmath>

//==============================================================================
// PITCH DETECTION ENGINE IMPLEMENTATION
//==============================================================================

PitchDetectionEngine::PitchDetectionEngine()
{
    initializeTelemetry();
}

float PitchDetectionEngine::detectPitch(const float* buffer, int size, double sampleRate)
{
    if (!buffer || size <= 0) return 0.0f;
    
    // Simple autocorrelation-based detection
    float frequency = autocorrelationPitchDetection(buffer, size, sampleRate);
    
    // Apply stability filter
    frequency = applyStabilityFilter(frequency);
    
    // Update telemetry
    telemetry.totalDetectionAttempts++;
    if (frequency > 0.0f) {
        telemetry.successfulDetections++;
        telemetry.recentFrequencies.push_back(frequency);
        if (telemetry.recentFrequencies.size() > 100)
            telemetry.recentFrequencies.erase(telemetry.recentFrequencies.begin());
    }
    
    return frequency;
}

float PitchDetectionEngine::autocorrelationPitchDetection(const float* buffer, int size, double sampleRate)
{
    if (size < 64) return 0.0f;
    
    int minPeriod = (int)(sampleRate / maxFrequency);
    int maxPeriod = (int)(sampleRate / minFrequency);
    
    if (maxPeriod > size / 2) maxPeriod = size / 2;
    if (minPeriod < 1) minPeriod = 1;
    
    std::vector<float> autocorr(maxPeriod + 1, 0.0f);
    
    // Compute autocorrelation
    for (int lag = minPeriod; lag <= maxPeriod; ++lag) {
        float sum = 0.0f;
        for (int i = 0; i < size - lag; ++i) {
            sum += buffer[i] * buffer[i + lag];
        }
        autocorr[lag] = sum;
    }
    
    // Find peak
    float maxCorr = 0.0f;
    int bestLag = 0;
    for (int lag = minPeriod; lag <= maxPeriod; ++lag) {
        if (autocorr[lag] > maxCorr) {
            maxCorr = autocorr[lag];
            bestLag = lag;
        }
    }
    
    if (bestLag == 0 || maxCorr < noiseThreshold * size) return 0.0f;
    
    return (float)sampleRate / bestLag;
}

float PitchDetectionEngine::detectPitchYin(const float* buffer, int size, double sampleRate)
{
    return autocorrelationPitchDetection(buffer, size, sampleRate); // Simplified
}

float PitchDetectionEngine::detectPitchHPS(const float* buffer, int size, double sampleRate)
{
    return autocorrelationPitchDetection(buffer, size, sampleRate); // Simplified
}

float PitchDetectionEngine::detectPitchCepstrum(const float* buffer, int size, double sampleRate)
{
    return autocorrelationPitchDetection(buffer, size, sampleRate); // Simplified
}

NoteInfo PitchDetectionEngine::frequencyToNote(float frequency)
{
    NoteInfo info;
    if (frequency <= 0.0f) return info;
    
    const float A4 = PitchDetectorConstants::DEFAULT_REFERENCE_PITCH;
    const std::array<String, 12> noteNames = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    
    float logFreq = std::log2(frequency / A4);
    int totalSemitones = (int)std::round(logFreq * PitchDetectorConstants::SEMITONES_PER_OCTAVE);
    
    info.octave = 4 + totalSemitones / PitchDetectorConstants::SEMITONES_PER_OCTAVE;
    int noteIndex = (totalSemitones % PitchDetectorConstants::SEMITONES_PER_OCTAVE + PitchDetectorConstants::SEMITONES_PER_OCTAVE) % PitchDetectorConstants::SEMITONES_PER_OCTAVE;
    info.noteName = noteNames[noteIndex];
    
    float expectedFreq = A4 * std::pow(2.0f, totalSemitones / (float)PitchDetectorConstants::SEMITONES_PER_OCTAVE);
    info.centsDeviation = PitchDetectorConstants::CENTS_PER_OCTAVE * std::log2(frequency / expectedFreq);
    info.isValid = true;
    
    return info;
}

float PitchDetectionEngine::applyStabilityFilter(float newFrequency)
{
    if (newFrequency <= 0.0f) return 0.0f;
    
    recentDetections.push_back(newFrequency);
    if (recentDetections.size() > stabilityWindow)
        recentDetections.erase(recentDetections.begin());
    
    return newFrequency; // Simplified - no filtering
}

void PitchDetectionEngine::initializeTelemetry()
{
    telemetry.reset();
    telemetry.platformInfo = SystemStats::getJUCEVersion();
}

String PitchDetectionEngine::exportTelemetryJson() const
{
    return "{}"; // Simplified
}

//==============================================================================
// AUTOTUNE ENGINE IMPLEMENTATION  
//==============================================================================

AutotuneEngine::AutotuneEngine()
{
    delayBuffer.resize(maxDelayInSamples, 0.0f);
    windowBuffer.resize(PitchDetectorConstants::AUTOTUNE_WINDOW_SIZE, 0.0f);
    overlapBuffer.resize(PitchDetectorConstants::AUTOTUNE_OVERLAP_SIZE, 0.0f);
    
    // Create Hann window
    const int windowSize = PitchDetectorConstants::AUTOTUNE_WINDOW_SIZE;
    for (int i = 0; i < windowSize; ++i)
        windowBuffer[i] = 0.5f * (1.0f - std::cos(2.0f * MathConstants<float>::pi * i / (windowSize - 1)));
}

void AutotuneEngine::prepareToPlay(double newSampleRate, int maximumExpectedSamplesPerBlock)
{
    sampleRate = newSampleRate;
    blockSize = maximumExpectedSamplesPerBlock;
    reset();
}

void AutotuneEngine::processBlock(AudioBuffer<float>& buffer, const float* pitchData, int numSamples)
{
    if (!pitchData || numSamples <= 0) return;
    
    float* audioData = buffer.getWritePointer(0);
    
    // Simple pitch correction
    for (int i = 0; i < numSamples; ++i) {
        float detectedPitch = pitchData[i];
        if (detectedPitch > 0.0f) {
            float targetPitch = calculateTargetPitch(detectedPitch);
            float correctionRatio = targetPitch / detectedPitch;
            
            // Simple amplitude modulation (placeholder for real pitch shifting)
            audioData[i] *= 1.0f + (correctionRatio - 1.0f) * settings.correctionStrength * 0.1f;
        }
    }
    
    totalProcessedSamples += numSamples;
}

void AutotuneEngine::reset()
{
    currentTargetPitch = 0.0f;
    previousTargetPitch = 0.0f;
    currentCorrectionAmount = 0.0f;
    pitchCorrectionActive = false;
    std::fill(delayBuffer.begin(), delayBuffer.end(), 0.0f);
}

float AutotuneEngine::calculateTargetPitch(float detectedPitch)
{
    if (detectedPitch <= 0.0f) return 0.0f;
    
    int midiNote = frequencyToMidiNote(detectedPitch);
    int targetMidiNote = findNearestScaleNote(midiNote);
    return midiNoteToFrequency(targetMidiNote);
}

int AutotuneEngine::frequencyToMidiNote(float frequency) const
{
    return (int)std::round(PitchDetectorConstants::SEMITONES_PER_OCTAVE * std::log2(frequency / settings.referencePitch)) + 69;
}

float AutotuneEngine::midiNoteToFrequency(int midiNote) const
{
    return settings.referencePitch * std::pow(2.0f, (midiNote - 69) / (float)PitchDetectorConstants::SEMITONES_PER_OCTAVE);
}

bool AutotuneEngine::isNoteInScale(int midiNote) const
{
    int noteClass = (midiNote - settings.rootNote + 12) % 12;
    return settings.customScale[noteClass];
}

int AutotuneEngine::findNearestScaleNote(int midiNote) const
{
    if (settings.scaleType == ScaleType::Chromatic) return midiNote;
    if (isNoteInScale(midiNote)) return midiNote;
    
    // Find nearest note in scale (search up to half octave)
    const int maxSearchDistance = PitchDetectorConstants::SEMITONES_PER_OCTAVE / 2;
    for (int distance = 1; distance <= maxSearchDistance; ++distance) {
        if (isNoteInScale(midiNote + distance)) return midiNote + distance;
        if (isNoteInScale(midiNote - distance)) return midiNote - distance;
    }
    
    return midiNote;
}

std::array<bool, 12> AutotuneEngine::getActiveScale() const
{
    switch (settings.scaleType) {
        case ScaleType::Major:
            return {true, false, true, false, true, true, false, true, false, true, false, true};
        case ScaleType::Minor:
            return {true, false, true, true, false, true, false, true, true, false, true, false};
        case ScaleType::Pentatonic:
            return {true, false, true, false, true, false, false, true, false, true, false, false};
        case ScaleType::Blues:
            return {true, false, false, true, false, true, true, true, false, false, true, false};
        case ScaleType::Dorian:
            return {true, false, true, true, false, true, false, true, false, true, true, false};
        case ScaleType::Custom:
            return settings.customScale;
        case ScaleType::Chromatic:
        default:
            return {true, true, true, true, true, true, true, true, true, true, true, true};
    }
}

//==============================================================================
// GUI COMPONENTS IMPLEMENTATION
//==============================================================================

CircularPitchTuner::CircularPitchTuner()
{
    setSize(PitchDetectorConstants::CIRCULAR_TUNER_SIZE, PitchDetectorConstants::CIRCULAR_TUNER_SIZE);
}

void CircularPitchTuner::paint(Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced(10.0f);
    auto centre = bounds.getCentre();
    
    // Draw outer ring
    g.setColour(Colour(0x60ffffff));
    g.drawEllipse(bounds, 2.0f);
    
    // Draw center marks
    g.setColour(Colours::white);
    const int numMarks = PitchDetectorConstants::SEMITONES_PER_OCTAVE;
    for (int i = 0; i < numMarks; ++i) {
        float angle = i * MathConstants<float>::twoPi / numMarks - MathConstants<float>::halfPi;
        float x = centre.x + radius * 0.9f * std::cos(angle);
        float y = centre.y + radius * 0.9f * std::sin(angle);
        g.fillEllipse(x - 2, y - 2, 4, 4);
    }
    
    // Draw needle if we have valid pitch
    if (hasValidPitch) {
        const float centsThreshold = 20.0f;
        g.setColour(currentCents < -centsThreshold || currentCents > centsThreshold ? Colours::red : Colours::green);
        float needleAngle = currentCents * MathConstants<float>::pi / (2.0f * PitchDetectorConstants::CENTS_DISPLAY_RANGE);
        float needleX = centre.x + needleLength * std::sin(needleAngle);
        float needleY = centre.y - needleLength * std::cos(needleAngle);
        
        g.drawLine(centre.x, centre.y, needleX, needleY, 3.0f);
        g.fillEllipse(centre.x - 4, centre.y - 4, 8, 8);
    }
    
    // Draw note name
    g.setColour(Colours::white);
    g.setFont(FontOptions(20.0f, Font::bold));
    g.drawText(currentNote, bounds, Justification::centred);
}

void CircularPitchTuner::resized() {}

void CircularPitchTuner::updatePitch(float frequency, const NoteInfo& noteInfo)
{
    if (noteInfo.isValid) {
        currentCents = noteInfo.centsDeviation;
        currentNote = noteInfo.noteName + String(noteInfo.octave);
        hasValidPitch = true;
    } else {
        hasValidPitch = false;
        currentNote = "--";
    }
    repaint();
}

void CircularPitchTuner::setTargetNote(const String& note)
{
    targetNote = note;
    repaint();
}

//==============================================================================

AutotuneControls::AutotuneControls()
{
    // Title
    addAndMakeVisible(titleLabel);
    titleLabel.setText("Autotune Controls", dontSendNotification);
    titleLabel.setFont(FontOptions(20.0f, Font::bold));
    titleLabel.setJustificationType(Justification::centred);
    titleLabel.setColour(Label::textColourId, Colours::white);

    // Autotune ON/OFF button
    addAndMakeVisible(autotuneButton);
    autotuneButton.setButtonText("Autotune OFF");
    autotuneButton.setColour(TextButton::buttonColourId, Colour(0xff804040));

    // Sliders
    addAndMakeVisible(strengthSlider);
    strengthSlider.setRange(0.0, 1.0, 0.01);
    strengthSlider.setValue(0.8);
    strengthSlider.setSliderStyle(Slider::RotaryHorizontalVerticalDrag);

    addAndMakeVisible(speedSlider);
    speedSlider.setRange(0.0, 1.0, 0.01);
    speedSlider.setValue(0.5);
    speedSlider.setSliderStyle(Slider::RotaryHorizontalVerticalDrag);

    addAndMakeVisible(mixSlider);
    mixSlider.setRange(0.0, 1.0, 0.01);
    mixSlider.setValue(1.0);
    mixSlider.setSliderStyle(Slider::RotaryHorizontalVerticalDrag);

    // Labels
    addAndMakeVisible(strengthLabel);
    strengthLabel.setText("Strength", dontSendNotification);
    strengthLabel.attachToComponent(&strengthSlider, false);

    addAndMakeVisible(speedLabel);
    speedLabel.setText("Speed", dontSendNotification);
    speedLabel.attachToComponent(&speedSlider, false);

    addAndMakeVisible(mixLabel);
    mixLabel.setText("Mix", dontSendNotification);
    mixLabel.attachToComponent(&mixSlider, false);
}

AutotuneControls::~AutotuneControls() {}

void AutotuneControls::paint(Graphics& g)
{
    g.fillAll(Colour(0xff202020));
}

void AutotuneControls::resized()
{
    auto area = getLocalBounds().reduced(10);
    
    titleLabel.setBounds(area.removeFromTop(30));
    area.removeFromTop(10);
    
    autotuneButton.setBounds(area.removeFromTop(40));
    area.removeFromTop(20);
    
    auto sliderArea = area.removeFromTop(100);
    int sliderWidth = sliderArea.getWidth() / 3;
    
    strengthSlider.setBounds(sliderArea.removeFromLeft(sliderWidth).reduced(5));
    speedSlider.setBounds(sliderArea.removeFromLeft(sliderWidth).reduced(5));
    mixSlider.setBounds(sliderArea.reduced(5));
}

void AutotuneControls::setProcessor(AudioProcessor* proc)
{
    processor = proc;
}

//==============================================================================

PitchDetectorGUI::PitchDetectorGUI()
{
    setOpaque(true);
    initializeComponents();
    startTimerHz(PitchDetectorConstants::GUI_UPDATE_RATE_HZ);
    setSize(DEFAULT_WIDTH, DEFAULT_HEIGHT);
}

PitchDetectorGUI::~PitchDetectorGUI()
{
    stopTimer();
}

void PitchDetectorGUI::initializeComponents()
{
    addAndMakeVisible(circularTuner);
    
    addAndMakeVisible(noteNameLabel);
    noteNameLabel.setFont(FontOptions(36.0f, Font::bold));
    noteNameLabel.setJustificationType(Justification::centred);
    noteNameLabel.setText("--", dontSendNotification);
    noteNameLabel.setColour(Label::textColourId, Colours::white);
    
    addAndMakeVisible(frequencyLabel);
    frequencyLabel.setFont(FontOptions(14.0f));
    frequencyLabel.setJustificationType(Justification::centred);
    frequencyLabel.setText("-- Hz", dontSendNotification);
    frequencyLabel.setColour(Label::textColourId, Colours::lightgreen);
    
    addAndMakeVisible(centsLabel);
    centsLabel.setFont(FontOptions(14.0f));
    centsLabel.setJustificationType(Justification::centred);
    centsLabel.setText("-- cents", dontSendNotification);
    centsLabel.setColour(Label::textColourId, Colours::yellow);
    
    addAndMakeVisible(audioLevelLabel);
    audioLevelLabel.setFont(FontOptions(12.0f));
    audioLevelLabel.setJustificationType(Justification::centred);
    audioLevelLabel.setText("Level: --", dontSendNotification);
    audioLevelLabel.setColour(Label::textColourId, Colours::lightblue);
}

void PitchDetectorGUI::paint(Graphics& g)
{
    // Glassmorphism background
    g.fillAll(Colour(0xff1a1a1a));
    
    auto bounds = getLocalBounds().toFloat();
    
    // Glass effect background
    g.setColour(GLASS_BG);
    g.fillRoundedRectangle(bounds.reduced(10), 15.0f);
    
    g.setColour(GLASS_BORDER);
    g.drawRoundedRectangle(bounds.reduced(10), 15.0f, 1.0f);
}

void PitchDetectorGUI::resized()
{
    auto area = getLocalBounds().reduced(20);
    
    // Circular tuner at top
    const int tunerArea = 150;
    circularTuner.setBounds(area.removeFromTop(tunerArea).withSizeKeepingCentre(
        PitchDetectorConstants::CIRCULAR_TUNER_SIZE, 
        PitchDetectorConstants::CIRCULAR_TUNER_SIZE));
    area.removeFromTop(10);
    
    // Note name (large)
    noteNameLabel.setBounds(area.removeFromTop(50));
    area.removeFromTop(5);
    
    // Frequency and cents info
    frequencyLabel.setBounds(area.removeFromTop(25));
    centsLabel.setBounds(area.removeFromTop(25));
    area.removeFromTop(10);
    
    // Audio level
    audioLevelLabel.setBounds(area.removeFromTop(20));
}

void PitchDetectorGUI::updatePitchDisplay(float frequency, const NoteInfo& noteInfo)
{
    circularTuner.updatePitch(frequency, noteInfo);
    
    if (noteInfo.isValid) {
        noteNameLabel.setText(noteInfo.noteName + String(noteInfo.octave), dontSendNotification);
        frequencyLabel.setText(String(frequency, 1) + " Hz", dontSendNotification);
        centsLabel.setText(String(noteInfo.centsDeviation, 1) + " cents", dontSendNotification);
    } else {
        noteNameLabel.setText("--", dontSendNotification);
        frequencyLabel.setText("-- Hz", dontSendNotification);
        centsLabel.setText("-- cents", dontSendNotification);
    }
}

void PitchDetectorGUI::updateAudioLevel(float level)
{
    audioLevelLabel.setText("Level: " + String(level, 2), dontSendNotification);
}

void PitchDetectorGUI::timerCallback()
{
    // Update display from FIFO data
    int readIndex = fifoReadIndex.load();
    int writeIndex = fifoWriteIndex.load();
    
    if (readIndex != writeIndex) {
        float frequency = pitchFifo[readIndex];
        float level = levelFifo[readIndex];
        
        fifoReadIndex = (readIndex + 1) % fifoSize;
        
        NoteInfo noteInfo = engine.frequencyToNote(frequency);
        updatePitchDisplay(frequency, noteInfo);
        updateAudioLevel(level);
    }
}

void PitchDetectorGUI::setupStandaloneAudio() {}
void PitchDetectorGUI::shutdownStandaloneAudio() {}

//==============================================================================
// AUDIO PROCESSOR IMPLEMENTATION
//==============================================================================

PitchDetectorProcessor::PitchDetectorProcessor()
    : AudioProcessor(BusesProperties()
                    .withInput("Input", AudioChannelSet::mono(), true)
                    .withOutput("Output", AudioChannelSet::mono(), true)),
      parameterTreeState(*this, nullptr, Identifier("Parameters"), createParameterLayout())
{
    std::fill(fifo, fifo + fifoSize, 0.0f);
    std::fill(processingBuffer, processingBuffer + fifoSize, 0.0f);
    
    autotuneEngine = std::make_unique<AutotuneEngine>();
    pitchBuffer.resize(fifoSize, 0.0f);
    
    parameterTreeState.addParameterListener("correctionStrength", this);
    parameterTreeState.addParameterListener("correctionSpeed", this);
    parameterTreeState.addParameterListener("mixAmount", this);
    parameterTreeState.addParameterListener("rootNote", this);
}

PitchDetectorProcessor::~PitchDetectorProcessor() {}

AudioProcessorValueTreeState::ParameterLayout PitchDetectorProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<RangedAudioParameter>> parameters;

    parameters.push_back(std::make_unique<AudioParameterFloat>(
        "correctionStrength", "Correction Strength", 
        NormalisableRange<float>(0.0f, 1.0f, 0.01f), PitchDetectorConstants::DEFAULT_CORRECTION_STRENGTH));
    
    parameters.push_back(std::make_unique<AudioParameterFloat>(
        "correctionSpeed", "Correction Speed", 
        NormalisableRange<float>(0.0f, 1.0f, 0.01f), PitchDetectorConstants::DEFAULT_CORRECTION_SPEED));
    
    parameters.push_back(std::make_unique<AudioParameterFloat>(
        "mixAmount", "Mix Amount", 
        NormalisableRange<float>(0.0f, 1.0f, 0.01f), PitchDetectorConstants::DEFAULT_MIX_AMOUNT));
    
    parameters.push_back(std::make_unique<AudioParameterInt>(
        "rootNote", "Root Note", 0, PitchDetectorConstants::SEMITONES_PER_OCTAVE - 1, 0));

    return { parameters.begin(), parameters.end() };
}

void PitchDetectorProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    if (autotuneEngine)
        autotuneEngine->prepareToPlay(sampleRate, samplesPerBlock);
}

void PitchDetectorProcessor::releaseResources() {}

void PitchDetectorProcessor::processBlock(AudioBuffer<float>& buffer, MidiBuffer& midiMessages)
{
    ScopedNoDenormals noDenormals;
    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    if (totalNumInputChannels > 0) {
        const float* inputData = buffer.getReadPointer(0);
        int numSamples = buffer.getNumSamples();
        
        pushSamplesToFifo(inputData, numSamples);
        
        if (nextBlockReady.load()) {
            processAudioBlock();
            nextBlockReady.store(false);
        }
        
        if (autotuneEnabled && autotuneEngine) {
            autotuneEngine->processBlock(buffer, pitchBuffer.data(), numSamples);
        }
    }
}

void PitchDetectorProcessor::pushSamplesToFifo(const float* samples, int numSamples)
{
    for (int i = 0; i < numSamples; ++i) {
        fifo[fifoIndex] = samples[i];
        fifoIndex = (fifoIndex + 1) % fifoSize;
        
        if (fifoIndex == 0)
            nextBlockReady.store(true);
    }
}

void PitchDetectorProcessor::processAudioBlock()
{
    // Simple pitch detection on FIFO buffer
    PitchDetectionEngine engine;
    float frequency = engine.detectPitch(fifo, fifoSize, getSampleRate());
    
    // Store for autotune
    std::fill(pitchBuffer.begin(), pitchBuffer.end(), frequency);
    
    // Update GUI if connected
    if (gui) {
        NoteInfo noteInfo = engine.frequencyToNote(frequency);
        gui->updatePitchDisplay(frequency, noteInfo);
    }
}

void PitchDetectorProcessor::parameterChanged(const String& parameterID, float newValue)
{
    if (parameterID == "correctionStrength" && autotuneEngine)
        autotuneEngine->setCorrectionStrength(newValue);
    else if (parameterID == "correctionSpeed" && autotuneEngine)
        autotuneEngine->setCorrectionSpeed(newValue);
    else if (parameterID == "mixAmount" && autotuneEngine)
        autotuneEngine->setMixAmount(newValue);
    else if (parameterID == "rootNote" && autotuneEngine)
        autotuneEngine->setRootNote((int)newValue);
}

AudioProcessorEditor* PitchDetectorProcessor::createEditor()
{
    return new PitchDetectorEditor(*this);
}

void PitchDetectorProcessor::getStateInformation(MemoryBlock& destData)
{
    auto state = parameterTreeState.copyState();
    std::unique_ptr<XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void PitchDetectorProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState.get() != nullptr)
        if (xmlState->hasTagName(parameterTreeState.state.getType()))
            parameterTreeState.replaceState(ValueTree::fromXml(*xmlState));
}

void PitchDetectorProcessor::startStopRecording() {}
void PitchDetectorProcessor::saveRecording() {}

//==============================================================================
// PLUGIN EDITOR IMPLEMENTATION
//==============================================================================

PitchDetectorEditor::PitchDetectorEditor(PitchDetectorProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    addAndMakeVisible(gui);
    setSize(PitchDetectorConstants::DEFAULT_WINDOW_WIDTH, PitchDetectorConstants::DEFAULT_WINDOW_HEIGHT);
    
    // Connect GUI to processor
    audioProcessor.gui = &gui;
}

PitchDetectorEditor::~PitchDetectorEditor()
{
    audioProcessor.gui = nullptr;
}

void PitchDetectorEditor::paint(Graphics& g) {}

void PitchDetectorEditor::resized()
{
    gui.setBounds(getLocalBounds());
}

//==============================================================================
// STANDALONE APP IMPLEMENTATION
//==============================================================================

StandalonePitchDetector::StandalonePitchDetector()
{
    setOpaque(true);
    
    auto envFlag = SystemStats::getEnvironmentVariable("PD_DISABLE_AUDIO", {});
    disableAudio = envFlag.equalsIgnoreCase("1") || envFlag.equalsIgnoreCase("true");
    
    std::fill(fifo, fifo + fifoSize, 0.0f);
    std::fill(processingBuffer, processingBuffer + fifoSize, 0.0f);
    
    addAndMakeVisible(gui);
    
    gui.onExportTelemetry = [this] { exportTelemetryData(); };
    gui.onResetTelemetry = [this] { resetTelemetryData(); };
    
    addAndMakeVisible(permissionStatusLabel);
    permissionStatusLabel.setFont(FontOptions(14.0f, Font::bold));
    permissionStatusLabel.setJustificationType(Justification::centred);
    
    addAndMakeVisible(permissionButton);
    addAndMakeVisible(audioSettingsButton);
    
    if (!disableAudio) {
        setAudioChannels(1, 0);
        startTimerHz(PitchDetectorConstants::STANDALONE_TIMER_HZ);
    }
    
    setSize(PitchDetectorConstants::DEFAULT_WINDOW_WIDTH + 100, PitchDetectorConstants::DEFAULT_WINDOW_HEIGHT + 100);
}

StandalonePitchDetector::~StandalonePitchDetector()
{
    stopTimer();
    shutdownAudio();
}

void StandalonePitchDetector::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    engine.initializeTelemetry();
    audioSetupFailed = false;
}

void StandalonePitchDetector::getNextAudioBlock(const AudioSourceChannelInfo& bufferToFill)
{
    if (bufferToFill.buffer->getNumChannels() > 0) {
        const float* inputData = bufferToFill.buffer->getReadPointer(0, bufferToFill.startSample);
        pushSamplesToFifo(inputData, bufferToFill.numSamples);
        
        if (nextBlockReady) {
            processAudioBlock();
            nextBlockReady = false;
        }
    }
    
    bufferToFill.clearActiveBufferRegion();
}

void StandalonePitchDetector::releaseResources() {}

void StandalonePitchDetector::pushSamplesToFifo(const float* samples, int numSamples)
{
    for (int i = 0; i < numSamples; ++i) {
        fifo[fifoIndex] = samples[i];
        fifoIndex = (fifoIndex + 1) % fifoSize;
        
        if (fifoIndex == 0)
            nextBlockReady = true;
    }
}

void StandalonePitchDetector::processAudioBlock()
{
    double actualSampleRate = deviceManager.getCurrentAudioDevice() ? 
                              deviceManager.getCurrentAudioDevice()->getCurrentSampleRate() : 
                              PitchDetectorConstants::DEFAULT_SAMPLE_RATE;
    float frequency = engine.detectPitch(fifo, fifoSize, actualSampleRate);
    
    NoteInfo noteInfo = engine.frequencyToNote(frequency);
    
    // Update GUI via FIFO
    int writeIndex = gui.fifoWriteIndex.load();
    gui.pitchFifo[writeIndex] = frequency;
    gui.levelFifo[writeIndex] = 0.5f; // Simplified
    gui.fifoWriteIndex = (writeIndex + 1) % gui.fifoSize;
}

void StandalonePitchDetector::timerCallback() {}

void StandalonePitchDetector::paint(Graphics& g)
{
    g.fillAll(Colour(0xff202020));
}

void StandalonePitchDetector::resized()
{
    auto area = getLocalBounds();
    
    if (!disableAudio && audioSetupFailed) {
        permissionStatusLabel.setBounds(area.removeFromTop(40).reduced(10));
        permissionButton.setBounds(area.removeFromTop(40).reduced(10));
        audioSettingsButton.setBounds(area.removeFromTop(40).reduced(10));
        area.removeFromTop(10);
    }
    
    gui.setBounds(area);
}

void StandalonePitchDetector::initializePermissions() {}
void StandalonePitchDetector::checkPermissionStatus() {}
void StandalonePitchDetector::updatePermissionUI() {}
void StandalonePitchDetector::requestMicrophonePermission() {}
void StandalonePitchDetector::setupAudioWithPermission() {}
void StandalonePitchDetector::exportTelemetryData() {}
void StandalonePitchDetector::resetTelemetryData() {}
void StandalonePitchDetector::showAudioSettings() {}

// Create plugin instance
AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PitchDetectorProcessor();
}