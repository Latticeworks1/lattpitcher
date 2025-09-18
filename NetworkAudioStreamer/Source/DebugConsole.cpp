#include "DebugConsole.h"

DebugConsole::DebugConsole(NetworkAudioProcessor& proc) : processor_(proc) {
    // Log display
    addAndMakeVisible(logDisplay_);
    logDisplay_.setMultiLine(true);
    logDisplay_.setReadOnly(true);
    logDisplay_.setScrollbarsShown(true);
    logDisplay_.setCaretVisible(false);
    logDisplay_.setPopupMenuEnabled(false);
    logDisplay_.setFont(FontOptions{Font::getDefaultMonospacedFontName(), 12.0f, Font::plain});
    logDisplay_.setColour(TextEditor::backgroundColourId, ModernTheme::cardBackground);
    logDisplay_.setColour(TextEditor::textColourId, ModernTheme::accentGreen);
    logDisplay_.setColour(TextEditor::outlineColourId, ModernTheme::borderColor);
    
    // Control buttons
    addAndMakeVisible(clearButton_);
    clearButton_.setButtonText("Clear");
    clearButton_.onClick = [this] { clearLog(); };
    
    addAndMakeVisible(exportButton_);
    exportButton_.setButtonText("Export");
    exportButton_.onClick = [this] { exportLogs(); };
    
    addAndMakeVisible(pauseButton_);
    pauseButton_.setButtonText("Pause");
    pauseButton_.setToggleable(true);
    pauseButton_.onClick = [this] { 
        isPaused_ = pauseButton_.getToggleState();
        pauseButton_.setButtonText(isPaused_ ? "Resume" : "Pause");
    };
    
    // Search functionality
    addAndMakeVisible(searchLabel_);
    searchLabel_.setText("Search:", dontSendNotification);
    searchLabel_.setFont(FontOptions{10.0f, Font::plain});
    
    addAndMakeVisible(searchBox_);
    searchBox_.setTextToShowWhenEmpty("Search logs...", Colours::grey);
    searchBox_.onTextChange = [this] {
        currentSearchTerm_ = searchBox_.getText();
        updateLogFilter();
    };
    
    // Log level filter
    addAndMakeVisible(logLevelFilter_);
    logLevelFilter_.addItem("All", 1);
    logLevelFilter_.addItem("Error Only", 2);
    logLevelFilter_.addItem("Warning+", 3);
    logLevelFilter_.addItem("Info+", 4);
    logLevelFilter_.addItem("Debug+", 5);
    logLevelFilter_.setSelectedId(1);
    logLevelFilter_.onChange = [this] {
        selectedLogLevel_ = logLevelFilter_.getSelectedId();
        updateLogFilter();
    };
    
    // Stats label
    addAndMakeVisible(statsLabel_);
    statsLabel_.setFont(FontOptions{Font::getDefaultMonospacedFontName(), 11.0f, Font::plain});
    statsLabel_.setColour(Label::backgroundColourId, Colour(0xff2a2a2a));
    statsLabel_.setColour(Label::textColourId, Colour(0xff00ffff));
    statsLabel_.setJustificationType(Justification::topLeft);
    
    // Advanced visualization components
    addAndMakeVisible(performanceGraph_);
    performanceGraph_.setPaintingIsUnclipped(true);
    
    // Spectrum analyzer
    spectrumAnalyzer_ = std::make_unique<SpectrumAnalyzer>();
    addAndMakeVisible(*spectrumAnalyzer_);
    
    // Network topology visualization
    addAndMakeVisible(networkTopology_);
    networkTopology_.setPaintingIsUnclipped(true);
    
    // Advanced metrics panel
    addAndMakeVisible(advancedMetrics_);
    advancedMetrics_.setPaintingIsUnclipped(true);
    
    // Initialize history arrays
    cpuHistory_.reserve(200);
    latencyHistory_.reserve(200);
    glitchHistory_.reserve(200);
    
    startTimer(100); // Update every 100ms
}

DebugConsole::~DebugConsole() {
    componentBeingDeleted = true;
    stopTimer();
    
    // Clear the log safely
    const ScopedLock lock(logLock_);
    logEntries_.clear();
}

void DebugConsole::paint(Graphics& g) {
    // Modern dark gradient background
    g.fillAll(ModernTheme::backgroundDark);
    
    // Subtle gradient overlay
    ColourGradient gradient(Colour(0x20ffffff), 0, 0, Colour(0x05ffffff), 0, getHeight(), false);
    g.setGradientFill(gradient);
    g.fillAll();
    
    // Draw modern card backgrounds with rounded corners
    g.setColour(ModernTheme::cardBackground);
    g.fillRoundedRectangle(logDisplay_.getBounds().toFloat().expanded(5), 8.0f);
    g.fillRoundedRectangle(statsLabel_.getBounds().toFloat().expanded(5), 8.0f);
    g.fillRoundedRectangle(performanceGraph_.getBounds().toFloat().expanded(5), 8.0f);
    
    // Modern border outlines
    g.setColour(ModernTheme::borderColor);
    g.drawRoundedRectangle(logDisplay_.getBounds().toFloat().expanded(5), 8.0f, 1.0f);
    g.drawRoundedRectangle(statsLabel_.getBounds().toFloat().expanded(5), 8.0f, 1.0f);
    g.drawRoundedRectangle(performanceGraph_.getBounds().toFloat().expanded(5), 8.0f, 1.0f);
    
    // Section titles with modern typography
    g.setColour(ModernTheme::textSecondary);
    g.setFont(FontOptions{14.0f, Font::bold});
    g.drawText("REAL-TIME LOGS", logDisplay_.getBounds().getX(), logDisplay_.getBounds().getY() - 25, 200, 20, Justification::left);
    g.drawText("SYSTEM METRICS", statsLabel_.getBounds().getX(), statsLabel_.getBounds().getY() - 25, 200, 20, Justification::left);
    g.drawText("PERFORMANCE GRAPHS", performanceGraph_.getBounds().getX(), performanceGraph_.getBounds().getY() - 25, 200, 20, Justification::left);
    
    // Draw the enhanced performance graph
    drawPerformanceGraph(g, performanceGraph_.getBounds());
    
    // Draw network topology
    drawNetworkTopology(g, networkTopology_.getBounds());
    
    // Draw advanced metrics
    drawAdvancedMetrics(g, advancedMetrics_.getBounds());
}

void DebugConsole::resized() {
    auto area = getLocalBounds();
    
    // Top controls row
    auto topArea = area.removeFromTop(35);
    topArea.removeFromTop(5);
    
    clearButton_.setBounds(topArea.removeFromLeft(60));
    topArea.removeFromLeft(5);
    exportButton_.setBounds(topArea.removeFromLeft(60));
    topArea.removeFromLeft(5);
    pauseButton_.setBounds(topArea.removeFromLeft(60));
    topArea.removeFromLeft(15);
    
    logLevelFilter_.setBounds(topArea.removeFromLeft(100));
    topArea.removeFromLeft(10);
    
    searchLabel_.setBounds(topArea.removeFromLeft(50));
    topArea.removeFromLeft(5);
    searchBox_.setBounds(topArea.removeFromLeft(150));
    
    area.removeFromTop(35); // Space for section titles
    
    // Split into main sections
    auto leftPanel = area.removeFromLeft(area.getWidth() * 0.4f); // 40% for logs
    area.removeFromLeft(10); // Gap
    auto rightPanel = area; // 60% for visualizations
    
    // Left panel: Log display
    logDisplay_.setBounds(leftPanel.reduced(5));
    
    // Right panel: Split into visualization areas
    auto topRight = rightPanel.removeFromTop(rightPanel.getHeight() * 0.33f);
    rightPanel.removeFromTop(10);
    auto middleRight = rightPanel.removeFromTop(rightPanel.getHeight() * 0.5f);
    rightPanel.removeFromTop(10);
    auto bottomRight = rightPanel;
    
    // Top right: Stats and spectrum analyzer side by side
    auto statsArea = topRight.removeFromLeft(topRight.getWidth() * 0.5f);
    topRight.removeFromLeft(5);
    auto spectrumArea = topRight;
    
    statsLabel_.setBounds(statsArea.reduced(5));
    if (spectrumAnalyzer_) {
        spectrumAnalyzer_->setBounds(spectrumArea.reduced(5));
    }
    
    // Middle right: Performance graph
    performanceGraph_.setBounds(middleRight.reduced(5));
    
    // Bottom right: Network topology and advanced metrics
    auto networkArea = bottomRight.removeFromLeft(bottomRight.getWidth() * 0.5f);
    bottomRight.removeFromLeft(5);
    auto metricsArea = bottomRight;
    
    networkTopology_.setBounds(networkArea.reduced(5));
    advancedMetrics_.setBounds(metricsArea.reduced(5));
}

void DebugConsole::timerCallback() {
    updateStats();
}

void DebugConsole::processAudioForAnalysis(const AudioBuffer<float>& buffer) {
    if (spectrumAnalyzer_ && !componentBeingDeleted.load()) {
        spectrumAnalyzer_->processAudioData(buffer);
    }
}

void DebugConsole::addLogEntry(const String& level, const String& message) {
    if (isPaused_) return;
    
    const ScopedLock lock(logLock_);
    
    String timestamp = Time::getCurrentTime().toString(true, true, true, true);
    String logEntry = "[" + timestamp + "] " + level + ": " + message;
    
    logEntries_.push_back(logEntry);
    if (logEntries_.size() > maxLogEntries_) {
        logEntries_.pop_front();
    }
    
    updateLogFilter();
}

void DebugConsole::clearLog() {
    const ScopedLock lock(logLock_);
    logEntries_.clear();
    filteredEntries_.clear();
    
    MessageManager::callAsync([this]() {
        logDisplay_.clear();
    });
}

// TODO: Extract these massive visualization methods into separate components
void DebugConsole::updateStats() {
    // Stub implementation - needs to be extracted to PerformanceMonitor component
}

void DebugConsole::updateLogFilter() {
    // Stub implementation - needs log filtering logic
}

void DebugConsole::exportLogs() {
    // Stub implementation - needs export functionality
}

void DebugConsole::drawPerformanceGraph(Graphics& g, Rectangle<int> area) {
    // Stub implementation - needs to be extracted to PerformanceGraph component
}

void DebugConsole::drawNetworkTopology(Graphics& g, Rectangle<int> area) {
    // Stub implementation - needs to be extracted to NetworkTopology component  
}

void DebugConsole::drawAdvancedMetrics(Graphics& g, Rectangle<int> area) {
    // Stub implementation - needs to be extracted to AdvancedMetrics component
}

bool DebugConsole::matchesFilter(const String& logEntry, const String& searchTerm, int logLevel) {
    // Stub implementation - needs filtering logic
    return true;
}

void DebugConsole::checkForCriticalIssues(const AudioStats& stats) {
    // Stub implementation - needs alert system
}