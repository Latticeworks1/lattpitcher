#include "StandaloneApp.h"

//==============================================================================
StandalonePitchDetector::StandalonePitchDetector()
    : AudioAppComponent()
{
    setOpaque(true);
    
    // Initialize FIFO
    std::fill(fifo, fifo + fifoSize, 0.0f);
    std::fill(processingBuffer, processingBuffer + fifoSize, 0.0f);
    
    // Setup GUI
    addAndMakeVisible(gui);
    
    // Connect GUI callbacks
    gui.onExportTelemetry = [this] { exportTelemetryData(); };
    gui.onResetTelemetry = [this] { resetTelemetryData(); };
    
    // Permission components
    addAndMakeVisible(permissionStatusLabel);
    permissionStatusLabel.setFont(FontOptions(14.0f, Font::bold));
    permissionStatusLabel.setJustificationType(Justification::centred);
    permissionStatusLabel.setText("Checking Permissions...", dontSendNotification);
    
    addAndMakeVisible(permissionButton);
    permissionButton.setButtonText("Grant Microphone Access");
    permissionButton.setColour(TextButton::buttonColourId, Colour(0xff4CAF50));
    permissionButton.setColour(TextButton::textColourOnId, Colours::white);
    permissionButton.onClick = [this] { requestMicrophonePermission(); };

    // Audio settings button (always available)
    addAndMakeVisible(audioSettingsButton);
    audioSettingsButton.setButtonText("Audio Settings");
    audioSettingsButton.setColour(TextButton::buttonColourId, Colour(0xff404040));
    audioSettingsButton.setColour(TextButton::textColourOnId, Colours::white);
    audioSettingsButton.setColour(TextButton::textColourOffId, Colours::lightgrey);
    audioSettingsButton.onClick = [this] { showAudioSettings(); };
    
    // Initialize permissions
    initializePermissions();
    
    startTimerHz(60);
    setSize(800, 700); // Extra height for permission UI
}

StandalonePitchDetector::~StandalonePitchDetector()
{
    shutdownAudio();
    stopTimer();
}

//==============================================================================
void StandalonePitchDetector::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    ignoreUnused(samplesPerBlockExpected);
    
    // Initialize engine telemetry
    auto& telemetry = engine.getTelemetry();
    telemetry.sampleRateInfo = sampleRate;
    telemetry.bufferSizeInfo = fifoSize;
    
    audioSetupFailed = false;
    fifoIndex = 0;
    nextBlockReady = false;
    audioBlockCount = 0;
}

void StandalonePitchDetector::getNextAudioBlock(const AudioSourceChannelInfo& bufferToFill)
{
    // DON'T clear input data - we need it for pitch detection!
    // bufferToFill.clearActiveBufferRegion(); // REMOVED - this was wiping input
    
    if (bufferToFill.buffer->getNumChannels() > 0) {
        audioBlockCount++;
        
        const float* channelData = bufferToFill.buffer->getReadPointer(0, bufferToFill.startSample);
        int numSamples = bufferToFill.numSamples;
        
        // Calculate RMS for audio level
        float rms = 0.0f;
        for (int i = 0; i < numSamples; ++i) {
            rms += channelData[i] * channelData[i];
        }
        currentAudioLevel = std::sqrt(rms / numSamples);
        gui.updateAudioLevel(currentAudioLevel);
        
        // Update telemetry
        auto& telemetry = engine.getTelemetry();
        telemetry.totalAudioSamples += numSamples;
        
        // Debug audio input every 100 blocks
        if (audioBlockCount % 100 == 0) {
            DBG("Audio Block #" << audioBlockCount << " - RMS: " << currentAudioLevel << 
                " - Samples: " << numSamples << " - Rate: " << 
                (deviceManager.getCurrentAudioDevice() ? 
                 deviceManager.getCurrentAudioDevice()->getCurrentSampleRate() : 0.0));
            
            // Sample a few values to verify we're getting data
            if (numSamples > 0) {
                DBG("Sample values: " << channelData[0] << ", " << 
                    (numSamples > 1 ? channelData[1] : 0.0f) << ", " <<
                    (numSamples > 2 ? channelData[2] : 0.0f));
            }
        }
        
        // Alert if we're getting data but it's all zeros
        if (audioBlockCount % 500 == 0 && currentAudioLevel == 0.0f) {
            DBG("WARNING: 500 blocks of audio data but all samples are zero! Check microphone input.");
        }
        
        // Push to FIFO for processing
        pushSamplesToFifo(channelData, numSamples);
    } else {
        // Debug when we don't get channels
        if (audioBlockCount % 100 == 0) {
            DBG("WARNING: getNextAudioBlock called but no channels available!");
        }
    }
}

void StandalonePitchDetector::releaseResources()
{
    // Nothing to clean up
}

//==============================================================================
void StandalonePitchDetector::pushSamplesToFifo(const float* samples, int numSamples)
{
    for (int i = 0; i < numSamples; ++i) {
        fifo[fifoIndex] = samples[i];
        
        if (++fifoIndex == fifoSize) {
            // Copy to processing buffer and reset
            std::copy(fifo, fifo + fifoSize, processingBuffer);
            fifoIndex = 0;
            nextBlockReady = true;
            
            // Debug FIFO activity
            static int fifoCount = 0;
            fifoCount++;
            if (fifoCount % 10 == 0) { // Debug every 10th FIFO buffer
                // Check if we have actual signal
                float fifoRms = 0.0f;
                for (int j = 0; j < fifoSize; ++j) {
                    fifoRms += processingBuffer[j] * processingBuffer[j];
                }
                fifoRms = std::sqrt(fifoRms / fifoSize);
                
                DBG("FIFO Buffer #" << fifoCount << " ready - RMS: " << fifoRms << 
                    " - First few samples: " << processingBuffer[0] << ", " << 
                    processingBuffer[1] << ", " << processingBuffer[2]);
            }
        }
    }
}

void StandalonePitchDetector::processAudioBlock()
{
    if (deviceManager.getCurrentAudioDevice() == nullptr) {
        DBG("WARNING: processAudioBlock called but no audio device!");
        return;
    }
    
    double sampleRate = deviceManager.getCurrentAudioDevice()->getCurrentSampleRate();
    if (sampleRate <= 0.0) {
        DBG("WARNING: Invalid sample rate: " << sampleRate);
        return;
    }
    
    // Detect pitch
    float frequency = engine.detectPitch(processingBuffer, fifoSize, sampleRate);
    NoteInfo noteInfo = engine.frequencyToNote(frequency);
    
    // Debug pitch detection results
    static int detectionCount = 0;
    detectionCount++;
    if (detectionCount % 20 == 0) { // Debug every 20th detection
        DBG("Pitch Detection #" << detectionCount << " - Freq: " << frequency << 
            " Hz - Note: " << (noteInfo.isValid ? noteInfo.noteName + String(noteInfo.octave) : "None") <<
            " - Cents: " << (noteInfo.isValid ? String(noteInfo.centsDeviation, 1) : "N/A"));
    }
    
    // Update GUI
    gui.updatePitchDisplay(frequency, noteInfo);
}

//==============================================================================
void StandalonePitchDetector::paint(Graphics& g)
{
    g.fillAll(Colours::black);
}

void StandalonePitchDetector::resized()
{
    auto area = getLocalBounds();
    
    // Permission area at top
    auto permissionArea = area.removeFromTop(60);
    permissionStatusLabel.setBounds(permissionArea.removeFromTop(30).reduced(8));
    permissionButton.setBounds(permissionArea.removeFromLeft(200).reduced(8));
    audioSettingsButton.setBounds(permissionArea.removeFromLeft(160).reduced(8));

    // GUI takes rest
    gui.setBounds(area);
}

//==============================================================================
void StandalonePitchDetector::timerCallback()
{
    // Update audio level
    gui.updateAudioLevel(currentAudioLevel);
    
    // Process audio if ready
    if (nextBlockReady) {
        processAudioBlock();
        nextBlockReady = false;
    }
    
    // Update debug info
    String debugInfo = "=== STANDALONE DEBUG INFO ===\n";
    debugInfo += "Audio Setup: " + String(audioSetupFailed ? "FAILED" : "OK") + "\n";
    debugInfo += "Permission: " + String(RuntimePermissions::isGranted(RuntimePermissions::recordAudio) ? "GRANTED" : "DENIED") + "\n";
    
    if (deviceManager.getCurrentAudioDevice()) {
        debugInfo += "Sample Rate: " + String(deviceManager.getCurrentAudioDevice()->getCurrentSampleRate()) + " Hz\n";
        debugInfo += "Block Size: " + String(deviceManager.getCurrentAudioDevice()->getCurrentBufferSizeSamples()) + " samples\n";
    }
    
    debugInfo += "Audio Blocks: " + String(audioBlockCount) + "\n";
    debugInfo += "FIFO Index: " + String(fifoIndex) + "/" + String(fifoSize) + "\n";
    debugInfo += "Current Level: " + String(currentAudioLevel, 4) + "\n";
    
    const auto& telemetry = engine.getTelemetry();
    debugInfo += "Processing Cycles: " + String(telemetry.totalProcessingCycles) + "\n";
    debugInfo += "Detection Rate: " + String(telemetry.totalDetectionAttempts > 0 ? 
                                            (float)telemetry.successfulDetections / telemetry.totalDetectionAttempts * 100.0f : 0.0f, 1) + "%\n";
    
    if (audioSetupFailed) {
        debugInfo += "\nSTATUS: Audio setup failed!";
    } else if (audioBlockCount == 0) {
        debugInfo += "\nSTATUS: No audio blocks received.";
    } else if (currentAudioLevel == 0.0f) {
        debugInfo += "\nSTATUS: Audio received but no signal.";
    } else {
        debugInfo += "\nSTATUS: Audio input working!";
    }
    
    gui.updateDebugInfo(debugInfo);
    
    // Permission monitoring
    static int permissionCheckCounter = 0;
    if (++permissionCheckCounter % 5 == 0) { // Check every 5 seconds
        PermissionState oldState = currentPermissionState;
        checkPermissionStatus();
        if (oldState != currentPermissionState) {
            updatePermissionUI();
        }
    }
}

//==============================================================================
void StandalonePitchDetector::initializePermissions()
{
    checkPermissionStatus();
    updatePermissionUI();
}

void StandalonePitchDetector::checkPermissionStatus()
{
    if (RuntimePermissions::isGranted(RuntimePermissions::recordAudio)) {
        currentPermissionState = Granted;
    } else if (RuntimePermissions::isRequired(RuntimePermissions::recordAudio)) {
        currentPermissionState = NotDetermined;
    } else {
        currentPermissionState = Denied;
    }
}

void StandalonePitchDetector::updatePermissionUI()
{
    switch (currentPermissionState) {
        case Granted:
            permissionStatusLabel.setText("Microphone Permission: GRANTED", dontSendNotification);
            permissionStatusLabel.setColour(Label::textColourId, Colours::green);
            permissionButton.setVisible(false);
            setupAudioWithPermission();
            break;
            
        case NotDetermined:
            permissionStatusLabel.setText("Microphone Permission: REQUIRED", dontSendNotification);
            permissionStatusLabel.setColour(Label::textColourId, Colours::orange);
            permissionButton.setVisible(true);
            permissionButton.setButtonText("Grant Microphone Access");
            break;
            
        case Denied:
            permissionStatusLabel.setText("Microphone Permission: DENIED", dontSendNotification);
            permissionStatusLabel.setColour(Label::textColourId, Colours::red);
            permissionButton.setVisible(true);
            permissionButton.setButtonText("Open System Preferences");
            break;
            
        case Unknown:
        default:
            permissionStatusLabel.setText("Microphone Permission: CHECKING...", dontSendNotification);
            permissionStatusLabel.setColour(Label::textColourId, Colours::yellow);
            permissionButton.setVisible(false);
            break;
    }
}

void StandalonePitchDetector::requestMicrophonePermission()
{
    if (currentPermissionState == Denied) {
        #if JUCE_MAC
        URL systemPrefs("x-apple.systempreferences:com.apple.preference.security?Privacy_Microphone");
        systemPrefs.launchInDefaultBrowser();
        #endif
        return;
    }
    
    RuntimePermissions::request(RuntimePermissions::recordAudio, [this](bool granted) {
        if (granted) {
            currentPermissionState = Granted;
            setupAudioWithPermission();
        } else {
            currentPermissionState = Denied;
        }
        updatePermissionUI();
    });
}

void StandalonePitchDetector::setupAudioWithPermission()
{
    DBG("=== SETTING UP AUDIO WITH PERMISSION ===");
    
    // Configure audio setup
    AudioDeviceManager::AudioDeviceSetup setup;
    setup.inputDeviceName = String(); // Use default input device
    setup.outputDeviceName = String(); // Use default output device  
    setup.sampleRate = 44100.0; // Standard sample rate
    setup.bufferSize = 512; // Good balance of latency and stability
    setup.inputChannels.setRange(0, 1, true); // Enable first input channel
    setup.outputChannels.clear(); // No output needed
    setup.useDefaultInputChannels = true;
    setup.useDefaultOutputChannels = false;
    
    String errorMessage = deviceManager.initialise(1, 0, nullptr, true, String(), &setup);
    
    if (errorMessage.isEmpty()) {
        DBG("Audio device initialized successfully");
        DBG("Input device: " << (deviceManager.getCurrentAudioDevice() ? 
                                deviceManager.getCurrentAudioDevice()->getName() : "None"));
        DBG("Sample rate: " << (deviceManager.getCurrentAudioDevice() ? 
                               deviceManager.getCurrentAudioDevice()->getCurrentSampleRate() : 0.0));
        
        // Now set audio channels for this component
        setAudioChannels(1, 0); // Mono input, no output
        audioSetupFailed = false;

        // Update GUI status bar with device info
        if (auto* dev = deviceManager.getCurrentAudioDevice()) {
            gui.setAudioDeviceInfo(dev->getName(), dev->getCurrentSampleRate(), dev->getCurrentBufferSizeSamples());
        }
    } else {
        DBG("Audio initialization failed: " << errorMessage);
        audioSetupFailed = true;
        
        AlertWindow::showMessageBoxAsync(AlertWindow::WarningIcon,
                                        "Audio Setup Failed",
                                        "Failed to initialize audio input:\n" + errorMessage + 
                                        "\n\nPlease check your microphone connection and system audio settings.");
    }
}

//==============================================================================
void StandalonePitchDetector::exportTelemetryData()
{
    FileChooser chooser("Export Telemetry Data", 
                       File::getSpecialLocation(File::userDesktopDirectory).getChildFile("standalone_telemetry.json"), 
                       "*.json");
    
    chooser.launchAsync(FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles,
                       [this](const FileChooser& fc) {
        if (fc.getURLResults().size() > 0) {
            File outputFile = fc.getURLResults()[0].getLocalFile();
            String jsonData = engine.exportTelemetryJson();
            
            if (outputFile.replaceWithText(jsonData)) {
                AlertWindow::showMessageBoxAsync(AlertWindow::InfoIcon, "Export Complete", 
                                               "Telemetry data exported to:\n" + outputFile.getFullPathName());
            } else {
                AlertWindow::showMessageBoxAsync(AlertWindow::WarningIcon, "Export Failed", 
                                               "Failed to export telemetry data to file.");
            }
        }
    });
}

void StandalonePitchDetector::resetTelemetryData()
{
    engine.initializeTelemetry();
    AlertWindow::showMessageBoxAsync(AlertWindow::InfoIcon, "Telemetry Reset", 
                                   "Telemetry data has been reset.");
}

void StandalonePitchDetector::showAudioSettings()
{
    auto* selector = new AudioDeviceSelectorComponent(deviceManager,
                                                     1, 2,
                                                     0, 2,
                                                     true,
                                                     true,
                                                     true,
                                                     false);
    selector->setSize(520, 420);

    DialogWindow::LaunchOptions opts;
    opts.content.setOwned(selector);
    opts.dialogTitle = "Audio Settings";
    opts.componentToCentreAround = this;
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar = true;
    opts.resizable = true;
    opts.launchAsync();
}
