#include "FLStreamEditor.h"

//==============================================================================
FLStreamEditor::FLStreamEditor(FLStreamProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setSize(EDITOR_WIDTH, EDITOR_HEIGHT);
    setResizable(false, false);
    
    setupComponents();
    setupParameterAttachments();
    
    // Setup processor callbacks with message thread marshalling
    audioProcessor.onLogMessage = [this](const String& message) {
        MessageManager::callAsync([this, message]() {
            addLogMessage(message);
        });
    };
    
    audioProcessor.onUserJoined = [this](const String& userId, const String& userName) {
        MessageManager::callAsync([this, userId, userName]() {
            addLogMessage("User joined: " + userName + " (" + userId + ")");
            updateConnectionStatus();
        });
    };
    
    audioProcessor.onUserLeft = [this](const String& userId) {
        MessageManager::callAsync([this, userId]() {
            addLogMessage("User left: " + userId);
            updateConnectionStatus();
        });
    };
    
    audioProcessor.onStatsUpdate = [this](const FLStreamProcessor::StreamingStats& stats) {
        MessageManager::callAsync([this, stats]() {
            lastStats = stats;
            updateStatistics();
        });
    };
    
    // Start UI updates
    startTimerHz(30); // 30 FPS updates
    
    addLogMessage("FL Studio Audio Streamer ready");
}

FLStreamEditor::~FLStreamEditor()
{
    stopTimer();
}

//==============================================================================
void FLStreamEditor::setupComponents()
{
    // Title and status
    titleLabel.setText("FL Studio Audio Streamer", dontSendNotification);
    titleLabel.setFont(FontOptions().withHeight(24.0f));
    titleLabel.setJustificationType(Justification::centred);
    titleLabel.setColour(Label::textColourId, FL_TEXT);
    addAndMakeVisible(titleLabel);
    
    statusLabel.setText("Ready", dontSendNotification);
    statusLabel.setFont(FontOptions().withHeight(14.0f));
    statusLabel.setJustificationType(Justification::centred);
    statusLabel.setColour(Label::textColourId, FL_SUCCESS);
    addAndMakeVisible(statusLabel);
    
    // Connection group
    connectionGroup.setText("Connection Settings");
    connectionGroup.setColour(GroupComponent::textColourId, FL_TEXT);
    connectionGroup.setColour(GroupComponent::outlineColourId, FL_ACCENT);
    addAndMakeVisible(connectionGroup);
    
    // Streaming mode
    streamingModeLabel.setText("Mode:", dontSendNotification);
    streamingModeLabel.setColour(Label::textColourId, FL_TEXT);
    addAndMakeVisible(streamingModeLabel);
    
    streamingModeCombo.addItem("Disabled", 1);
    streamingModeCombo.addItem("Server (Broadcast Audio)", 2);
    streamingModeCombo.addItem("Client (Receive Audio)", 3);
    streamingModeCombo.setSelectedId(1);
    streamingModeCombo.addListener(this);
    addAndMakeVisible(streamingModeCombo);
    
    // Server controls
    serverStartButton.setButtonText("Start Server");
    serverStartButton.addListener(this);
    serverStartButton.setColour(TextButton::buttonColourId, FL_SUCCESS);
    addAndMakeVisible(serverStartButton);
    
    connectButton.setButtonText("Connect to Server");
    connectButton.addListener(this);
    connectButton.setColour(TextButton::buttonColourId, FL_ACCENT);
    connectButton.setEnabled(false);
    addAndMakeVisible(connectButton);
    
    // Port setting
    portLabel.setText("Port:", dontSendNotification);
    portLabel.setColour(Label::textColourId, FL_TEXT);
    addAndMakeVisible(portLabel);
    
    portSlider.setRange(9000, 9999, 1);
    portSlider.setValue(9001);
    portSlider.setSliderStyle(Slider::IncDecButtons);
    portSlider.setTextBoxStyle(Slider::TextBoxLeft, false, 60, 20);
    portSlider.addListener(this);
    addAndMakeVisible(portSlider);
    
    // Room ID
    roomIdLabel.setText("Room ID:", dontSendNotification);
    roomIdLabel.setColour(Label::textColourId, FL_TEXT);
    addAndMakeVisible(roomIdLabel);
    
    roomIdEditor.setText("FL-ROOM-001");
    roomIdEditor.setFont(Font(Font::getDefaultMonospacedFontName(), 14.0f, Font::plain));
    addAndMakeVisible(roomIdEditor);
    
    // Audio controls group
    audioGroup.setText("Audio Controls");
    audioGroup.setColour(GroupComponent::textColourId, FL_TEXT);
    audioGroup.setColour(GroupComponent::outlineColourId, FL_ACCENT);
    addAndMakeVisible(audioGroup);
    
    // Input gain
    inputGainLabel.setText("Input Gain", dontSendNotification);
    inputGainLabel.setColour(Label::textColourId, FL_TEXT);
    addAndMakeVisible(inputGainLabel);
    
    inputGainSlider.setRange(0.0, 2.0, 0.01);
    inputGainSlider.setValue(1.0);
    inputGainSlider.setSliderStyle(Slider::Rotary);
    inputGainSlider.setTextBoxStyle(Slider::TextBoxBelow, false, 60, 20);
    inputGainSlider.addListener(this);
    addAndMakeVisible(inputGainSlider);
    
    inputGainValueLabel.setText("100%", dontSendNotification);
    inputGainValueLabel.setColour(Label::textColourId, FL_ACCENT);
    inputGainValueLabel.setJustificationType(Justification::centred);
    addAndMakeVisible(inputGainValueLabel);
    
    // Output gain
    outputGainLabel.setText("Output Gain", dontSendNotification);
    outputGainLabel.setColour(Label::textColourId, FL_TEXT);
    addAndMakeVisible(outputGainLabel);
    
    outputGainSlider.setRange(0.0, 2.0, 0.01);
    outputGainSlider.setValue(1.0);
    outputGainSlider.setSliderStyle(Slider::Rotary);
    outputGainSlider.setTextBoxStyle(Slider::TextBoxBelow, false, 60, 20);
    outputGainSlider.addListener(this);
    addAndMakeVisible(outputGainSlider);
    
    outputGainValueLabel.setText("100%", dontSendNotification);
    outputGainValueLabel.setColour(Label::textColourId, FL_ACCENT);
    outputGainValueLabel.setJustificationType(Justification::centred);
    addAndMakeVisible(outputGainValueLabel);
    
    // Mix amount
    mixAmountLabel.setText("Network Mix", dontSendNotification);
    mixAmountLabel.setColour(Label::textColourId, FL_TEXT);
    addAndMakeVisible(mixAmountLabel);
    
    mixAmountSlider.setRange(0.0, 1.0, 0.01);
    mixAmountSlider.setValue(1.0);
    mixAmountSlider.setSliderStyle(Slider::Rotary);
    mixAmountSlider.setTextBoxStyle(Slider::TextBoxBelow, false, 60, 20);
    mixAmountSlider.addListener(this);
    addAndMakeVisible(mixAmountSlider);
    
    mixAmountValueLabel.setText("100%", dontSendNotification);
    mixAmountValueLabel.setColour(Label::textColourId, FL_ACCENT);
    mixAmountValueLabel.setJustificationType(Justification::centred);
    addAndMakeVisible(mixAmountValueLabel);
    
    // Advanced group
    advancedGroup.setText("Advanced Settings");
    advancedGroup.setColour(GroupComponent::textColourId, FL_TEXT);
    advancedGroup.setColour(GroupComponent::outlineColourId, FL_ACCENT);
    addAndMakeVisible(advancedGroup);
    
    // Master track mode
    masterTrackToggle.setButtonText("Master Track Mode");
    masterTrackToggle.setColour(ToggleButton::textColourId, FL_TEXT);
    addAndMakeVisible(masterTrackToggle);
    
    // Latency compensation
    latencyCompLabel.setText("Latency Comp (samples)", dontSendNotification);
    latencyCompLabel.setColour(Label::textColourId, FL_TEXT);
    addAndMakeVisible(latencyCompLabel);
    
    latencyCompSlider.setRange(0, 4800, 1);
    latencyCompSlider.setValue(0);
    latencyCompSlider.setSliderStyle(Slider::LinearHorizontal);
    latencyCompSlider.setTextBoxStyle(Slider::TextBoxRight, false, 60, 20);
    latencyCompSlider.addListener(this);
    addAndMakeVisible(latencyCompSlider);
    
    latencyCompValueLabel.setText("0 ms", dontSendNotification);
    latencyCompValueLabel.setColour(Label::textColourId, FL_ACCENT);
    addAndMakeVisible(latencyCompValueLabel);
    
    // Status group
    statusGroup.setText("Status & Statistics");
    statusGroup.setColour(GroupComponent::textColourId, FL_TEXT);
    statusGroup.setColour(GroupComponent::outlineColourId, FL_ACCENT);
    addAndMakeVisible(statusGroup);
    
    // Status labels
    connectedUsersLabel.setText("Connected Users:", dontSendNotification);
    connectedUsersLabel.setColour(Label::textColourId, FL_TEXT);
    addAndMakeVisible(connectedUsersLabel);
    
    connectedUsersValue.setText("0", dontSendNotification);
    connectedUsersValue.setColour(Label::textColourId, FL_ACCENT);
    addAndMakeVisible(connectedUsersValue);
    
    bandwidthLabel.setText("Bandwidth:", dontSendNotification);
    bandwidthLabel.setColour(Label::textColourId, FL_TEXT);
    addAndMakeVisible(bandwidthLabel);
    
    bandwidthValue.setText("0 Mbps", dontSendNotification);
    bandwidthValue.setColour(Label::textColourId, FL_ACCENT);
    addAndMakeVisible(bandwidthValue);
    
    latencyLabel.setText("Latency:", dontSendNotification);
    latencyLabel.setColour(Label::textColourId, FL_TEXT);
    addAndMakeVisible(latencyLabel);
    
    latencyValue.setText("-- ms", dontSendNotification);
    latencyValue.setColour(Label::textColourId, FL_ACCENT);
    addAndMakeVisible(latencyValue);
    
    cpuUsageLabel.setText("CPU Usage:", dontSendNotification);
    cpuUsageLabel.setColour(Label::textColourId, FL_TEXT);
    addAndMakeVisible(cpuUsageLabel);
    
    cpuUsageValue.setText("0%", dontSendNotification);
    cpuUsageValue.setColour(Label::textColourId, FL_ACCENT);
    addAndMakeVisible(cpuUsageValue);
    
    // Audio meters
    inputMeterLabel.setText("Input Level", dontSendNotification);
    inputMeterLabel.setColour(Label::textColourId, FL_TEXT);
    addAndMakeVisible(inputMeterLabel);
    
    outputMeterLabel.setText("Output Level", dontSendNotification);
    outputMeterLabel.setColour(Label::textColourId, FL_TEXT);
    addAndMakeVisible(outputMeterLabel);
    
    addAndMakeVisible(inputMeterComponent);
    addAndMakeVisible(outputMeterComponent);
    
    // Log group
    logGroup.setText("Activity Log");
    logGroup.setColour(GroupComponent::textColourId, FL_TEXT);
    logGroup.setColour(GroupComponent::outlineColourId, FL_ACCENT);
    addAndMakeVisible(logGroup);
    
    logTextEditor.setMultiLine(true);
    logTextEditor.setReadOnly(true);
    logTextEditor.setFont(Font(Font::getDefaultMonospacedFontName(), 12.0f, Font::plain));
    logTextEditor.setColour(TextEditor::backgroundColourId, Colour(0xff2c2c2c));
    logTextEditor.setColour(TextEditor::textColourId, FL_TEXT);
    addAndMakeVisible(logTextEditor);
    
    clearLogButton.setButtonText("Clear Log");
    clearLogButton.addListener(this);
    addAndMakeVisible(clearLogButton);
    
    openWebClientButton.setButtonText("Open Web Client");
    openWebClientButton.addListener(this);
    openWebClientButton.setColour(TextButton::buttonColourId, FL_SUCCESS);
    addAndMakeVisible(openWebClientButton);
}

void FLStreamEditor::setupParameterAttachments()
{
    auto& parameters = audioProcessor.getParameters();
    
    streamingModeAttachment = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment>(
        parameters, "streamingMode", streamingModeCombo);
    
    inputGainAttachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(
        parameters, "inputGain", inputGainSlider);
    
    outputGainAttachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(
        parameters, "outputGain", outputGainSlider);
    
    mixAmountAttachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(
        parameters, "mixAmount", mixAmountSlider);
    
    latencyCompAttachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(
        parameters, "latencyComp", latencyCompSlider);
    
    portAttachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(
        parameters, "wsPort", portSlider);
    
    masterTrackAttachment = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment>(
        parameters, "masterTrack", masterTrackToggle);
}

//==============================================================================
void FLStreamEditor::paint(Graphics& g)
{
    // FL Studio style background
    g.fillAll(FL_BACKGROUND);
    
    // Paint audio meters
    Rectangle<int> inputMeterBounds = inputMeterComponent.getBounds();
    if (!inputMeterBounds.isEmpty()) {
        inputMeter.paint(g, inputMeterBounds);
    }
    
    Rectangle<int> outputMeterBounds = outputMeterComponent.getBounds();
    if (!outputMeterBounds.isEmpty()) {
        outputMeter.paint(g, outputMeterBounds);
    }
}

void FLStreamEditor::resized()
{
    int y = MARGIN;
    
    // Title section
    titleLabel.setBounds(MARGIN, y, getWidth() - 2 * MARGIN, 30);
    y += 35;
    
    statusLabel.setBounds(MARGIN, y, getWidth() - 2 * MARGIN, 20);
    y += 30;
    
    // Connection section
    connectionGroup.setBounds(MARGIN, y, getWidth() - 2 * MARGIN, 120);
    y += 20;
    
    streamingModeLabel.setBounds(MARGIN + 10, y, 60, COMPONENT_HEIGHT);
    streamingModeCombo.setBounds(MARGIN + 80, y, 150, COMPONENT_HEIGHT);
    
    serverStartButton.setBounds(MARGIN + 240, y, 100, COMPONENT_HEIGHT);
    connectButton.setBounds(MARGIN + 350, y, 120, COMPONENT_HEIGHT);
    y += 35;
    
    portLabel.setBounds(MARGIN + 10, y, 40, COMPONENT_HEIGHT);
    portSlider.setBounds(MARGIN + 60, y, 100, COMPONENT_HEIGHT);
    
    roomIdLabel.setBounds(MARGIN + 180, y, 70, COMPONENT_HEIGHT);
    roomIdEditor.setBounds(MARGIN + 260, y, 120, COMPONENT_HEIGHT);
    y += 35;
    
    openWebClientButton.setBounds(MARGIN + 10, y, 150, COMPONENT_HEIGHT);
    y += 50;
    
    // Audio controls section
    audioGroup.setBounds(MARGIN, y, getWidth() - 2 * MARGIN, 120);
    y += 25;
    
    int knobWidth = 80;
    int knobSpacing = (getWidth() - 2 * MARGIN - 3 * knobWidth) / 4;
    
    // Input gain
    int knobX = MARGIN + knobSpacing;
    inputGainLabel.setBounds(knobX, y, knobWidth, 15);
    inputGainSlider.setBounds(knobX, y + 15, knobWidth, knobWidth);
    inputGainValueLabel.setBounds(knobX, y + 15 + knobWidth, knobWidth, 15);
    
    // Output gain
    knobX += knobWidth + knobSpacing;
    outputGainLabel.setBounds(knobX, y, knobWidth, 15);
    outputGainSlider.setBounds(knobX, y + 15, knobWidth, knobWidth);
    outputGainValueLabel.setBounds(knobX, y + 15 + knobWidth, knobWidth, 15);
    
    // Mix amount
    knobX += knobWidth + knobSpacing;
    mixAmountLabel.setBounds(knobX, y, knobWidth, 15);
    mixAmountSlider.setBounds(knobX, y + 15, knobWidth, knobWidth);
    mixAmountValueLabel.setBounds(knobX, y + 15 + knobWidth, knobWidth, 15);
    
    y += 120;
    
    // Advanced section
    advancedGroup.setBounds(MARGIN, y, getWidth() - 2 * MARGIN, 80);
    y += 25;
    
    masterTrackToggle.setBounds(MARGIN + 10, y, 150, COMPONENT_HEIGHT);
    y += 35;
    
    latencyCompLabel.setBounds(MARGIN + 10, y, 140, COMPONENT_HEIGHT);
    latencyCompSlider.setBounds(MARGIN + 160, y, 200, COMPONENT_HEIGHT);
    latencyCompValueLabel.setBounds(MARGIN + 370, y, 80, COMPONENT_HEIGHT);
    y += 50;
    
    // Status section  
    statusGroup.setBounds(MARGIN, y, getWidth() - 2 * MARGIN, 100);
    y += 25;
    
    int statusCol1 = MARGIN + 10;
    int statusCol2 = MARGIN + 120;
    int statusCol3 = MARGIN + 260;
    int statusCol4 = MARGIN + 370;
    
    connectedUsersLabel.setBounds(statusCol1, y, 100, COMPONENT_HEIGHT);
    connectedUsersValue.setBounds(statusCol2, y, 80, COMPONENT_HEIGHT);
    
    bandwidthLabel.setBounds(statusCol3, y, 80, COMPONENT_HEIGHT);
    bandwidthValue.setBounds(statusCol4, y, 100, COMPONENT_HEIGHT);
    y += 25;
    
    latencyLabel.setBounds(statusCol1, y, 100, COMPONENT_HEIGHT);
    latencyValue.setBounds(statusCol2, y, 80, COMPONENT_HEIGHT);
    
    cpuUsageLabel.setBounds(statusCol3, y, 80, COMPONENT_HEIGHT);
    cpuUsageValue.setBounds(statusCol4, y, 100, COMPONENT_HEIGHT);
    y += 25;
    
    // Audio meters
    inputMeterLabel.setBounds(statusCol1, y, 100, 15);
    inputMeterComponent.setBounds(statusCol1, y + 15, 200, 20);
    
    outputMeterLabel.setBounds(statusCol3, y, 100, 15);
    outputMeterComponent.setBounds(statusCol3, y + 15, 200, 20);
    y += 55;
    
    // Log section
    logGroup.setBounds(MARGIN, y, getWidth() - 2 * MARGIN, 120);
    y += 25;
    
    logTextEditor.setBounds(MARGIN + 10, y, getWidth() - 2 * MARGIN - 20, 70);
    y += 75;
    
    clearLogButton.setBounds(MARGIN + 10, y, 100, COMPONENT_HEIGHT);
}

//==============================================================================
void FLStreamEditor::timerCallback()
{
    updateConnectionStatus();
    updateStreamingMode();
    updateAudioLevels();
    
    // Update value labels
    inputGainValueLabel.setText(formatPercentage(inputGainSlider.getValue()), dontSendNotification);
    outputGainValueLabel.setText(formatPercentage(outputGainSlider.getValue()), dontSendNotification);
    mixAmountValueLabel.setText(formatPercentage(mixAmountSlider.getValue()), dontSendNotification);
    
    // Update latency compensation
    const double latencyMs = (latencyCompSlider.getValue() / 48000.0) * 1000.0;
    latencyCompValueLabel.setText(String(latencyMs, 1) + " ms", dontSendNotification);
}

//==============================================================================
void FLStreamEditor::buttonClicked(Button* button)
{
    if (button == &serverStartButton) {
        if (audioProcessor.isServerRunning()) {
            audioProcessor.stopWebSocketServer();
            serverStartButton.setButtonText("Start Server");
            serverStartButton.setColour(TextButton::buttonColourId, FL_SUCCESS);
        } else {
            const int port = static_cast<int>(portSlider.getValue());
            if (audioProcessor.startWebSocketServer(port)) {
                serverStartButton.setButtonText("Stop Server");
                serverStartButton.setColour(TextButton::buttonColourId, FL_DANGER);
                
                audioProcessor.setRoomId(roomIdEditor.getText());
                addLogMessage("Server started on port " + String(port));
            }
        }
    }
    else if (button == &connectButton) {
        // Client connection logic would go here
        addLogMessage("Client connection not implemented yet");
    }
    else if (button == &clearLogButton) {
        logTextEditor.clear();
    }
    else if (button == &openWebClientButton) {
        openWebClient();
    }
}

void FLStreamEditor::sliderValueChanged(Slider*)
{
    // Parameter attachments handle the actual parameter updates
    // This is for any additional UI updates if needed
}

void FLStreamEditor::comboBoxChanged(ComboBox* comboBoxThatHasChanged)
{
    if (comboBoxThatHasChanged == &streamingModeCombo) {
        updateStreamingMode();
    }
}

//==============================================================================
void FLStreamEditor::updateConnectionStatus()
{
    isConnected = audioProcessor.isServerRunning();
    isServerMode = audioProcessor.isStreamingServer();
    
    if (isConnected) {
        statusLabel.setText("Connected - " + String(audioProcessor.getConnectedUserCount()) + " users", 
                          dontSendNotification);
        statusLabel.setColour(Label::textColourId, FL_SUCCESS);
    } else {
        statusLabel.setText("Ready", dontSendNotification);
        statusLabel.setColour(Label::textColourId, FL_TEXT_SECONDARY);
    }
    
    connectedUsersValue.setText(String(audioProcessor.getConnectedUserCount()), dontSendNotification);
}

void FLStreamEditor::updateStreamingMode()
{
    const int modeIndex = streamingModeCombo.getSelectedId();
    
    serverStartButton.setEnabled(modeIndex == 2); // Server mode
    connectButton.setEnabled(modeIndex == 3);     // Client mode
    openWebClientButton.setEnabled(audioProcessor.isServerRunning());
}

void FLStreamEditor::updateAudioLevels()
{
    // Get audio levels from processor
    const auto stats = audioProcessor.getStreamingStats();
    
    // Update input meter (placeholder - would need actual audio level data)
    inputMeter.update(stats.audioLevel * 0.7f, stats.audioLevel);
    
    // Update output meter
    outputMeter.update(stats.audioLevel * 0.5f, stats.audioLevel * 0.8f);
    
    // Trigger repaint for meters
    inputMeterComponent.repaint();
    outputMeterComponent.repaint();
}

void FLStreamEditor::updateStatistics()
{
    bandwidthValue.setText(formatBandwidth(lastStats.bandwidth), dontSendNotification);
    latencyValue.setText(formatLatency(lastStats.latency), dontSendNotification);
    cpuUsageValue.setText(formatPercentage(lastStats.cpuUsage / 100.0), dontSendNotification);
}

//==============================================================================
void FLStreamEditor::addLogMessage(const String& message)
{
    const String timestamp = Time::getCurrentTime().formatted("%H:%M:%S");
    const String logEntry = "[" + timestamp + "] " + message + "\n";
    
    logTextEditor.insertTextAtCaret(logEntry);
    logTextEditor.moveCaretToEnd();
}

void FLStreamEditor::openWebClient()
{
    if (!audioProcessor.isServerRunning()) {
        addLogMessage("Error: Server not running");
        return;
    }
    
    const int port = static_cast<int>(portSlider.getValue());
    const String url = "http://localhost:" + String(port);
    
    // Open in default browser
    URL(url).launchInDefaultBrowser();
    
    addLogMessage("Opening web client: " + url);
}

//==============================================================================
String FLStreamEditor::formatBandwidth(double mbps)
{
    if (mbps < 0.001) return "0 kbps";
    if (mbps < 1.0) return String(mbps * 1000.0, 1) + " kbps";
    return String(mbps, 2) + " Mbps";
}

String FLStreamEditor::formatLatency(double ms)
{
    if (ms < 0.1) return "< 0.1 ms";
    return String(ms, 1) + " ms";
}

String FLStreamEditor::formatPercentage(double value)
{
    return String(static_cast<int>(value * 100)) + "%";
}