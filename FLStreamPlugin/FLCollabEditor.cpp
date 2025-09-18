#include "FLCollabEditor.h"

class StatusUpdateTimer : public juce::Timer
{
public:
    StatusUpdateTimer(FLCollabEditor& editor) : editor_(editor) {}
    
    void timerCallback() override
    {
        editor_.updateConnectionStatus();
        editor_.updateAudioStatus();
    }
    
private:
    FLCollabEditor& editor_;
};

FLCollabEditor::FLCollabEditor(FLCollabAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setSize(600, 500);
    setResizable(false, false);
    
    // Create all components
    // Network Configuration Section
    networkGroup = std::make_unique<juce::GroupComponent>("network", "Network Configuration");
    addAndMakeVisible(*networkGroup);
    
    serverLabel = std::make_unique<juce::Label>("serverLabel", "Server:");
    serverLabel->setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(*serverLabel);
    
    serverAddressEditor = std::make_unique<juce::TextEditor>("serverAddress");
    serverAddressEditor->setText("your-worker.puter.site");
    serverAddressEditor->addListener(this);
    addAndMakeVisible(*serverAddressEditor);
    
    portLabel = std::make_unique<juce::Label>("portLabel", "HTTPS:");
    portLabel->setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(*portLabel);
    
    portEditor = std::make_unique<juce::TextEditor>("port");
    portEditor->setText("443");
    portEditor->addListener(this);
    addAndMakeVisible(*portEditor);
    
    connectButton = std::make_unique<juce::TextButton>("Connect");
    connectButton->addListener(this);
    addAndMakeVisible(*connectButton);
    
    connectionStatusLabel = std::make_unique<juce::Label>("connectionStatus", "Disconnected");
    connectionStatusLabel->setJustificationType(juce::Justification::centred);
    connectionStatusLabel->setColour(juce::Label::textColourId, juce::Colours::red);
    addAndMakeVisible(*connectionStatusLabel);
    
    // Room Configuration Section
    roomGroup = std::make_unique<juce::GroupComponent>("room", "Room Configuration");
    addAndMakeVisible(*roomGroup);
    
    roomCodeLabel = std::make_unique<juce::Label>("roomCodeLabel", "Room Code:");
    roomCodeLabel->setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(*roomCodeLabel);
    
    roomCodeEditor = std::make_unique<juce::TextEditor>("roomCode");
    roomCodeEditor->setText("fl_studio_collab_room_1");
    roomCodeEditor->addListener(this);
    addAndMakeVisible(*roomCodeEditor);
    
    userTypeLabel = std::make_unique<juce::Label>("userTypeLabel", "User Type:");
    userTypeLabel->setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(*userTypeLabel);
    
    userTypeCombo = std::make_unique<juce::ComboBox>("userType");
    userTypeCombo->addItem("Producer", 1);
    userTypeCombo->addItem("Vocalist", 2);
    userTypeCombo->setSelectedId(1);
    userTypeCombo->addListener(this);
    addAndMakeVisible(*userTypeCombo);
    
    joinRoomButton = std::make_unique<juce::TextButton>("Join Room");
    joinRoomButton->addListener(this);
    addAndMakeVisible(*joinRoomButton);
    
    // Audio Controls Section
    audioGroup = std::make_unique<juce::GroupComponent>("audio", "Audio Controls");
    addAndMakeVisible(*audioGroup);
    
    volumeLabel = std::make_unique<juce::Label>("volumeLabel", "Volume:");
    volumeLabel->setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(*volumeLabel);
    
    volumeSlider = std::make_unique<juce::Slider>("volume");
    volumeSlider->setSliderStyle(juce::Slider::LinearHorizontal);
    volumeSlider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 25);
    volumeSlider->setRange(0.0, 2.0, 0.01);
    volumeSlider->setValue(1.0);
    addAndMakeVisible(*volumeSlider);
    
    muteButton = std::make_unique<juce::ToggleButton>("Mute");
    muteButton->addListener(this);
    addAndMakeVisible(*muteButton);
    
    audioStatusLabel = std::make_unique<juce::Label>("audioStatus", "No Audio");
    audioStatusLabel->setJustificationType(juce::Justification::centred);
    audioStatusLabel->setColour(juce::Label::textColourId, juce::Colours::orange);
    addAndMakeVisible(*audioStatusLabel);
    
    // Status Display Section
    statusGroup = std::make_unique<juce::GroupComponent>("status", "Status Log");
    addAndMakeVisible(*statusGroup);
    
    logTextEditor = std::make_unique<juce::TextEditor>("log");
    logTextEditor->setMultiLine(true);
    logTextEditor->setReadOnly(true);
    logTextEditor->setScrollbarsShown(true);
    logTextEditor->setFont(juce::Font(juce::FontOptions(12.0f)));
    addAndMakeVisible(*logTextEditor);
    
    clearLogButton = std::make_unique<juce::TextButton>("Clear Log");
    clearLogButton->addListener(this);
    addAndMakeVisible(*clearLogButton);
    
    // Set up parameter attachments
    volumeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.parameters, "volume", *volumeSlider);
    muteAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.parameters, "mute", *muteButton);
    userTypeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.parameters, "userType", *userTypeCombo);
    
    // Set up update timer
    updateTimer = std::make_unique<StatusUpdateTimer>(*this);
    updateTimer->startTimer(1000); // Update every second
    
    setupDefaultValues();
    addLogMessage("FL Studio Collaboration Plugin Ready");
}

FLCollabEditor::~FLCollabEditor()
{
    if (updateTimer)
        updateTimer->stopTimer();
}

void FLCollabEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff2a2a2a));
    
    g.setColour(juce::Colours::white);
    g.setFont(16.0f);
    g.drawFittedText("FL Studio Collaboration", 10, 5, getWidth() - 20, 25, 
                     juce::Justification::centred, 1);
}

void FLCollabEditor::resized()
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop(35); // Title space
    
    // Network Configuration Section (top)
    auto networkBounds = bounds.removeFromTop(80);
    networkGroup->setBounds(networkBounds);
    
    auto networkContent = networkBounds.reduced(10);
    networkContent.removeFromTop(15); // Group title space
    
    auto networkRow1 = networkContent.removeFromTop(25);
    serverLabel->setBounds(networkRow1.removeFromLeft(60));
    serverAddressEditor->setBounds(networkRow1.removeFromLeft(120));
    networkRow1.removeFromLeft(10);
    portLabel->setBounds(networkRow1.removeFromLeft(40));
    portEditor->setBounds(networkRow1.removeFromLeft(60));
    networkRow1.removeFromLeft(10);
    connectButton->setBounds(networkRow1.removeFromLeft(80));
    
    auto networkRow2 = networkContent.removeFromTop(25);
    connectionStatusLabel->setBounds(networkRow2);
    
    bounds.removeFromTop(10); // Spacing
    
    // Room Configuration Section
    auto roomBounds = bounds.removeFromTop(80);
    roomGroup->setBounds(roomBounds);
    
    auto roomContent = roomBounds.reduced(10);
    roomContent.removeFromTop(15); // Group title space
    
    auto roomRow1 = roomContent.removeFromTop(25);
    roomCodeLabel->setBounds(roomRow1.removeFromLeft(80));
    roomCodeEditor->setBounds(roomRow1.removeFromLeft(200));
    roomRow1.removeFromLeft(10);
    userTypeLabel->setBounds(roomRow1.removeFromLeft(70));
    userTypeCombo->setBounds(roomRow1.removeFromLeft(100));
    
    auto roomRow2 = roomContent.removeFromTop(25);
    joinRoomButton->setBounds(roomRow2.removeFromLeft(100));
    
    bounds.removeFromTop(10); // Spacing
    
    // Audio Controls Section
    auto audioBounds = bounds.removeFromTop(80);
    audioGroup->setBounds(audioBounds);
    
    auto audioContent = audioBounds.reduced(10);
    audioContent.removeFromTop(15); // Group title space
    
    auto audioRow1 = audioContent.removeFromTop(25);
    volumeLabel->setBounds(audioRow1.removeFromLeft(60));
    volumeSlider->setBounds(audioRow1.removeFromLeft(200));
    audioRow1.removeFromLeft(10);
    muteButton->setBounds(audioRow1.removeFromLeft(60));
    
    auto audioRow2 = audioContent.removeFromTop(25);
    audioStatusLabel->setBounds(audioRow2);
    
    bounds.removeFromTop(10); // Spacing
    
    // Status Log Section (remaining space)
    statusGroup->setBounds(bounds);
    
    auto statusContent = bounds.reduced(10);
    statusContent.removeFromTop(15); // Group title space
    
    auto statusButtons = statusContent.removeFromBottom(30);
    clearLogButton->setBounds(statusButtons.removeFromLeft(100));
    
    statusContent.removeFromBottom(5); // Spacing
    logTextEditor->setBounds(statusContent);
}

void FLCollabEditor::buttonClicked(juce::Button* button)
{
    if (button == connectButton.get())
    {
        connectToServer();
    }
    else if (button == joinRoomButton.get())
    {
        joinRoom();
    }
    else if (button == clearLogButton.get())
    {
        logTextEditor->clear();
    }
}

void FLCollabEditor::comboBoxChanged(juce::ComboBox* comboBox)
{
    if (comboBox == userTypeCombo.get())
    {
        bool isProducer = (userTypeCombo->getSelectedId() == 1);
        audioProcessor.setUserType(isProducer);
        addLogMessage("User type changed to: " + 
                     juce::String(isProducer ? "Producer" : "Vocalist"));
    }
}

void FLCollabEditor::textEditorReturnKeyPressed(juce::TextEditor& editor)
{
    textEditorFocusLost(editor);
}

void FLCollabEditor::textEditorFocusLost(juce::TextEditor& editor)
{
    if (&editor == roomCodeEditor.get())
    {
        juce::String newRoomCode = roomCodeEditor->getText();
        audioProcessor.setRoomCode(newRoomCode);
        addLogMessage("Room code set to: " + newRoomCode);
    }
}

void FLCollabEditor::updateConnectionStatus()
{
    if (audioProcessor.isConnected())
    {
        connectionStatusLabel->setText("Connected", juce::dontSendNotification);
        connectionStatusLabel->setColour(juce::Label::textColourId, juce::Colours::green);
        connectButton->setButtonText("Disconnect");
    }
    else
    {
        connectionStatusLabel->setText("Disconnected", juce::dontSendNotification);
        connectionStatusLabel->setColour(juce::Label::textColourId, juce::Colours::red);
        connectButton->setButtonText("Connect");
    }
}

void FLCollabEditor::updateAudioStatus()
{
    // This would need to be implemented in the audio processor to track audio activity
    // For now, show basic status
    if (audioProcessor.isConnected() && !muteButton->getToggleState())
    {
        audioStatusLabel->setText("Audio Active", juce::dontSendNotification);
        audioStatusLabel->setColour(juce::Label::textColourId, juce::Colours::green);
    }
    else if (muteButton->getToggleState())
    {
        audioStatusLabel->setText("Muted", juce::dontSendNotification);
        audioStatusLabel->setColour(juce::Label::textColourId, juce::Colours::orange);
    }
    else
    {
        audioStatusLabel->setText("No Audio", juce::dontSendNotification);
        audioStatusLabel->setColour(juce::Label::textColourId, juce::Colours::red);
    }
}

void FLCollabEditor::addLogMessage(const juce::String& message)
{
    juce::String timestamp = juce::Time::getCurrentTime().toString(true, true, false);
    juce::String logLine = "[" + timestamp + "] " + message + "\n";
    
    logTextEditor->insertTextAtCaret(logLine);
    logTextEditor->moveCaretToEnd();
}

void FLCollabEditor::connectToServer()
{
    juce::String serverAddress = serverAddressEditor->getText();
    juce::String port = portEditor->getText();
    
    addLogMessage("Attempting to connect to " + serverAddress + ":" + port);
    
    // Here you would implement the actual connection logic
    // For now, we'll just update the UI and let the audio processor handle it
    audioProcessor.setServerAddress(serverAddress, port.getIntValue());
}

void FLCollabEditor::joinRoom()
{
    juce::String roomCode = roomCodeEditor->getText();
    if (roomCode.isEmpty())
    {
        addLogMessage("Error: Room code cannot be empty");
        return;
    }
    
    audioProcessor.setRoomCode(roomCode);
    addLogMessage("Joining room: " + roomCode);
}

void FLCollabEditor::setupDefaultValues()
{
    // Set default values from the audio processor
    roomCodeEditor->setText("fl_studio_collab_room_1");
    audioProcessor.setRoomCode("fl_studio_collab_room_1");
}