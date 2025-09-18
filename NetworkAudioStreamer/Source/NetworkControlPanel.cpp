#include "NetworkControlPanel.h"

NetworkControlPanel::NetworkControlPanel(NetworkAudioProcessor& processor) : processor_(processor) {
    setupControls();
}

NetworkControlPanel::~NetworkControlPanel() {
}

void NetworkControlPanel::setupControls() {
    // Mode selection
    addAndMakeVisible(modeLabel_);
    modeLabel_.setText("Mode:", dontSendNotification);
    modeLabel_.setFont(Font(14.0f));
    modeLabel_.setColour(Label::textColourId, ModernTheme::textPrimary);
    
    addAndMakeVisible(modeBox_);
    modeBox_.addItem("Inactive", 1);
    modeBox_.addItem("Server", 2);
    modeBox_.addItem("Client", 3);
    modeBox_.setSelectedId(1);
    modeBox_.setColour(ComboBox::backgroundColourId, ModernTheme::cardBackground);
    modeBox_.setColour(ComboBox::textColourId, ModernTheme::textPrimary);
    modeBox_.setColour(ComboBox::outlineColourId, ModernTheme::borderColor);
    
    // Port configuration
    addAndMakeVisible(portLabel_);
    portLabel_.setText("Port:", dontSendNotification);
    portLabel_.setFont(Font(14.0f));
    portLabel_.setColour(Label::textColourId, ModernTheme::textPrimary);
    
    addAndMakeVisible(portSlider_);
    portSlider_.setRange(1024, 65535, 1);
    portSlider_.setValue(9001);
    portSlider_.setSliderStyle(Slider::IncDecButtons);
    portSlider_.setTextBoxStyle(Slider::TextBoxLeft, false, 80, 20);
    portSlider_.setColour(Slider::backgroundColourId, ModernTheme::cardBackground);
    portSlider_.setColour(Slider::textBoxTextColourId, ModernTheme::textPrimary);
    
    // Address configuration (for client mode)
    addAndMakeVisible(addressLabel_);
    addressLabel_.setText("Server Address:", dontSendNotification);
    addressLabel_.setFont(Font(14.0f));
    addressLabel_.setColour(Label::textColourId, ModernTheme::textPrimary);
    
    addAndMakeVisible(addressEditor_);
    addressEditor_.setMultiLine(false);
    addressEditor_.setReturnKeyStartsNewLine(false);
    addressEditor_.setReadOnly(false);
    addressEditor_.setScrollbarsShown(false);
    addressEditor_.setCaretVisible(true);
    addressEditor_.setPopupMenuEnabled(true);
    addressEditor_.setText("127.0.0.1");
    addressEditor_.setColour(TextEditor::backgroundColourId, ModernTheme::cardBackground);
    addressEditor_.setColour(TextEditor::textColourId, ModernTheme::textPrimary);
    addressEditor_.setColour(TextEditor::outlineColourId, ModernTheme::borderColor);
    
    // Session ID
    addAndMakeVisible(sessionLabel_);
    sessionLabel_.setText("Session ID:", dontSendNotification);
    sessionLabel_.setFont(Font(14.0f));
    sessionLabel_.setColour(Label::textColourId, ModernTheme::textPrimary);
    
    addAndMakeVisible(sessionSlider_);
    sessionSlider_.setRange(1, 999999, 1);
    sessionSlider_.setValue(12345);
    sessionSlider_.setSliderStyle(Slider::IncDecButtons);
    sessionSlider_.setTextBoxStyle(Slider::TextBoxLeft, false, 80, 20);
    sessionSlider_.setColour(Slider::backgroundColourId, ModernTheme::cardBackground);
    sessionSlider_.setColour(Slider::textBoxTextColourId, ModernTheme::textPrimary);
    
    // User ID
    addAndMakeVisible(userLabel_);
    userLabel_.setText("User ID:", dontSendNotification);
    userLabel_.setFont(Font(14.0f));
    userLabel_.setColour(Label::textColourId, ModernTheme::textPrimary);
    
    addAndMakeVisible(userSlider_);
    userSlider_.setRange(1, 99, 1);
    userSlider_.setValue(1);
    userSlider_.setSliderStyle(Slider::IncDecButtons);
    userSlider_.setTextBoxStyle(Slider::TextBoxLeft, false, 60, 20);
    userSlider_.setColour(Slider::backgroundColourId, ModernTheme::cardBackground);
    userSlider_.setColour(Slider::textBoxTextColourId, ModernTheme::textPrimary);
    
    // Status display
    addAndMakeVisible(statusLabel_);
    statusLabel_.setText("Status: Disconnected", dontSendNotification);
    statusLabel_.setFont(Font(16.0f, Font::bold));
    statusLabel_.setColour(Label::textColourId, ModernTheme::accentRed);
    statusLabel_.setJustificationType(Justification::centred);
    
    // Control buttons
    addAndMakeVisible(connectButton_);
    connectButton_.setButtonText("Connect");
    connectButton_.setColour(TextButton::buttonColourId, ModernTheme::accentBlue);
    connectButton_.setColour(TextButton::textColourOffId, ModernTheme::textPrimary);
    connectButton_.onClick = [this] { connectButtonClicked(); };
    
    addAndMakeVisible(resetStatsButton_);
    resetStatsButton_.setButtonText("Reset Stats");
    resetStatsButton_.setColour(TextButton::buttonColourId, ModernTheme::accentOrange);
    resetStatsButton_.setColour(TextButton::textColourOffId, ModernTheme::textPrimary);
    resetStatsButton_.onClick = [this] { resetStatsButtonClicked(); };
}

void NetworkControlPanel::paint(Graphics& g) {
    // Modern card background
    g.fillAll(ModernTheme::backgroundDark);
    
    g.setColour(ModernTheme::cardBackground);
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 12.0f);
    
    // Border
    g.setColour(ModernTheme::borderColor);
    g.drawRoundedRectangle(getLocalBounds().toFloat(), 12.0f, 2.0f);
    
    // Title
    g.setColour(ModernTheme::textSecondary);
    g.setFont(Font(18.0f, Font::bold));
    g.drawText("NETWORK SETTINGS", 20, 10, getWidth() - 40, 30, Justification::centred);
}

void NetworkControlPanel::resized() {
    auto area = getLocalBounds().reduced(20);
    area.removeFromTop(40); // Space for title
    
    int rowHeight = 35;
    int labelWidth = 120;
    int controlWidth = 150;
    
    // Mode selection
    auto modeRow = area.removeFromTop(rowHeight);
    modeLabel_.setBounds(modeRow.removeFromLeft(labelWidth));
    modeBox_.setBounds(modeRow.removeFromLeft(controlWidth));
    
    // Port configuration
    auto portRow = area.removeFromTop(rowHeight);
    portLabel_.setBounds(portRow.removeFromLeft(labelWidth));
    portSlider_.setBounds(portRow.removeFromLeft(controlWidth));
    
    // Address configuration
    auto addressRow = area.removeFromTop(rowHeight);
    addressLabel_.setBounds(addressRow.removeFromLeft(labelWidth));
    addressEditor_.setBounds(addressRow.removeFromLeft(controlWidth));
    
    // Session ID
    auto sessionRow = area.removeFromTop(rowHeight);
    sessionLabel_.setBounds(sessionRow.removeFromLeft(labelWidth));
    sessionSlider_.setBounds(sessionRow.removeFromLeft(controlWidth));
    
    // User ID
    auto userRow = area.removeFromTop(rowHeight);
    userLabel_.setBounds(userRow.removeFromLeft(labelWidth));
    userSlider_.setBounds(userRow.removeFromLeft(controlWidth));
    
    area.removeFromTop(20); // Spacing
    
    // Status and buttons
    statusLabel_.setBounds(area.removeFromTop(30));
    
    area.removeFromTop(10);
    auto buttonRow = area.removeFromTop(40);
    connectButton_.setBounds(buttonRow.removeFromLeft(120));
    buttonRow.removeFromLeft(20);
    resetStatsButton_.setBounds(buttonRow.removeFromLeft(120));
}

void NetworkControlPanel::updateStatus() {
    // Update connection status based on processor state
    // This will be connected to the actual NetworkManager
    statusLabel_.setText("Status: Connected", dontSendNotification);
    statusLabel_.setColour(Label::textColourId, ModernTheme::accentGreen);
}

void NetworkControlPanel::connectButtonClicked() {
    // TODO: Connect to NetworkAudioProcessor networking
    connectButton_.setButtonText("Disconnect");
    updateStatus();
}

void NetworkControlPanel::resetStatsButtonClicked() {
    // TODO: Reset statistics in processor
}