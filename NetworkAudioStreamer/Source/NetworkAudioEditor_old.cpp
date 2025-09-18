#include "NetworkAudioEditor.h"

//==============================================================================
// Modern Theme Colors
const Colour DebugConsole::ModernTheme::backgroundDark = Colour(0xff0a0a0a);
const Colour DebugConsole::ModernTheme::cardBackground = Colour(0xff1a1a1a);
const Colour DebugConsole::ModernTheme::accentBlue = Colour(0xff00d4ff);
const Colour DebugConsole::ModernTheme::accentGreen = Colour(0xff00ff88);
const Colour DebugConsole::ModernTheme::accentRed = Colour(0xffff4444);
const Colour DebugConsole::ModernTheme::accentOrange = Colour(0xffff8800);
const Colour DebugConsole::ModernTheme::textPrimary = Colour(0xffffffff);
const Colour DebugConsole::ModernTheme::textSecondary = Colour(0xffaaaaaa);
const Colour DebugConsole::ModernTheme::borderColor = Colour(0xff333333);

//==============================================================================
SpectrumAnalyzer::SpectrumAnalyzer()
    : forwardFFT(fftOrder), window(fftSize, dsp::WindowingFunction<float>::hann) {
    setOpaque(false);
    startTimer(60); // 60 FPS for smooth spectrum
}

SpectrumAnalyzer::~SpectrumAnalyzer() {
    stopTimer();
}

void SpectrumAnalyzer::paint(Graphics& g) {
    g.fillAll(Colour(0xff0a0a0a));
    
    // Draw spectrum background
    g.setColour(Colour(0xff1a1a1a));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 8.0f);
    
    // Draw frequency grid
    g.setColour(Colour(0xff333333));
    auto bounds = getLocalBounds().reduced(10);
    
    // Vertical frequency lines (1kHz, 2kHz, 5kHz, 10kHz, 20kHz)
    const float freqs[] = {1000, 2000, 5000, 10000, 20000};
    for (auto freq : freqs) {
        float x = bounds.getX() + bounds.getWidth() * jmap(std::log10(freq), std::log10(20.0f), std::log10(20000.0f), 0.0f, 1.0f);
        g.drawVerticalLine(x, bounds.getY(), bounds.getBottom());
    }
    
    // Draw spectrum data
    if (nextFFTBlockReady) {
        g.setColour(Colour(0xff00d4ff)); // Modern blue
        
        Path spectrumPath;
        auto width = bounds.getWidth();
        auto height = bounds.getHeight();
        
        for (int i = 1; i < fftSize / 2; ++i) {
            auto skewedProportionX = 1.0f - std::exp(std::log(1.0f - (float)i / (float)(fftSize / 2)) * 0.2f);
            auto fftX = jlimit(0.0f, 1.0f, skewedProportionX);
            auto level = jmap(jlimit(0.0f, 1.0f, jmap(20.0f * std::log10(scopeData[i]), -100.0f, 0.0f, 0.0f, 1.0f)), 0.0f, 1.0f, (float)height, 0.0f);
            
            if (i == 1) spectrumPath.startNewSubPath(bounds.getX() + fftX * width, bounds.getY() + level);
            else spectrumPath.lineTo(bounds.getX() + fftX * width, bounds.getY() + level);
        }
        
        g.strokePath(spectrumPath, PathStrokeType(2.0f));
        
        // Add glow effect
        g.setColour(Colour(0x3300d4ff));
        g.strokePath(spectrumPath, PathStrokeType(8.0f));
    }
    
    // Draw labels
    g.setColour(Colour(0xffaaaaaa));
    g.setFont(Font(12.0f));
    g.drawText("SPECTRUM ANALYZER", bounds.getX(), bounds.getY() - 20, 200, 20, Justification::left);
    g.drawText("20Hz", bounds.getX(), bounds.getBottom() + 5, 50, 15, Justification::left);
    g.drawText("20kHz", bounds.getRight() - 50, bounds.getBottom() + 5, 50, 15, Justification::right);
}

void SpectrumAnalyzer::timerCallback() {
    if (nextFFTBlockReady) {
        drawNextFrameOfSpectrum();
        nextFFTBlockReady = false;
        repaint();
    }
}

void SpectrumAnalyzer::processAudioData(const AudioBuffer<float>& buffer) {
    if (buffer.getNumChannels() > 0) {
        auto* channelData = buffer.getReadPointer(0);
        
        for (auto i = 0; i < buffer.getNumSamples(); ++i)
            pushNextSampleIntoFifo(channelData[i]);
    }
}

void SpectrumAnalyzer::pushNextSampleIntoFifo(float sample) noexcept {
    if (fifoIndex == fftSize) {
        if (!nextFFTBlockReady) {
            zeromem(fftData, sizeof(fftData));
            memcpy(fftData, fifo, sizeof(fifo));
            nextFFTBlockReady = true;
        }
        fifoIndex = 0;
    }
    
    fifo[fifoIndex++] = sample;
}

void SpectrumAnalyzer::drawNextFrameOfSpectrum() {
    window.multiplyWithWindowingTable(fftData, fftSize);
    forwardFFT.performFrequencyOnlyForwardTransform(fftData);
    
    auto mindB = -100.0f;
    auto maxdB = 0.0f;
    
    for (int i = 0; i < fftSize / 2; ++i) {
        auto skewedProportionX = 1.0f - std::exp(std::log(1.0f - (float)i / (float)(fftSize / 2)) * 0.2f);
        auto fftDataIndex = jlimit(0, fftSize / 2 - 1, (int)(skewedProportionX * (fftSize / 2)));
        auto level = jmap(jlimit(mindB, maxdB, 20.0f * std::log10(fftData[fftDataIndex])), mindB, maxdB, 0.0f, 1.0f);
        
        scopeData[i] = level;
    }
}

void SpectrumAnalyzer::resized() {
    // Spectrum analyzer handles its own layout
}

//==============================================================================
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
    g.setFont(Font(14.0f, Font::bold));
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
    if (!isValidComponent() || !MessageManager::getInstance()->isThisTheMessageThread()) {
        // If called from audio thread or component is invalid, defer to message thread
        if (MessageManager::getInstance()) {
            MessageManager::callAsync([this, level, message] {
                addLogEntry(level, message);
            });
        }
        return;
    }
    
    if (isPaused_) {
        return; // Don't add entries when paused
    }
    
    const ScopedLock lock(logLock_);
    
    auto timestamp = Time::getCurrentTime().toString(false, true, true, true);
    String entry = "[" + timestamp + "] [" + level + "] " + message + "\n";
    
    logEntries_.push_back(entry);
    
    if (logEntries_.size() > maxLogEntries_) {
        logEntries_.pop_front();
    }
    
    // Update filtered entries and display
    updateLogFilter();
}

void DebugConsole::updateLogFilter() {
    if (componentBeingDeleted.load()) return;
    
    const ScopedLock lock(logLock_);
    filteredEntries_.clear();
    
    for (const auto& entry : logEntries_) {
        if (matchesFilter(entry, currentSearchTerm_, selectedLogLevel_)) {
            filteredEntries_.push_back(entry);
        }
    }
    
    // Update display
    String displayText;
    for (const auto& entry : filteredEntries_) {
        displayText += entry;
    }
    
    if (!componentBeingDeleted.load() && logDisplay_.isVisible()) {
        logDisplay_.setText(displayText);
        logDisplay_.moveCaretToEnd();
    }
}

bool DebugConsole::matchesFilter(const String& logEntry, const String& searchTerm, int logLevel) {
    // Check search term
    if (searchTerm.isNotEmpty() && !logEntry.containsIgnoreCase(searchTerm)) {
        return false;
    }
    
    // Check log level filter
    switch (logLevel) {
        case 2: // Error only
            return logEntry.contains("[ERROR]");
        case 3: // Warning+
            return logEntry.contains("[ERROR]") || logEntry.contains("[WARNING]");
        case 4: // Info+
            return logEntry.contains("[ERROR]") || logEntry.contains("[WARNING]") || logEntry.contains("[INFO]");
        case 5: // Debug+ (all)
            return true;
        default: // All
            return true;
    }
}

void DebugConsole::exportLogs() {
    const ScopedLock lock(logLock_);
    
    String allLogs;
    for (const auto& entry : logEntries_) {
        allLogs += entry;
    }
    
    // Create filename with timestamp
    auto now = Time::getCurrentTime();
    String filename = "NetworkAudioStreamer_Debug_" + 
                     String(now.getYear()) + 
                     String(now.getMonth() + 1).paddedLeft('0', 2) +
                     String(now.getDayOfMonth()).paddedLeft('0', 2) + "_" +
                     String(now.getHours()).paddedLeft('0', 2) +
                     String(now.getMinutes()).paddedLeft('0', 2) + ".txt";
    
    // Simple direct file export to desktop
    File logFile = File::getSpecialLocation(File::userDesktopDirectory).getChildFile(filename);
    
    if (logFile.replaceWithText(allLogs)) {
        addLogEntry("INFO", "Debug log exported to: " + logFile.getFullPathName());
    } else {
        addLogEntry("ERROR", "Failed to export debug log to: " + logFile.getFullPathName());
    }
}

void DebugConsole::clearLog() {
    const ScopedLock lock(logLock_);
    logEntries_.clear();
    logDisplay_.clear();
}

void DebugConsole::updateStats() {
    if (componentBeingDeleted.load() || !isValidComponent()) {
        return;
    }
    
    auto* monitor = processor_.getAudioMonitor();
    if (!monitor) {
        return;
    }
    
    try {
        const auto& stats = monitor->getStats();
        
        // Update performance history for graphing
        float cpuUsage = static_cast<float>(stats.cpuUsage.load());
        float latency = static_cast<float>(stats.avgLatency.load());
        int glitchCount = stats.glitchCount.load();
        
        cpuHistory_.push_back(cpuUsage);
        latencyHistory_.push_back(latency);
        glitchHistory_.push_back(glitchCount);
        
        // Keep only last 200 points (about 20 seconds at 100ms updates)
        if (cpuHistory_.size() > 200) {
            cpuHistory_.erase(cpuHistory_.begin());
            latencyHistory_.erase(latencyHistory_.begin());
            glitchHistory_.erase(glitchHistory_.begin());
        }
        
        String statsText = "=== REAL-TIME STATS ===\n\n";
        
        // Network Stats
        statsText += "NETWORK:\n";
        statsText += "• Packets Sent: " + String(stats.packetsSent.load()) + "\n";
        statsText += "• Packets Received: " + String(stats.packetsReceived.load()) + "\n";
        statsText += "• Packets Dropped: " + String(stats.packetsDropped.load()) + "\n";
        statsText += "• Avg Latency: " + String(stats.avgLatency.load(), 2) + "ms\n\n";
        
        // Buffer Health
        statsText += "BUFFER HEALTH:\n";
        statsText += "• Underruns: " + String(stats.bufferUnderruns.load()) + "\n";
        statsText += "• Overruns: " + String(stats.bufferOverruns.load()) + "\n";
        statsText += "• Total Glitches: " + String(stats.glitchCount.load()) + "\n\n";
        
        // Performance
        statsText += "PERFORMANCE:\n";
        statsText += "• CPU Usage: " + String(stats.cpuUsage.load() * 100, 1) + "%\n";
        statsText += "• Process Time: " + String(stats.processTimeMs.load(), 3) + "ms\n\n";
        
        // Audio Levels
        double inputDb = stats.inputLevel.load() > 0.0 ? Decibels::gainToDecibels(stats.inputLevel.load()) : -100.0;
        double outputDb = stats.outputLevel.load() > 0.0 ? Decibels::gainToDecibels(stats.outputLevel.load()) : -100.0;
        statsText += "AUDIO LEVELS:\n";
        statsText += "• Input: " + String(inputDb, 1) + " dB\n";
        statsText += "• Output: " + String(outputDb, 1) + " dB\n\n";
        
        // Recent Glitches
        auto glitches = monitor->getRecentGlitches(5);
        if (!glitches.empty()) {
            statsText += "RECENT ISSUES:\n";
            for (const auto& glitch : glitches) {
                uint64_t ageMs = Time::getMillisecondCounter() - glitch.timestamp;
                String ageStr = ageMs < 1000 ? String(ageMs) + "ms" : String(ageMs / 1000.0, 1) + "s";
                statsText += "• " + glitch.type + " (" + ageStr + " ago)\n";
            }
        } else {
            statsText += "RECENT ISSUES:\n• No issues detected ✓\n";
        }
        
        if (!componentBeingDeleted.load() && statsLabel_.isVisible()) {
            statsLabel_.setText(statsText, dontSendNotification);
        }
        
        // Check for critical issues and generate smart alerts
        checkForCriticalIssues(stats);
        
        // Trigger graph repaint
        if (!componentBeingDeleted.load() && performanceGraph_.isVisible()) {
            performanceGraph_.repaint();
        }
        
    } catch (...) {
        // Silently handle any exceptions to prevent crashes
        DBG("Exception in DebugConsole::updateStats");
    }
}

void DebugConsole::drawPerformanceGraph(Graphics& g, Rectangle<int> area) {
    if (cpuHistory_.empty()) return;
    
    g.fillAll(Colour(0xff1a1a1a));
    g.setColour(Colour(0xff333333));
    g.drawRect(area, 1);
    
    auto graphArea = area.reduced(10);
    
    // Draw grid
    g.setColour(Colour(0xff333333));
    for (int i = 1; i < 5; ++i) {
        int y = graphArea.getY() + (graphArea.getHeight() * i / 5);
        g.drawLine(graphArea.getX(), y, graphArea.getRight(), y);
    }
    
    // Draw CPU usage (0-100%)
    if (cpuHistory_.size() > 1) {
        g.setColour(Colour(0xff00ff00)); // Green
        Path cpuPath;
        
        for (size_t i = 0; i < cpuHistory_.size(); ++i) {
            float x = graphArea.getX() + (graphArea.getWidth() * i / static_cast<float>(cpuHistory_.size() - 1));
            float y = graphArea.getBottom() - (graphArea.getHeight() * cpuHistory_[i]);
            
            if (i == 0) cpuPath.startNewSubPath(x, y);
            else cpuPath.lineTo(x, y);
        }
        g.strokePath(cpuPath, PathStrokeType(1.5f));
    }
    
    // Draw latency (scaled to 0-50ms range)
    if (latencyHistory_.size() > 1) {
        g.setColour(Colour(0xff0080ff)); // Blue
        Path latencyPath;
        
        for (size_t i = 0; i < latencyHistory_.size(); ++i) {
            float x = graphArea.getX() + (graphArea.getWidth() * i / static_cast<float>(latencyHistory_.size() - 1));
            float normalizedLatency = jmin(1.0f, latencyHistory_[i] / 50.0f); // Scale to 50ms max
            float y = graphArea.getBottom() - (graphArea.getHeight() * normalizedLatency);
            
            if (i == 0) latencyPath.startNewSubPath(x, y);
            else latencyPath.lineTo(x, y);
        }
        g.strokePath(latencyPath, PathStrokeType(1.5f));
    }
    
    // Draw glitch indicators as red spikes
    if (!glitchHistory_.empty()) {
        g.setColour(Colour(0xffff0000)); // Red
        
        for (size_t i = 0; i < glitchHistory_.size(); ++i) {
            if (i > 0 && glitchHistory_[i] > glitchHistory_[i-1]) {
                float x = graphArea.getX() + (graphArea.getWidth() * i / static_cast<float>(glitchHistory_.size() - 1));
                g.drawLine(x, graphArea.getY(), x, graphArea.getBottom());
            }
        }
    }
    
    // Draw legend
    g.setFont(10.0f);
    int legendY = area.getY() + 5;
    
    g.setColour(Colour(0xff00ff00));
    g.drawText("CPU%", area.getX() + 10, legendY, 50, 15, Justification::left);
    
    g.setColour(Colour(0xff0080ff));
    g.drawText("Latency", area.getX() + 70, legendY, 50, 15, Justification::left);
    
    g.setColour(Colour(0xffff0000));
    g.drawText("Glitches", area.getX() + 130, legendY, 50, 15, Justification::left);
}

//==============================================================================
NetworkAudioEditor::NetworkAudioEditor(NetworkAudioProcessor& p) : AudioProcessorEditor(&p), processor_(p), 
    tabbedComponent_(TabbedButtonBar::TabsAtTop) {
    setSize(800, 600); // Larger size for debug interface
    
    // Setup tabbed component
    addAndMakeVisible(tabbedComponent_);
    tabbedComponent_.setTabBarDepth(30);
    
    setupSettingsTab();
    setupDebugTab();
    
    // Start timer for status updates
    startTimer(250);
    
    // Connect processor logging to GUI
    processor_.setLogCallback([this](const String& level, const String& message) {
        logMessage(level, message);
    });
    
    // Initial log entries
    logMessage("INFO", "NetworkAudioStreamer GUI initialized");
    logMessage("INFO", "Monitoring system active");
}

NetworkAudioEditor::~NetworkAudioEditor() {
    editorBeingDeleted = true;
    stopTimer();
    
    // Safely clear the log callback to prevent crashes
    processor_.clearLogCallback();
    
    // Clear debug console first to prevent callbacks
    debugConsole_.reset();
}

void NetworkAudioEditor::setupSettingsTab() {
    // Settings tab setup
    settingsTab_.setSize(800, 550);
    
    // Mode selection
    settingsTab_.addAndMakeVisible(modeBox_);
    modeBox_.addItem("Inactive", 1);
    modeBox_.addItem("Server", 2);
    modeBox_.addItem("Client", 3);
    comboAttachments_.push_back(std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor_.getParameters(), "mode", modeBox_));
    
    // Port
    settingsTab_.addAndMakeVisible(portSlider_);
    portSlider_.setRange(1024, 65535, 1);
    portSlider_.setValue(9001);
    portSlider_.setTextBoxStyle(Slider::TextBoxRight, false, 80, 20);
    sliderAttachments_.push_back(std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(
        processor_.getParameters(), "port", portSlider_));
    
    // Address
    settingsTab_.addAndMakeVisible(addressEditor_);
    addressEditor_.setText("127.0.0.1");
    
    // Session ID
    settingsTab_.addAndMakeVisible(sessionSlider_);
    sessionSlider_.setRange(1, 9999, 1);
    sessionSlider_.setValue(1);
    sessionSlider_.setTextBoxStyle(Slider::TextBoxRight, false, 60, 20);
    sliderAttachments_.push_back(std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(
        processor_.getParameters(), "sessionId", sessionSlider_));
    
    // User ID  
    settingsTab_.addAndMakeVisible(userSlider_);
    userSlider_.setRange(1, 9999, 1);
    userSlider_.setValue(1);
    userSlider_.setTextBoxStyle(Slider::TextBoxRight, false, 60, 20);
    sliderAttachments_.push_back(std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(
        processor_.getParameters(), "userId", userSlider_));
    
    // Status
    settingsTab_.addAndMakeVisible(statusLabel_);
    statusLabel_.setText("Network Audio Streamer - Enhanced Debug Mode", dontSendNotification);
    statusLabel_.setFont(FontOptions{16.0f, Font::bold});
    statusLabel_.setJustificationType(Justification::centred);
    
    // Connect button
    settingsTab_.addAndMakeVisible(connectButton_);
    connectButton_.setButtonText("Connect");
    connectButton_.onClick = [this] { 
        logMessage("INFO", "Connection button pressed");
        updateStatusDisplay();
    };
    
    // Reset stats button
    settingsTab_.addAndMakeVisible(resetStatsButton_);
    resetStatsButton_.setButtonText("Reset Stats");
    resetStatsButton_.onClick = [this] { 
        if (auto* monitor = processor_.getAudioMonitor()) {
            monitor->reset();
            logMessage("INFO", "Statistics reset");
        }
    };
    
    // Network stats display
    settingsTab_.addAndMakeVisible(networkStatsLabel_);
    networkStatsLabel_.setFont(FontOptions{Font::getDefaultMonospacedFontName(), 10.0f, Font::plain});
    networkStatsLabel_.setColour(Label::backgroundColourId, Colour(0xff1a1a1a));
    networkStatsLabel_.setColour(Label::textColourId, Colour(0xff00ffff));
    networkStatsLabel_.setJustificationType(Justification::topLeft);
    
    // Audio stats display  
    settingsTab_.addAndMakeVisible(audioStatsLabel_);
    audioStatsLabel_.setFont(FontOptions{Font::getDefaultMonospacedFontName(), 10.0f, Font::plain});
    audioStatsLabel_.setColour(Label::backgroundColourId, Colour(0xff1a1a1a));
    audioStatsLabel_.setColour(Label::textColourId, Colour(0xff00ff00));
    audioStatsLabel_.setJustificationType(Justification::topLeft);
    
    // Performance display
    settingsTab_.addAndMakeVisible(performanceLabel_);
    performanceLabel_.setFont(FontOptions{Font::getDefaultMonospacedFontName(), 10.0f, Font::plain});
    performanceLabel_.setColour(Label::backgroundColourId, Colour(0xff1a1a1a));
    performanceLabel_.setColour(Label::textColourId, Colour(0xfffff000));
    performanceLabel_.setJustificationType(Justification::topLeft);
    
    // Add settings tab to tabbed component
    tabbedComponent_.addTab("Settings", Colour(0xff444444), &settingsTab_, false);
    
    logMessage("INFO", "Settings tab initialized");
}

void NetworkAudioEditor::setupDebugTab() {
    debugConsole_ = std::make_unique<DebugConsole>(processor_);
    
    // Add debug tab
    tabbedComponent_.addTab("Debug Console", Colour(0xff444444), debugConsole_.get(), false);
    
    logMessage("INFO", "Debug console initialized");
}

void NetworkAudioEditor::logMessage(const String& level, const String& message) {
    if (!editorBeingDeleted.load() && debugConsole_ && debugConsole_->isValidComponent()) {
        debugConsole_->addLogEntry(level, message);
    }
}

void NetworkAudioEditor::updateStatusDisplay() {
    if (editorBeingDeleted.load()) {
        return;
    }
    
    auto* monitor = processor_.getAudioMonitor();
    if (!monitor) {
        return;
    }
    
    try {
        const auto& stats = monitor->getStats();
        
        // Network stats
        String networkText = "NETWORK STATUS:\n";
        networkText += "Packets Sent: " + String(stats.packetsSent.load()) + "\n";
        networkText += "Packets Received: " + String(stats.packetsReceived.load()) + "\n";
        networkText += "Packets Dropped: " + String(stats.packetsDropped.load()) + "\n";
        networkText += "Avg Latency: " + String(stats.avgLatency.load(), 2) + "ms";
        networkStatsLabel_.setText(networkText, dontSendNotification);
        
        // Audio stats
        String audioText = "AUDIO STATUS:\n";
        audioText += "Buffer Underruns: " + String(stats.bufferUnderruns.load()) + "\n";
        audioText += "Buffer Overruns: " + String(stats.bufferOverruns.load()) + "\n";
        audioText += "Total Glitches: " + String(stats.glitchCount.load()) + "\n";
        
        double inputDb = stats.inputLevel.load() > 0.0 ? Decibels::gainToDecibels(stats.inputLevel.load()) : -100.0;
        double outputDb = stats.outputLevel.load() > 0.0 ? Decibels::gainToDecibels(stats.outputLevel.load()) : -100.0;
        audioText += "Input Level: " + String(inputDb, 1) + " dB\n";
        audioText += "Output Level: " + String(outputDb, 1) + " dB";
        audioStatsLabel_.setText(audioText, dontSendNotification);
        
        // Performance stats
        String perfText = "PERFORMANCE:\n";
        perfText += "CPU Usage: " + String(stats.cpuUsage.load() * 100, 1) + "%\n";
        perfText += "Process Time: " + String(stats.processTimeMs.load(), 3) + "ms\n";
        perfText += "Last Process: " + String((Time::getMillisecondCounter() - stats.lastProcessTime.load()) / 1000.0, 1) + "s ago";
        performanceLabel_.setText(perfText, dontSendNotification);
        
        // Log any issues
        if (stats.packetsDropped.load() > 0) {
            logMessage("WARNING", "Packets dropped: " + String(stats.packetsDropped.load()));
        }
        if (stats.bufferUnderruns.load() > 0 || stats.bufferOverruns.load() > 0) {
            logMessage("ERROR", "Buffer issues detected - Underruns: " + String(stats.bufferUnderruns.load()) + 
                                ", Overruns: " + String(stats.bufferOverruns.load()));
        }
        if (stats.cpuUsage.load() > 0.8) {
            logMessage("WARNING", "High CPU usage: " + String(stats.cpuUsage.load() * 100, 1) + "%");
        }
    } catch (...) {
        // Silently handle any exceptions to prevent crashes
        DBG("Exception in NetworkAudioEditor::updateStatusDisplay");
    }
}

void NetworkAudioEditor::timerCallback() {
    if (!editorBeingDeleted.load()) {
        updateStatusDisplay();
    }
}

void NetworkAudioEditor::resized() {
    tabbedComponent_.setBounds(getLocalBounds());
    
    // Layout settings tab
    auto settingsArea = Rectangle<int>(0, 0, 800, 550);
    
    // Title
    statusLabel_.setBounds(20, 20, 760, 30);
    
    // Controls in left column
    auto leftCol = settingsArea.withX(20).withWidth(300);
    modeBox_.setBounds(leftCol.withY(70).withHeight(25));
    portSlider_.setBounds(leftCol.withY(110).withHeight(25));
    addressEditor_.setBounds(leftCol.withY(150).withHeight(25));
    sessionSlider_.setBounds(leftCol.withY(190).withHeight(25));
    userSlider_.setBounds(leftCol.withY(230).withHeight(25));
    
    // Buttons
    connectButton_.setBounds(leftCol.withY(280).withWidth(100).withHeight(30));
    resetStatsButton_.setBounds(leftCol.withX(130).withY(280).withWidth(100).withHeight(30));
    
    // Stats panels in right columns
    networkStatsLabel_.setBounds(340, 70, 200, 120);
    audioStatsLabel_.setBounds(560, 70, 220, 120);
    performanceLabel_.setBounds(340, 210, 200, 100);
}

// Paint functions for settings tab
void NetworkAudioEditor::paint(Graphics& g) {
    if (tabbedComponent_.getCurrentTabIndex() == 0) { // Settings tab
        g.fillAll(Colour(0xff2a2a2a));
        
        g.setColour(Colours::white);
        g.setFont(12.0f);
        
        // Labels for controls
        g.drawText("Mode:", 20, 70, 80, 25, Justification::centredRight);
        g.drawText("Port:", 20, 110, 80, 25, Justification::centredRight);  
        g.drawText("Address:", 20, 150, 80, 25, Justification::centredRight);
        g.drawText("Session:", 20, 190, 80, 25, Justification::centredRight);
        g.drawText("User ID:", 20, 230, 80, 25, Justification::centredRight);
        
        // Draw borders around stats panels
        g.setColour(Colour(0xff444444));
        g.drawRect(networkStatsLabel_.getBounds().expanded(5), 1);
        g.drawRect(audioStatsLabel_.getBounds().expanded(5), 1);
        g.drawRect(performanceLabel_.getBounds().expanded(5), 1);
        
        g.setColour(Colours::lightgrey);
        g.setFont(10.0f);
        g.drawText("Network Stats", 345, 55, 100, 15, Justification::centredLeft);
        g.drawText("Audio Health", 565, 55, 100, 15, Justification::centredLeft);
        g.drawText("Performance", 345, 195, 100, 15, Justification::centredLeft);
    }
}

void DebugConsole::checkForCriticalIssues(const AudioStats& stats) {
    uint64_t currentTime = Time::getMillisecondCounter();
    const uint64_t alertCooldown = 5000; // 5 second cooldown between similar alerts
    
    // Check CPU usage patterns
    float cpuUsage = static_cast<float>(stats.cpuUsage.load());
    if (cpuUsage > 0.80f) { // 80% CPU threshold
        alertState_.consecutiveHighCpu++;
        if (alertState_.consecutiveHighCpu >= 3 && 
            currentTime - alertState_.lastCpuAlertTime > alertCooldown) {
            addLogEntry("🚨 CRITICAL", "SUSTAINED HIGH CPU: " + String(cpuUsage * 100, 1) + 
                       "% for " + String(alertState_.consecutiveHighCpu) + " consecutive readings");
            alertState_.lastCpuAlertTime = currentTime;
        }
    } else {
        alertState_.consecutiveHighCpu = 0;
    }
    
    // Check latency patterns
    float latency = static_cast<float>(stats.avgLatency.load());
    if (latency > 50.0f) { // 50ms latency threshold
        alertState_.consecutiveHighLatency++;
        if (alertState_.consecutiveHighLatency >= 3 &&
            currentTime - alertState_.lastLatencyAlertTime > alertCooldown) {
            addLogEntry("⚠️ WARNING", "HIGH LATENCY DETECTED: " + String(latency, 1) + 
                       "ms for " + String(alertState_.consecutiveHighLatency) + " readings");
            alertState_.lastLatencyAlertTime = currentTime;
        }
    } else {
        alertState_.consecutiveHighLatency = 0;
    }
    
    // Check glitch patterns
    int currentGlitchCount = stats.glitchCount.load();
    if (currentGlitchCount > alertState_.recentGlitchCount) {
        int newGlitches = currentGlitchCount - alertState_.recentGlitchCount;
        if (newGlitches >= 5 && 
            currentTime - alertState_.lastGlitchAlertTime > alertCooldown) {
            addLogEntry("🔥 URGENT", "GLITCH STORM: " + String(newGlitches) + 
                       " new glitches detected! Total: " + String(currentGlitchCount));
            alertState_.lastGlitchAlertTime = currentTime;
        }
        alertState_.recentGlitchCount = currentGlitchCount;
    }
    
    // Check for complete audio dropout
    double inputLevel = stats.inputLevel.load();
    double outputLevel = stats.outputLevel.load();
    if (inputLevel < 0.0001 && outputLevel < 0.0001) {
        static int silenceCounter = 0;
        silenceCounter++;
        if (silenceCounter > 50) { // 5+ seconds of silence at 100ms updates
            addLogEntry("💀 CRITICAL", "AUDIO DROPOUT: Complete silence detected for 5+ seconds");
            silenceCounter = 0; // Reset to prevent spam
        }
    } else {
        static int silenceCounter = 0;
        silenceCounter = 0;
    }
    
    // Network packet loss detection
    int totalPackets = stats.packetsSent.load() + stats.packetsReceived.load();
    int droppedPackets = stats.packetsDropped.load();
    if (totalPackets > 100 && droppedPackets > 0) {
        float lossRate = static_cast<float>(droppedPackets) / totalPackets;
        if (lossRate > 0.05f) { // 5% packet loss threshold
            addLogEntry("📡 NETWORK", "HIGH PACKET LOSS: " + String(lossRate * 100, 1) + 
                       "% (" + String(droppedPackets) + "/" + String(totalPackets) + ")");
        }
    }
}

void DebugConsole::drawNetworkTopology(Graphics& g, Rectangle<int> area) {
    if (area.isEmpty()) return;
    
    // Modern card background
    g.setColour(ModernTheme::cardBackground);
    g.fillRoundedRectangle(area.toFloat(), 8.0f);
    
    g.setColour(ModernTheme::borderColor);
    g.drawRoundedRectangle(area.toFloat(), 8.0f, 1.0f);
    
    auto bounds = area.reduced(15);
    
    // Title
    g.setColour(ModernTheme::textSecondary);
    g.setFont(Font(12.0f, Font::bold));
    g.drawText("NETWORK TOPOLOGY", bounds.getX(), bounds.getY() - 20, 200, 15, Justification::left);
    
    // Get network stats
    auto* monitor = processor_.getAudioMonitor();
    if (!monitor) return;
    
    const auto& stats = monitor->getStats();
    auto* networkManager = processor_.getNetworkManager();
    if (!networkManager) return;
    
    // Draw server/client visualization
    auto mode = networkManager->getMode();
    auto centerX = bounds.getCentreX();
    auto centerY = bounds.getCentreY();
    
    if (mode == NetworkManager::Mode::Server) {
        // Draw server node
        g.setColour(ModernTheme::accentBlue);
        g.fillEllipse(centerX - 20, centerY - 20, 40, 40);
        g.setColour(ModernTheme::textPrimary);
        g.setFont(Font(10.0f, Font::bold));
        g.drawText("SERVER", centerX - 25, centerY - 5, 50, 10, Justification::centred);
        
        // Draw connection status
        bool isConnected = networkManager->getConnectionStatus() == NetworkManager::ConnectionStatus::Connected;
        g.setColour(isConnected ? ModernTheme::accentGreen : ModernTheme::accentRed);
        g.fillEllipse(centerX + 25, centerY - 5, 10, 10);
        
        // Draw packet flow animation
        if (stats.packetsSent.load() > 0) {
            float angle = (Time::getMillisecondCounter() * 0.01f);
            for (int i = 0; i < 3; ++i) {
                float x = centerX + 30 * std::cos(angle + i * 2.0f);
                float y = centerY + 30 * std::sin(angle + i * 2.0f);
                g.setColour(ModernTheme::accentBlue.withAlpha(0.5f));
                g.fillEllipse(x - 2, y - 2, 4, 4);
            }
        }
        
    } else if (mode == NetworkManager::Mode::Client) {
        // Draw client node
        g.setColour(ModernTheme::accentGreen);
        g.fillEllipse(centerX - 15, centerY - 15, 30, 30);
        g.setColour(ModernTheme::textPrimary);
        g.setFont(Font(8.0f, Font::bold));
        g.drawText("CLIENT", centerX - 20, centerY - 3, 40, 8, Justification::centred);
        
        // Draw connection line to server
        g.setColour(ModernTheme::accentBlue);
        g.drawLine(centerX + 15, centerY, bounds.getRight() - 20, centerY, 2.0f);
        
        // Server representation
        g.setColour(ModernTheme::textSecondary);
        g.fillEllipse(bounds.getRight() - 25, centerY - 10, 20, 20);
        g.drawText("SRV", bounds.getRight() - 23, centerY - 3, 16, 8, Justification::centred);
    }
    
    // Network stats overlay
    g.setColour(ModernTheme::textSecondary);
    g.setFont(Font(9.0f));
    String netStats = "Sent: " + String(stats.packetsSent.load()) + 
                     " | Recv: " + String(stats.packetsReceived.load()) +
                     " | Lost: " + String(stats.packetsDropped.load());
    g.drawText(netStats, bounds.getX(), bounds.getBottom() - 15, bounds.getWidth(), 12, Justification::centred);
}

void DebugConsole::drawAdvancedMetrics(Graphics& g, Rectangle<int> area) {
    if (area.isEmpty()) return;
    
    // Modern card background
    g.setColour(ModernTheme::cardBackground);
    g.fillRoundedRectangle(area.toFloat(), 8.0f);
    
    g.setColour(ModernTheme::borderColor);
    g.drawRoundedRectangle(area.toFloat(), 8.0f, 1.0f);
    
    auto bounds = area.reduced(15);
    
    // Title
    g.setColour(ModernTheme::textSecondary);
    g.setFont(Font(12.0f, Font::bold));
    g.drawText("ADVANCED METRICS", bounds.getX(), bounds.getY() - 20, 200, 15, Justification::left);
    
    auto* monitor = processor_.getAudioMonitor();
    if (!monitor) return;
    
    const auto& stats = monitor->getStats();
    
    // Performance indicators with color coding
    auto metricY = bounds.getY() + 10;
    auto metricHeight = 18;
    auto spacing = 22;
    
    // CPU efficiency
    float cpuUsage = static_cast<float>(stats.cpuUsage.load());
    g.setColour(cpuUsage > 0.8f ? ModernTheme::accentRed : 
                cpuUsage > 0.6f ? ModernTheme::accentOrange : ModernTheme::accentGreen);
    g.fillRoundedRectangle(bounds.getX(), metricY, bounds.getWidth() * cpuUsage, metricHeight, 4.0f);
    g.setColour(ModernTheme::textPrimary);
    g.setFont(Font(10.0f));
    g.drawText("CPU: " + String(cpuUsage * 100, 1) + "%", bounds.getX() + 5, metricY, 100, metricHeight, Justification::centredLeft);
    
    // Latency indicator
    metricY += spacing;
    float latency = static_cast<float>(stats.avgLatency.load());
    float latencyNorm = jmin(1.0f, latency / 100.0f); // Normalize to 100ms max
    g.setColour(latency > 50.0f ? ModernTheme::accentRed : 
                latency > 20.0f ? ModernTheme::accentOrange : ModernTheme::accentBlue);
    g.fillRoundedRectangle(bounds.getX(), metricY, bounds.getWidth() * latencyNorm, metricHeight, 4.0f);
    g.setColour(ModernTheme::textPrimary);
    g.drawText("Latency: " + String(latency, 1) + "ms", bounds.getX() + 5, metricY, 120, metricHeight, Justification::centredLeft);
    
    // Audio quality indicator
    metricY += spacing;
    int glitchCount = stats.glitchCount.load();
    float qualityScore = jmax(0.0f, 1.0f - (glitchCount / 100.0f)); // Inverse relationship with glitches
    g.setColour(qualityScore > 0.8f ? ModernTheme::accentGreen : 
                qualityScore > 0.5f ? ModernTheme::accentOrange : ModernTheme::accentRed);
    g.fillRoundedRectangle(bounds.getX(), metricY, bounds.getWidth() * qualityScore, metricHeight, 4.0f);
    g.setColour(ModernTheme::textPrimary);
    g.drawText("Quality: " + String(qualityScore * 100, 0) + "%", bounds.getX() + 5, metricY, 120, metricHeight, Justification::centredLeft);
    
    // Buffer health
    metricY += spacing;
    int bufferIssues = stats.bufferUnderruns.load() + stats.bufferOverruns.load();
    float bufferHealth = jmax(0.0f, 1.0f - (bufferIssues / 50.0f));
    g.setColour(bufferHealth > 0.9f ? ModernTheme::accentGreen : 
                bufferHealth > 0.7f ? ModernTheme::accentOrange : ModernTheme::accentRed);
    g.fillRoundedRectangle(bounds.getX(), metricY, bounds.getWidth() * bufferHealth, metricHeight, 4.0f);
    g.setColour(ModernTheme::textPrimary);
    g.drawText("Buffer: " + String(bufferHealth * 100, 0) + "%", bounds.getX() + 5, metricY, 120, metricHeight, Justification::centredLeft);
    
    // Overall system health score
    metricY += spacing + 10;
    float overallHealth = (qualityScore + bufferHealth + (1.0f - latencyNorm) + (1.0f - cpuUsage)) / 4.0f;
    g.setColour(ModernTheme::textSecondary);
    g.setFont(Font(11.0f, Font::bold));
    g.drawText("SYSTEM HEALTH", bounds.getX(), metricY, bounds.getWidth(), 15, Justification::centred);
    
    metricY += 20;
    g.setColour(overallHealth > 0.8f ? ModernTheme::accentGreen : 
                overallHealth > 0.6f ? ModernTheme::accentOrange : ModernTheme::accentRed);
    auto healthRect = Rectangle<float>(bounds.getX(), metricY, bounds.getWidth(), 25);
    g.fillRoundedRectangle(healthRect, 8.0f);
    
    // Add pulsing effect for critical health
    if (overallHealth < 0.5f) {
        float pulse = 0.5f + 0.5f * std::sin(Time::getMillisecondCounter() * 0.01f);
        g.setColour(ModernTheme::accentRed.withAlpha(pulse * 0.3f));
        g.fillRoundedRectangle(healthRect.expanded(2.0f), 10.0f);
    }
    
    g.setColour(ModernTheme::textPrimary);
    g.setFont(Font(14.0f, Font::bold));
    g.drawText(String(overallHealth * 100, 0) + "%", healthRect.getX(), healthRect.getY(), healthRect.getWidth(), healthRect.getHeight(), Justification::centred);
}