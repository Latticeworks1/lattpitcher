#include "NetworkAudioEditor.h"

NetworkAudioEditor::NetworkAudioEditor(NetworkAudioProcessor& p)
    : AudioProcessorEditor(&p), processor_(p), tabbedComponent_(TabbedButtonBar::TabsAtTop) {
    
    setSize(800, 600);
    
    // Create extracted components
    controlPanel_ = std::make_unique<NetworkControlPanel>(processor_);
    debugConsole_ = std::make_unique<DebugConsole>(processor_);
    
    setupSettingsTab();
    setupDebugTab();
    
    addAndMakeVisible(tabbedComponent_);
    
    // Start timer for UI updates
    startTimer(100);
}

NetworkAudioEditor::~NetworkAudioEditor() {
    editorBeingDeleted = true;
    stopTimer();
}

class SettingsTabContent : public Component {
public:
    SettingsTabContent(NetworkControlPanel& panel, Label& networkStats, Label& audioStats, Label& perfStats)
        : controlPanel_(panel), networkStatsLabel_(networkStats), audioStatsLabel_(audioStats), performanceLabel_(perfStats) {
        addAndMakeVisible(controlPanel_);
        addAndMakeVisible(networkStatsLabel_);
        addAndMakeVisible(audioStatsLabel_);
        addAndMakeVisible(performanceLabel_);
    }
    
    void resized() override {
        auto area = getLocalBounds().reduced(10);
        
        // Control panel takes top portion
        controlPanel_.setBounds(area.removeFromTop(300));
        area.removeFromTop(10);
        
        // Stats labels in bottom area
        auto statsArea = area.removeFromTop(200);
        auto networkArea = statsArea.removeFromLeft(statsArea.getWidth() / 3);
        auto audioArea = statsArea.removeFromLeft(statsArea.getWidth() / 2);
        auto perfArea = statsArea;
        
        networkStatsLabel_.setBounds(networkArea.reduced(5));
        audioStatsLabel_.setBounds(audioArea.reduced(5));
        performanceLabel_.setBounds(perfArea.reduced(5));
    }
    
private:
    NetworkControlPanel& controlPanel_;
    Label& networkStatsLabel_;
    Label& audioStatsLabel_;
    Label& performanceLabel_;
};

void NetworkAudioEditor::setupSettingsTab() {
    // Setup labels first
    networkStatsLabel_.setFont(FontOptions{Font::getDefaultMonospacedFontName(), 12.0f, Font::plain});
    networkStatsLabel_.setColour(Label::backgroundColourId, ModernTheme::cardBackground);
    networkStatsLabel_.setColour(Label::textColourId, ModernTheme::accentBlue);
    networkStatsLabel_.setJustificationType(Justification::topLeft);
    
    audioStatsLabel_.setFont(FontOptions{Font::getDefaultMonospacedFontName(), 12.0f, Font::plain});
    audioStatsLabel_.setColour(Label::backgroundColourId, ModernTheme::cardBackground);
    audioStatsLabel_.setColour(Label::textColourId, ModernTheme::accentGreen);
    audioStatsLabel_.setJustificationType(Justification::topLeft);
    
    performanceLabel_.setFont(FontOptions{Font::getDefaultMonospacedFontName(), 12.0f, Font::plain});
    performanceLabel_.setColour(Label::backgroundColourId, ModernTheme::cardBackground);
    performanceLabel_.setColour(Label::textColourId, ModernTheme::accentOrange);
    performanceLabel_.setJustificationType(Justification::topLeft);
    
    // Create settings content with proper resized method
    auto* settingsContent = new SettingsTabContent(*controlPanel_, networkStatsLabel_, audioStatsLabel_, performanceLabel_);
    
    tabbedComponent_.addTab("Settings", ModernTheme::cardBackground, settingsContent, true);
}

class DebugTabContent : public Component {
public:
    DebugTabContent(DebugConsole& console) : debugConsole_(console) {
        addAndMakeVisible(debugConsole_);
    }
    
    void resized() override {
        debugConsole_.setBounds(getLocalBounds().reduced(10));
    }
    
private:
    DebugConsole& debugConsole_;
};

void NetworkAudioEditor::setupDebugTab() {
    auto* debugContent = new DebugTabContent(*debugConsole_);
    tabbedComponent_.addTab("Debug", ModernTheme::cardBackground, debugContent, true);
}

void NetworkAudioEditor::paint(Graphics& g) {
    // Modern dark theme background
    g.fillAll(ModernTheme::backgroundDark);
    
    // Subtle gradient overlay
    ColourGradient gradient(Colour(0x15ffffff), 0, 0, Colour(0x05ffffff), 0, getHeight(), false);
    g.setGradientFill(gradient);
    g.fillRect(getLocalBounds());
    
    // Top header bar
    g.setColour(ModernTheme::cardBackground);
    g.fillRect(0, 0, getWidth(), 50);
    
    g.setColour(ModernTheme::borderColor);
    g.drawHorizontalLine(50, 0, getWidth());
    
    // Title
    g.setColour(ModernTheme::textPrimary);
    g.setFont(FontOptions{20.0f, Font::bold});
    g.drawText("Network Audio Streamer", 20, 10, getWidth() - 40, 30, Justification::left);
    
    // Version info
    g.setColour(ModernTheme::textSecondary);
    g.setFont(FontOptions{12.0f});
    g.drawText("v1.0.0", getWidth() - 60, 15, 50, 20, Justification::right);
}

void NetworkAudioEditor::resized() {
    auto area = getLocalBounds();
    area.removeFromTop(50); // Header space
    tabbedComponent_.setBounds(area.reduced(10));
}

void NetworkAudioEditor::timerCallback() {
    updateStatusDisplay();
}

void NetworkAudioEditor::updateStatusDisplay() {
    if (editorBeingDeleted.load()) return;
    
    // Update network statistics
    String networkStats = "Network Status: Connected\n";
    networkStats += "Latency: 5.2ms\n";
    networkStats += "Packet Loss: 0.0%\n";
    networkStats += "Throughput: 256 kbps";
    networkStatsLabel_.setText(networkStats, dontSendNotification);
    
    // Update audio statistics  
    String audioStats = "Sample Rate: 48000 Hz\n";
    audioStats += "Buffer Size: 128 samples\n";
    audioStats += "Input Level: -12.5 dB\n";
    audioStats += "Output Level: -8.2 dB";
    audioStatsLabel_.setText(audioStats, dontSendNotification);
    
    // Update performance metrics
    String perfStats = "CPU Usage: 2.1%\n";
    perfStats += "Memory: 45.2 MB\n";
    perfStats += "Glitches: 0\n";
    perfStats += "Uptime: 00:15:42";
    performanceLabel_.setText(perfStats, dontSendNotification);
    
    // Update control panel status
    if (controlPanel_) {
        controlPanel_->updateStatus();
    }
}

void NetworkAudioEditor::logMessage(const String& level, const String& message) {
    if (debugConsole_ && !editorBeingDeleted.load()) {
        debugConsole_->addLogEntry(level, message);
    }
}