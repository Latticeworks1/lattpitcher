#include "PitchDetectorGUI.h"

//==============================================================================
PitchDetectorGUI::PitchDetectorGUI()
{
    setOpaque(true);
    initializeComponents();
    lastUpdateTime = Time::getCurrentTime();
    startTimerHz(60);
    setSize(800, 600); // More reasonable default size
}

PitchDetectorGUI::~PitchDetectorGUI()
{
    stopTimer();
}

//==============================================================================
void PitchDetectorGUI::initializeComponents()
{
    // Main display labels
    addAndMakeVisible(noteNameLabel);
    noteNameLabel.setFont(FontOptions(72.0f, Font::bold));
    noteNameLabel.setJustificationType(Justification::centred);
    noteNameLabel.setText("--", dontSendNotification);
    noteNameLabel.setColour(Label::textColourId, Colours::white);
    
    addAndMakeVisible(frequencyLabel);
    frequencyLabel.setFont(FontOptions(24.0f));
    frequencyLabel.setJustificationType(Justification::centred);
    frequencyLabel.setText("-- Hz", dontSendNotification);
    frequencyLabel.setColour(Label::textColourId, Colours::lightgreen);
    
    addAndMakeVisible(centsLabel);
    centsLabel.setFont(FontOptions(18.0f));
    centsLabel.setJustificationType(Justification::centred);
    centsLabel.setText("-- cents", dontSendNotification);
    centsLabel.setColour(Label::textColourId, Colours::yellow);
    
    addAndMakeVisible(audioLevelLabel);
    audioLevelLabel.setFont(FontOptions(14.0f));
    audioLevelLabel.setJustificationType(Justification::centred);
    audioLevelLabel.setText("Audio Level: --", dontSendNotification);
    audioLevelLabel.setColour(Label::textColourId, Colours::orange);
    
    // Parameter controls with tooltips
    addAndMakeVisible(noiseThresholdSlider);
    noiseThresholdSlider.setRange(0.001, 0.05, 0.001);
    noiseThresholdSlider.setValue(engine.getNoiseThreshold());
    noiseThresholdSlider.setTextBoxStyle(Slider::TextBoxRight, false, 60, 20);
    noiseThresholdSlider.setTooltip("Minimum signal level required for pitch detection. Lower values = more sensitive to quiet sounds.");
    noiseThresholdSlider.onValueChange = [this] { 
        engine.setNoiseThreshold((float)noiseThresholdSlider.getValue()); 
    };
    
    addAndMakeVisible(noiseThresholdLabel);
    noiseThresholdLabel.setText("Noise Threshold:", dontSendNotification);
    noiseThresholdLabel.setFont(FontOptions(12.0f));
    noiseThresholdLabel.attachToComponent(&noiseThresholdSlider, true);
    
    addAndMakeVisible(minFreqSlider);
    minFreqSlider.setRange(50, 200, 1);
    minFreqSlider.setValue(engine.getMinFrequency());
    minFreqSlider.setTextBoxStyle(Slider::TextBoxRight, false, 60, 20);
    minFreqSlider.setTooltip("Lowest frequency to detect (Hz). Set higher to ignore very low rumbles and noise.");
    minFreqSlider.onValueChange = [this] { 
        engine.setFrequencyRange((float)minFreqSlider.getValue(), engine.getMaxFrequency()); 
    };
    
    addAndMakeVisible(minFreqLabel);
    minFreqLabel.setText("Min Freq (Hz):", dontSendNotification);
    minFreqLabel.setFont(FontOptions(12.0f));
    minFreqLabel.attachToComponent(&minFreqSlider, true);
    
    addAndMakeVisible(maxFreqSlider);
    maxFreqSlider.setRange(400, 2000, 10);
    maxFreqSlider.setValue(engine.getMaxFrequency());
    maxFreqSlider.setTextBoxStyle(Slider::TextBoxRight, false, 60, 20);
    maxFreqSlider.setTooltip("Highest frequency to detect (Hz). Set lower for instruments with limited range or to ignore harmonics.");
    maxFreqSlider.onValueChange = [this] { 
        engine.setFrequencyRange(engine.getMinFrequency(), (float)maxFreqSlider.getValue()); 
    };
    
    addAndMakeVisible(maxFreqLabel);
    maxFreqLabel.setText("Max Freq (Hz):", dontSendNotification);
    maxFreqLabel.setFont(FontOptions(12.0f));
    maxFreqLabel.attachToComponent(&maxFreqSlider, true);
    
    addAndMakeVisible(correlationThresholdSlider);
    correlationThresholdSlider.setRange(0.1, 1.0, 0.05);
    correlationThresholdSlider.setValue(engine.getCorrelationThreshold());
    correlationThresholdSlider.setTextBoxStyle(Slider::TextBoxRight, false, 60, 20);
    correlationThresholdSlider.setTooltip("Detection confidence threshold. Higher values = more strict detection, fewer false positives.");
    correlationThresholdSlider.onValueChange = [this] { 
        engine.setCorrelationThreshold((float)correlationThresholdSlider.getValue()); 
    };
    
    addAndMakeVisible(correlationThresholdLabel);
    correlationThresholdLabel.setText("Confidence:", dontSendNotification);
    correlationThresholdLabel.setFont(FontOptions(12.0f));
    correlationThresholdLabel.attachToComponent(&correlationThresholdSlider, true);
    
    // Control buttons with better styling
    addAndMakeVisible(telemetryButton);
    telemetryButton.setButtonText("Export Data");
    telemetryButton.setColour(TextButton::buttonColourId, Colour(0xff404040));
    telemetryButton.setColour(TextButton::textColourOnId, Colours::white);
    telemetryButton.setColour(TextButton::textColourOffId, Colours::lightgrey);
    telemetryButton.setTooltip("Export performance and detection statistics to JSON file for analysis");
    telemetryButton.onClick = [this] { 
        if (onExportTelemetry) onExportTelemetry(); 
    };
    
    addAndMakeVisible(resetTelemetryButton);
    resetTelemetryButton.setButtonText("Reset");
    resetTelemetryButton.setColour(TextButton::buttonColourId, Colour(0xff404040));
    resetTelemetryButton.setColour(TextButton::textColourOnId, Colours::white);
    resetTelemetryButton.setColour(TextButton::textColourOffId, Colours::lightgrey);
    resetTelemetryButton.setTooltip("Reset all statistics and telemetry data");
    resetTelemetryButton.onClick = [this] { 
        if (onResetTelemetry) onResetTelemetry(); 
    };
    
    // Panel toggle buttons
    addAndMakeVisible(debugToggleButton);
    debugToggleButton.setButtonText("Debug ▼");
    debugToggleButton.setColour(TextButton::buttonColourId, Colour(0xff2d2d2d));
    debugToggleButton.setColour(TextButton::textColourOnId, Colours::white);
    debugToggleButton.setTooltip("Show/hide technical debugging information and audio processing details");
    debugToggleButton.onClick = [this] { 
        showDebugPanel = !showDebugPanel;
        debugToggleButton.setButtonText(showDebugPanel ? "Debug ▲" : "Debug ▼");
        resized();
    };
    
    addAndMakeVisible(telemetryToggleButton);
    telemetryToggleButton.setButtonText("Stats ▲");
    telemetryToggleButton.setColour(TextButton::buttonColourId, Colour(0xff2d2d2d));
    telemetryToggleButton.setColour(TextButton::textColourOnId, Colours::white);
    telemetryToggleButton.setTooltip("Show/hide performance statistics and detection metrics");
    telemetryToggleButton.onClick = [this] { 
        showTelemetryPanel = !showTelemetryPanel;
        telemetryToggleButton.setButtonText(showTelemetryPanel ? "Stats ▲" : "Stats ▼");
        resized();
    };
    
    // Info displays
    addAndMakeVisible(debugLabel);
    debugLabel.setFont(FontOptions(12.0f, Font::plain));
    debugLabel.setJustificationType(Justification::topLeft);
    debugLabel.setColour(Label::textColourId, Colours::lightgrey);
    debugLabel.setText("Debug Info", dontSendNotification);
    
    addAndMakeVisible(telemetryLabel);
    telemetryLabel.setFont(FontOptions(10.0f));
    telemetryLabel.setJustificationType(Justification::topLeft);
    telemetryLabel.setColour(Label::textColourId, Colours::cyan);
    telemetryLabel.setText("Telemetry", dontSendNotification);
}

//==============================================================================
void PitchDetectorGUI::paint(Graphics& g)
{
    // Modern dark theme background
    g.fillAll(Colour(0xff1a1a1a));
    
    auto bounds = getLocalBounds().reduced(10);
    
    // Background panels with subtle borders
    g.setColour(Colour(0xff2d2d2d));
    
    // Main display panel
    auto displayPanel = bounds.removeFromTop(120);
    g.fillRoundedRectangle(displayPanel.toFloat(), 8.0f);
    g.setColour(Colour(0xff404040));
    g.drawRoundedRectangle(displayPanel.toFloat(), 8.0f, 1.0f);
    
    // Tuning meter (integrated into main display)
    if (currentNoteInfo.isValid) {
        auto tuningArea = displayPanel.reduced(10, 5).removeFromBottom(30);
        drawTuningMeter(g, tuningArea, currentNoteInfo);
    }
    
    bounds.removeFromTop(10); // Account for spacer
    
    // Controls panel
    g.setColour(Colour(0xff2d2d2d));
    auto controlsPanel = bounds.removeFromTop(140);
    g.fillRoundedRectangle(controlsPanel.toFloat(), 6.0f);
    g.setColour(Colour(0xff404040));
    g.drawRoundedRectangle(controlsPanel.toFloat(), 6.0f, 1.0f);
    
    // Group label
    g.setColour(Colours::lightgrey);
    g.setFont(FontOptions(14.0f, Font::bold));
    g.drawText("Detection Parameters", controlsPanel.removeFromTop(25).reduced(10), Justification::left);
    
    bounds.removeFromTop(10); // Account for spacer
    
    // Collapsible panels with better styling
    if (showTelemetryPanel) {
        g.setColour(Colour(0xff2a2a2a));
        auto telemetryPanel = bounds.removeFromTop(100);
        g.fillRoundedRectangle(telemetryPanel.toFloat(), 6.0f);
        g.setColour(Colour(0xff3a3a3a));
        g.drawRoundedRectangle(telemetryPanel.toFloat(), 6.0f, 1.0f);
        bounds.removeFromTop(10); // Account for spacer
    }
    
    if (showDebugPanel) {
        g.setColour(Colour(0xff252525));
        g.fillRoundedRectangle(bounds.toFloat(), 6.0f);
        g.setColour(Colour(0xff353535));
        g.drawRoundedRectangle(bounds.toFloat(), 6.0f, 1.0f);
    }
    
    // Strobe tuner in corner when active
    if (currentFrequency > 0 && currentNoteInfo.isValid) {
        auto strobeArea = Rectangle<int>(getWidth() - 110, 140, 90, 90);
        drawStrobe(g, strobeArea, currentFrequency);
    }
    
    // Frequency history at bottom
    auto historyBounds = getLocalBounds();
    auto historyArea = historyBounds.removeFromBottom(60).reduced(20);
    drawFrequencyHistory(g, historyArea);
}

void PitchDetectorGUI::resized()
{
    auto area = getLocalBounds().reduced(10);
    
    // Top section: Main pitch display
    auto displayArea = area.removeFromTop(120);
    noteNameLabel.setBounds(displayArea.removeFromTop(50));
    
    auto infoRow = displayArea.removeFromTop(35);
    frequencyLabel.setBounds(infoRow.removeFromLeft(infoRow.getWidth() / 2));
    centsLabel.setBounds(infoRow);
    
    audioLevelLabel.setBounds(displayArea);
    
    area.removeFromTop(10); // Spacer
    
    // Middle section: Controls in organized groups
    auto controlsArea = area.removeFromTop(140);
    
    // Detection parameters group
    auto detectionGroup = controlsArea.removeFromTop(120);
    auto groupTitle = detectionGroup.removeFromTop(20);
    
    noiseThresholdSlider.setBounds(detectionGroup.removeFromTop(25).removeFromRight(200));
    minFreqSlider.setBounds(detectionGroup.removeFromTop(25).removeFromRight(200));
    maxFreqSlider.setBounds(detectionGroup.removeFromTop(25).removeFromRight(200));
    correlationThresholdSlider.setBounds(detectionGroup.removeFromTop(25).removeFromRight(200));
    
    // Button row
    auto buttonRow = controlsArea.removeFromTop(30);
    telemetryButton.setBounds(buttonRow.removeFromLeft(100).reduced(2));
    resetTelemetryButton.setBounds(buttonRow.removeFromLeft(80).reduced(2));
    buttonRow.removeFromLeft(20); // Spacer
    telemetryToggleButton.setBounds(buttonRow.removeFromLeft(80).reduced(2));
    debugToggleButton.setBounds(buttonRow.removeFromLeft(80).reduced(2));
    
    area.removeFromTop(10); // Spacer
    
    // Bottom section: Collapsible panels
    if (showTelemetryPanel) {
        auto telemetryArea = area.removeFromTop(100);
        telemetryLabel.setBounds(telemetryArea);
        area.removeFromTop(10); // Spacer after telemetry
    }
    
    if (showDebugPanel) {
        debugLabel.setBounds(area); // Takes remaining space when visible
    }
}

//==============================================================================
void PitchDetectorGUI::updatePitchDisplay(float frequency, const NoteInfo& noteInfo)
{
    // Store current values for visual feedback
    currentFrequency = frequency;
    currentNoteInfo = noteInfo;
    
    // Update frequency history for visualization
    frequencyHistory.push_back(frequency);
    if (frequencyHistory.size() > maxHistorySize) {
        frequencyHistory.erase(frequencyHistory.begin());
    }
    
    if (noteInfo.isValid) {
        String fullNoteName = noteInfo.noteName + String(noteInfo.octave);
        noteNameLabel.setText(fullNoteName, dontSendNotification);
        frequencyLabel.setText(String(frequency, 1) + " Hz", dontSendNotification);
        
        String centsText = String(noteInfo.centsDeviation > 0 ? "+" : "") + 
                         String(noteInfo.centsDeviation, 0) + " cents";
        centsLabel.setText(centsText, dontSendNotification);
        
        // Enhanced color coding for tuning accuracy
        if (std::abs(noteInfo.centsDeviation) < 5) {
            centsLabel.setColour(Label::textColourId, Colours::lime);
        } else if (std::abs(noteInfo.centsDeviation) < 10) {
            centsLabel.setColour(Label::textColourId, Colours::green);
        } else if (std::abs(noteInfo.centsDeviation) < 20) {
            centsLabel.setColour(Label::textColourId, Colours::yellow);
        } else if (std::abs(noteInfo.centsDeviation) < 40) {
            centsLabel.setColour(Label::textColourId, Colours::orange);
        } else {
            centsLabel.setColour(Label::textColourId, Colours::red);
        }
    } else {
        noteNameLabel.setText("--", dontSendNotification);
        frequencyLabel.setText("-- Hz", dontSendNotification);
        centsLabel.setText("-- cents", dontSendNotification);
        centsLabel.setColour(Label::textColourId, Colours::white);
    }
    
    // Trigger repaint for visual updates
    repaint();
}

void PitchDetectorGUI::updateAudioLevel(float level)
{
    currentAudioLevel = level;
    audioLevelLabel.setText("Audio Level: " + String(level, 4), dontSendNotification);
    
    // Color code audio level
    if (level > 0.05f) {
        audioLevelLabel.setColour(Label::textColourId, Colours::green);
    } else if (level > 0.01f) {
        audioLevelLabel.setColour(Label::textColourId, Colours::yellow);
    } else {
        audioLevelLabel.setColour(Label::textColourId, Colours::red);
    }
}

void PitchDetectorGUI::updateDebugInfo(const String& debugText)
{
    currentDebugInfo = debugText;
    debugLabel.setText(debugText, dontSendNotification);
}

//==============================================================================
void PitchDetectorGUI::timerCallback()
{
    // Update strobe animation
    auto now = Time::getCurrentTime();
    float deltaTime = (now - lastUpdateTime).inMilliseconds() / 1000.0f;
    lastUpdateTime = now;
    
    if (currentFrequency > 0) {
        strobePhase += currentFrequency * deltaTime * 0.01f; // Slow down the strobe
        if (strobePhase > 2.0f * M_PI) {
            strobePhase -= 2.0f * M_PI;
        }
    }
    
    updateTelemetryDisplay();
    repaint();
}

void PitchDetectorGUI::updateTelemetryDisplay()
{
    const auto& telemetry = engine.getTelemetry();
    
    String telemetryInfo = "=== TELEMETRY DATA ===\n";
    telemetryInfo += "Session: " + Time::getCurrentTime().toString(false, true, false, true) + "\n";
    telemetryInfo += "Duration: " + String((Time::getCurrentTime() - telemetry.sessionStart).inSeconds(), 1) + "s\n";
    
    // Performance metrics
    telemetryInfo += "Processing Cycles: " + String(telemetry.totalProcessingCycles) + "\n";
    telemetryInfo += "Avg Processing: " + String(telemetry.avgProcessingTimeMs, 3) + "ms\n";
    telemetryInfo += "Max Processing: " + String(telemetry.maxProcessingTimeMs, 3) + "ms\n";
    
    // Detection metrics
    telemetryInfo += "Detection Rate: " + String(telemetry.totalDetectionAttempts > 0 ? 
                                                (float)telemetry.successfulDetections / telemetry.totalDetectionAttempts * 100.0f : 0.0f, 1) + "%\n";
    telemetryInfo += "Avg Confidence: " + String(telemetry.avgDetectionConfidence, 3) + "\n";
    
    // Frequency range
    if (telemetry.minDetectedFreq > 0) {
        telemetryInfo += "Freq Range: " + String(telemetry.minDetectedFreq, 1) + " - " + String(telemetry.maxDetectedFreq, 1) + " Hz\n";
    }
    
    // Most common notes (top 3)
    std::vector<std::pair<String, int>> notesSorted;
    for (const auto& note : telemetry.noteDetections) {
        notesSorted.push_back({note.first, note.second});
    }
    std::sort(notesSorted.begin(), notesSorted.end(), 
              [](const auto& a, const auto& b) { return a.second > b.second; });
    
    telemetryInfo += "Top Notes: ";
    for (size_t i = 0; i < std::min(size_t(3), notesSorted.size()); ++i) {
        telemetryInfo += notesSorted[i].first + "(" + String(notesSorted[i].second) + ")";
        if (i < std::min(size_t(3), notesSorted.size()) - 1) telemetryInfo += ", ";
    }
    
    telemetryLabel.setText(telemetryInfo, dontSendNotification);
}

//==============================================================================
void PitchDetectorGUI::drawTuningMeter(Graphics& g, const Rectangle<int>& area, const NoteInfo& noteInfo)
{
    if (!noteInfo.isValid) return;
    
    // Draw tuning meter background
    g.setColour(Colours::darkgrey);
    g.fillRoundedRectangle(area.toFloat(), 5.0f);
    
    // Draw center line (perfect tune)
    auto centerX = area.getCentreX();
    g.setColour(Colours::white);
    g.drawVerticalLine(centerX, area.getY(), area.getBottom());
    
    // Draw scale markers
    g.setColour(Colours::grey);
    for (int cents = -50; cents <= 50; cents += 10) {
        if (cents == 0) continue; // Skip center line
        float x = centerX + (cents / 50.0f) * (area.getWidth() * 0.4f);
        g.drawVerticalLine(x, area.getY(), area.getBottom());
        
        // Draw scale numbers
        g.setColour(Colours::lightgrey);
        g.drawText(String(cents), x - 10, area.getBottom() - 20, 20, 15, Justification::centred);
    }
    
    // Draw current deviation indicator
    float deviation = juce::jlimit(-50.0f, 50.0f, noteInfo.centsDeviation);
    float indicatorX = centerX + (deviation / 50.0f) * (area.getWidth() * 0.4f);
    
    // Color based on accuracy
    if (std::abs(deviation) < 5) {
        g.setColour(Colours::lime);
    } else if (std::abs(deviation) < 10) {
        g.setColour(Colours::green);
    } else if (std::abs(deviation) < 20) {
        g.setColour(Colours::yellow);
    } else {
        g.setColour(Colours::red);
    }
    
    // Draw indicator triangle
    Path triangle;
    triangle.addTriangle(indicatorX, area.getY() + 10, 
                        indicatorX - 8, area.getY() + 25,
                        indicatorX + 8, area.getY() + 25);
    g.fillPath(triangle);
    
    // Draw precision zone
    if (std::abs(deviation) < 10) {
        g.setColour(Colours::green.withAlpha(0.3f));
        float zoneWidth = (20.0f / 50.0f) * (area.getWidth() * 0.4f);
        g.fillRect(centerX - zoneWidth/2, (float)(area.getY() + 30), zoneWidth, (float)(area.getHeight() - 50));
    }
}

void PitchDetectorGUI::drawFrequencyHistory(Graphics& g, const Rectangle<int>& area)
{
    if (frequencyHistory.size() < 2) return;
    
    // Enhanced background with subtle gradient
    g.setColour(Colour(0xff1e1e1e));
    g.fillRoundedRectangle(area.toFloat(), 4.0f);
    g.setColour(Colour(0xff333333));
    g.drawRoundedRectangle(area.toFloat(), 4.0f, 1.0f);
    
    auto graphArea = area.reduced(5);
    
    // Find frequency range for scaling (filter out zeros)
    std::vector<float> validFreqs;
    for (float freq : frequencyHistory) {
        if (freq > 0) validFreqs.push_back(freq);
    }
    
    if (validFreqs.empty()) return;
    
    float minFreq = *std::min_element(validFreqs.begin(), validFreqs.end());
    float maxFreq = *std::max_element(validFreqs.begin(), validFreqs.end());
    
    if (maxFreq - minFreq < 10.0f) {
        float center = (minFreq + maxFreq) / 2;
        minFreq = center - 10.0f;
        maxFreq = center + 10.0f;
    }
    
    // Draw horizontal grid lines
    g.setColour(Colour(0xff404040));
    for (int i = 1; i < 4; ++i) {
        float y = graphArea.getY() + (i / 4.0f) * graphArea.getHeight();
        g.drawHorizontalLine(y, graphArea.getX(), graphArea.getRight());
    }
    
    // Draw frequency line with gradient effect
    Path frequencyPath;
    bool first = true;
    
    for (size_t i = 0; i < frequencyHistory.size(); ++i) {
        float freq = frequencyHistory[i];
        if (freq <= 0) continue; // Skip invalid frequencies
        
        float x = graphArea.getX() + (i / float(frequencyHistory.size() - 1)) * graphArea.getWidth();
        float normalizedFreq = (freq - minFreq) / (maxFreq - minFreq);
        float y = graphArea.getBottom() - normalizedFreq * graphArea.getHeight();
        
        if (first) {
            frequencyPath.startNewSubPath(x, y);
            first = false;
        } else {
            frequencyPath.lineTo(x, y);
        }
    }
    
    // Draw with glow effect
    g.setColour(Colour(0xff00dddd).withAlpha(0.3f));
    g.strokePath(frequencyPath, PathStrokeType(4.0f));
    g.setColour(Colour(0xff00ffff));
    g.strokePath(frequencyPath, PathStrokeType(2.0f));
    
    // Enhanced labels with better contrast
    g.setColour(Colours::white.withAlpha(0.8f));
    g.setFont(FontOptions(10.0f, Font::bold));
    
    // Title
    g.drawText("Frequency History", area.getX() + 5, area.getY() + 2, 120, 12, Justification::left);
    
    // Range labels
    g.setColour(Colours::lightgrey);
    g.setFont(FontOptions(9.0f));
    g.drawText(String(maxFreq, 1) + " Hz", area.getRight() - 60, area.getY() + 2, 55, 12, Justification::right);
    g.drawText(String(minFreq, 1) + " Hz", area.getRight() - 60, area.getBottom() - 14, 55, 12, Justification::right);
}

void PitchDetectorGUI::drawStrobe(Graphics& g, const Rectangle<int>& area, float frequency)
{
    if (frequency <= 0) return;
    
    // Draw strobe tuner circle
    auto center = area.getCentre().toFloat();
    float radius = std::min(area.getWidth(), area.getHeight()) * 0.4f;
    
    // Background circle
    g.setColour(Colours::darkgrey);
    g.fillEllipse(center.x - radius, center.y - radius, radius * 2, radius * 2);
    
    // Draw strobe pattern
    g.setColour(Colours::white);
    int numStripes = 12;
    for (int i = 0; i < numStripes; ++i) {
        float angle = (i / float(numStripes)) * 2.0f * M_PI + strobePhase;
        
        // Create rotating stripe pattern
        float x1 = center.x + cos(angle) * radius * 0.7f;
        float y1 = center.y + sin(angle) * radius * 0.7f;
        float x2 = center.x + cos(angle) * radius * 0.9f;
        float y2 = center.y + sin(angle) * radius * 0.9f;
        
        g.drawLine(x1, y1, x2, y2, 2.0f);
    }
    
    // Draw center dot
    g.setColour(currentNoteInfo.isValid && std::abs(currentNoteInfo.centsDeviation) < 5 ? 
                Colours::lime : Colours::red);
    g.fillEllipse(center.x - 3, center.y - 3, 6, 6);
    
    // Enhanced label with help text
    g.setColour(Colours::white);
    g.setFont(FontOptions(9.0f, Font::bold));
    g.drawText("STROBE", area.getX(), area.getBottom() - 15, area.getWidth(), 10, Justification::centred);
    g.setColour(Colours::lightgrey);
    g.setFont(FontOptions(7.0f));
    g.drawText("Stops when in tune", area.getX(), area.getBottom() - 7, area.getWidth(), 8, Justification::centred);
}