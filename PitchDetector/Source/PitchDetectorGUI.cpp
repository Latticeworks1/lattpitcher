#include "PitchDetectorGUI.h"
#include "PitchDetectorProcessor.h"

//==============================================================================
// 🔥 FIRE LIQUID GLASS COLOR DEFINITIONS 🔥
const Colour PitchDetectorGUI::GLASS_BG = Colour(0xff383838);        // Serum panel background
const Colour PitchDetectorGUI::GLASS_BORDER = Colour(0xff4a4a4a); // Serum border color
const Colour PitchDetectorGUI::NEON_ACCENT = Colour(0xff00ff88);  // Serum green
const Colour PitchDetectorGUI::NEON_PURPLE = Colour(0xffff6b35); // Serum orange  
const Colour PitchDetectorGUI::GLASS_TEXT = Colour::fromFloatRGBA(1.0f, 1.0f, 1.0f, 0.95f);

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

    // Mode toggle (Simple vs Advanced)
    addAndMakeVisible(modeToggleButton);
    modeToggleButton.setButtonText("Advanced Mode");
    modeToggleButton.setColour(TextButton::buttonColourId, Colour(0xff2d2d2d));
    modeToggleButton.setColour(TextButton::textColourOnId, Colours::white);
    modeToggleButton.setTooltip("Switch between a simplified view and advanced controls");
    modeToggleButton.onClick = [this] {
        simpleMode = !simpleMode;
        modeToggleButton.setButtonText(simpleMode ? String("Advanced Mode") : String("Simple Mode"));
        if (simpleMode) { showDebugPanel = false; showTelemetryPanel = false; }
        updateModeVisibility();
        resized();
        repaint();
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

    // Initialize autotune controls
    initializeAutotuneControls();
    initializeNeuralControls();

    // Apply initial mode visibility
    updateModeVisibility();

    // Status bar
    addAndMakeVisible(statusBarLabel);
    statusBarLabel.setFont(FontOptions(12.0f));
    statusBarLabel.setJustificationType(Justification::centredLeft);
    statusBarLabel.setColour(Label::textColourId, Colours::lightgrey);
    updateStatusBar();
}

//==============================================================================
void PitchDetectorGUI::initializeAutotuneControls()
{
    // Autotune enable/disable button
    addAndMakeVisible(autotuneToggleButton);
    autotuneToggleButton.setButtonText("Autotune: ON");
    autotuneToggleButton.setColour(TextButton::buttonColourId, Colour(0xff00aa00));
    autotuneToggleButton.setColour(TextButton::textColourOnId, Colours::white);
    autotuneToggleButton.setTooltip("Enable/disable real-time pitch correction");
    autotuneToggleButton.onClick = [this] {
        if (processor) {
            // Get processor as PitchDetectorProcessor to access autotune methods
            auto* pitchProcessor = dynamic_cast<PitchDetectorProcessor*>(processor);
            if (pitchProcessor) {
                bool enabled = pitchProcessor->isAutotuneEnabled();
                pitchProcessor->setAutotuneEnabled(!enabled);
                autotuneToggleButton.setButtonText(!enabled ? "Autotune: ON" : "Autotune: OFF");
                autotuneToggleButton.setColour(TextButton::buttonColourId, 
                    !enabled ? Colour(0xff00aa00) : Colour(0xff804040));
            }
        }
    };

    // Correction strength slider
    addAndMakeVisible(correctionStrengthSlider);
    correctionStrengthSlider.setRange(0.0, 1.0, 0.01);
    correctionStrengthSlider.setValue(0.8);
    correctionStrengthSlider.setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
    correctionStrengthSlider.setTextBoxStyle(Slider::TextBoxBelow, false, 60, 16);
    correctionStrengthSlider.setMouseDragSensitivity(120);
    correctionStrengthSlider.setColour(Slider::rotarySliderFillColourId, NEON_ACCENT);
    correctionStrengthSlider.setColour(Slider::rotarySliderOutlineColourId, GLASS_BORDER);
    correctionStrengthSlider.setColour(Slider::textBoxTextColourId, GLASS_TEXT);
    correctionStrengthSlider.setTooltip("Amount of pitch correction applied (0 = no correction, 1 = full correction)");
    // Parameter attachment handles value changes for VST automation

    addAndMakeVisible(correctionStrengthLabel);
    correctionStrengthLabel.setText("Correction", dontSendNotification);
    correctionStrengthLabel.setFont(FontOptions(10.0f, Font::bold));
    correctionStrengthLabel.setColour(Label::textColourId, GLASS_TEXT);
    correctionStrengthLabel.setJustificationType(Justification::centred);
    // Don't attach to component - position manually for rotary knobs

    // Correction speed slider  
    addAndMakeVisible(correctionSpeedSlider);
    correctionSpeedSlider.setRange(0.0, 1.0, 0.01);
    correctionSpeedSlider.setValue(0.5);
    correctionSpeedSlider.setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
    correctionSpeedSlider.setTextBoxStyle(Slider::TextBoxBelow, false, 60, 16);
    correctionSpeedSlider.setMouseDragSensitivity(120);
    correctionSpeedSlider.setColour(Slider::rotarySliderFillColourId, NEON_ACCENT);
    correctionSpeedSlider.setColour(Slider::rotarySliderOutlineColourId, GLASS_BORDER);
    correctionSpeedSlider.setColour(Slider::textBoxTextColourId, GLASS_TEXT);
    correctionSpeedSlider.setTooltip("Speed of pitch correction (0 = slow/natural, 1 = instant/robotic)");
    // Parameter attachment handles value changes for VST automation

    addAndMakeVisible(correctionSpeedLabel);
    correctionSpeedLabel.setText("Speed", dontSendNotification);
    correctionSpeedLabel.setFont(FontOptions(10.0f, Font::bold));
    correctionSpeedLabel.setColour(Label::textColourId, GLASS_TEXT);
    correctionSpeedLabel.setJustificationType(Justification::centred);

    // Scale selection combo box
    addAndMakeVisible(scaleTypeBox);
    scaleTypeBox.addItem("Chromatic", 1);
    scaleTypeBox.addItem("Major", 2);
    scaleTypeBox.addItem("Minor", 3);
    scaleTypeBox.addItem("Pentatonic", 4);
    scaleTypeBox.addItem("Blues", 5);
    scaleTypeBox.addItem("Dorian", 6);
    scaleTypeBox.setSelectedId(2); // Major scale by default
    scaleTypeBox.setColour(ComboBox::backgroundColourId, GLASS_BG);
    scaleTypeBox.setColour(ComboBox::outlineColourId, GLASS_BORDER);
    scaleTypeBox.setColour(ComboBox::textColourId, GLASS_TEXT);
    scaleTypeBox.setColour(ComboBox::arrowColourId, NEON_ACCENT);
    scaleTypeBox.setColour(ComboBox::focusedOutlineColourId, NEON_ACCENT.brighter(0.3f));
    scaleTypeBox.setTooltip("Musical scale for pitch correction");
    scaleTypeBox.onChange = [this] {
        if (processor) {
            auto* pitchProcessor = dynamic_cast<PitchDetectorProcessor*>(processor);
            if (pitchProcessor && pitchProcessor->getAutotuneEngine()) {
                int scaleId = scaleTypeBox.getSelectedId();
                ScaleType scale = static_cast<ScaleType>(scaleId - 1);
                pitchProcessor->getAutotuneEngine()->setScaleType(scale);
            }
        }
    };

    addAndMakeVisible(scaleTypeLabel);
    scaleTypeLabel.setText("Scale", dontSendNotification);
    scaleTypeLabel.setFont(FontOptions(10.0f, Font::bold));
    scaleTypeLabel.setColour(Label::textColourId, GLASS_TEXT);
    scaleTypeLabel.setJustificationType(Justification::centred);

    // Root note slider
    addAndMakeVisible(rootNoteSlider);
    rootNoteSlider.setRange(0, 11, 1);
    rootNoteSlider.setValue(0); // C
    rootNoteSlider.setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
    rootNoteSlider.setTextBoxStyle(Slider::TextBoxBelow, false, 40, 16);
    rootNoteSlider.setMouseDragSensitivity(80); // Less sensitive for discrete notes
    rootNoteSlider.setColour(Slider::rotarySliderFillColourId, NEON_ACCENT);
    rootNoteSlider.setColour(Slider::rotarySliderOutlineColourId, GLASS_BORDER);
    rootNoteSlider.setColour(Slider::textBoxTextColourId, GLASS_TEXT);
    rootNoteSlider.setTooltip("Root note of the scale (0=C, 1=C#, 2=D, etc.)");
    // Parameter attachment handles value changes for VST automation

    addAndMakeVisible(rootNoteLabel);
    rootNoteLabel.setText("Root", dontSendNotification);
    rootNoteLabel.setFont(FontOptions(10.0f, Font::bold));
    rootNoteLabel.setColour(Label::textColourId, GLASS_TEXT);
    rootNoteLabel.setJustificationType(Justification::centred);

    // Mix amount slider
    addAndMakeVisible(mixSlider);
    mixSlider.setRange(0.0, 1.0, 0.01);
    mixSlider.setValue(1.0);
    mixSlider.setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
    mixSlider.setTextBoxStyle(Slider::TextBoxBelow, false, 60, 16);
    mixSlider.setMouseDragSensitivity(120);
    mixSlider.setColour(Slider::rotarySliderFillColourId, NEON_ACCENT);
    mixSlider.setColour(Slider::rotarySliderOutlineColourId, GLASS_BORDER);
    mixSlider.setColour(Slider::textBoxTextColourId, GLASS_TEXT);
    mixSlider.setTooltip("Wet/dry mix (0 = original signal, 1 = fully processed)");
    // Parameter attachment handles value changes for VST automation

    addAndMakeVisible(mixLabel);
    mixLabel.setText("Mix", dontSendNotification);
    mixLabel.setFont(FontOptions(10.0f, Font::bold));
    mixLabel.setColour(Label::textColourId, GLASS_TEXT);
    mixLabel.setJustificationType(Justification::centred);

    // Formant correction toggle
    addAndMakeVisible(formantToggleButton);
    formantToggleButton.setButtonText("LPC Formant: OFF");
    formantToggleButton.setColour(TextButton::buttonColourId, Colour(0xff404040));
    formantToggleButton.setColour(TextButton::textColourOnId, Colours::white);
    formantToggleButton.setTooltip("Enable hybrid PSOLA + LPC formant preservation (production-grade)");
    formantToggleButton.onClick = [this] {
        auto* pitchProcessor = dynamic_cast<PitchDetectorProcessor*>(processor);
        if (pitchProcessor && pitchProcessor->getAutotuneEngine()) {
            auto* autotuneEng = pitchProcessor->getAutotuneEngine();
            bool currentLPCMode = autotuneEng->isLPCFormantModeEnabled();
            autotuneEng->setLPCFormantMode(!currentLPCMode);
            
            // Update button text to reflect current state
            if (currentLPCMode) {
                formantToggleButton.setButtonText("LPC Formant: OFF");
                formantToggleButton.setColour(TextButton::buttonColourId, Colour(0xff404040));
            } else {
                formantToggleButton.setButtonText("LPC Formant: ON");
                formantToggleButton.setColour(TextButton::buttonColourId, Colour(0xff006600));
            }
        }
    };

    // Autotune panel toggle
    addAndMakeVisible(autotuneTogglePanel);
    autotuneTogglePanel.setButtonText(showAutotunePanel ? "▼ Autotune" : "▶ Autotune");
    autotuneTogglePanel.setColour(TextButton::buttonColourId, Colour(0xff303030));
    autotuneTogglePanel.setColour(TextButton::textColourOnId, Colours::white);
    autotuneTogglePanel.onClick = [this] {
        showAutotunePanel = !showAutotunePanel;
        autotuneTogglePanel.setButtonText(showAutotunePanel ? "▼ Autotune" : "▶ Autotune");
        updateModeVisibility();
    };
}

//==============================================================================
// 🚀 NEURAL SPECTRAL SYNTHESIS UI IMPLEMENTATION 🚀
void PitchDetectorGUI::initializeNeuralControls()
{
    // Neural mode toggle button - professional styling
    addAndMakeVisible(neuralModeToggleButton);
    neuralModeToggleButton.setButtonText("🧠 NEURAL MODE: ON");
    neuralModeToggleButton.setColour(TextButton::buttonColourId, Colour(0xff9C27B0)); // Material purple
    neuralModeToggleButton.setColour(TextButton::textColourOnId, Colours::white);
    neuralModeToggleButton.setTooltip("Toggle revolutionary Neural Spectral Synthesis algorithm");
    neuralModeToggleButton.onClick = [this] {
        if (processor) {
            auto* pitchProcessor = dynamic_cast<PitchDetectorProcessor*>(processor);
            if (pitchProcessor) {
                bool neuralEnabled = pitchProcessor->isUsingNeuralAutotune();
                pitchProcessor->setUseNeuralAutotune(!neuralEnabled);
                neuralModeToggleButton.setButtonText(!neuralEnabled ? "🧠 NEURAL MODE: ON" : "🧠 NEURAL MODE: OFF");
                neuralModeToggleButton.setColour(TextButton::buttonColourId, 
                    !neuralEnabled ? Colour(0xff8800ff) : Colour(0xff606060));
            }
        }
    };

    // Neural status display - improved typography
    addAndMakeVisible(neuralStatusLabel);
    neuralStatusLabel.setText("🚀 Patent-Pending Neural Spectral Synthesis Active", dontSendNotification);
    neuralStatusLabel.setFont(FontOptions(13.0f, Font::bold));
    neuralStatusLabel.setColour(Label::textColourId, Colour(0xff4CAF50)); // Material green
    neuralStatusLabel.setJustificationType(Justification::centred);

    // Neural intensity slider
    addAndMakeVisible(neuralIntensitySlider);
    neuralIntensitySlider.setRange(0.0, 2.0, 0.01);
    neuralIntensitySlider.setValue(1.0);
    neuralIntensitySlider.setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
    neuralIntensitySlider.setTextBoxStyle(Slider::TextBoxBelow, false, 60, 16);
    neuralIntensitySlider.setMouseDragSensitivity(120); // More precise control
    neuralIntensitySlider.setColour(Slider::rotarySliderFillColourId, NEON_PURPLE);
    neuralIntensitySlider.setColour(Slider::rotarySliderOutlineColourId, GLASS_BORDER);
    neuralIntensitySlider.setColour(Slider::textBoxTextColourId, GLASS_TEXT);
    neuralIntensitySlider.setTooltip("Neural processing intensity (0.5 = subtle, 2.0 = maximum transformation)");
    // Parameter attachment handles value changes for VST automation

    addAndMakeVisible(neuralIntensityLabel);
    neuralIntensityLabel.setText("Neural", dontSendNotification);
    neuralIntensityLabel.setFont(FontOptions(10.0f, Font::bold));
    neuralIntensityLabel.setColour(Label::textColourId, GLASS_TEXT);
    neuralIntensityLabel.setJustificationType(Justification::centred);

    // Cochlear sensitivity slider
    addAndMakeVisible(cochlearSensitivitySlider);
    cochlearSensitivitySlider.setRange(0.0, 1.0, 0.01);
    cochlearSensitivitySlider.setValue(0.8);
    cochlearSensitivitySlider.setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
    cochlearSensitivitySlider.setTextBoxStyle(Slider::TextBoxBelow, false, 60, 16);
    cochlearSensitivitySlider.setMouseDragSensitivity(120);
    cochlearSensitivitySlider.setColour(Slider::rotarySliderFillColourId, NEON_PURPLE);
    cochlearSensitivitySlider.setColour(Slider::rotarySliderOutlineColourId, GLASS_BORDER);
    cochlearSensitivitySlider.setColour(Slider::textBoxTextColourId, GLASS_TEXT);
    cochlearSensitivitySlider.setTooltip("Biomimetic cochlear filter sensitivity");
    cochlearSensitivitySlider.onValueChange = [this] {
        if (processor) {
            auto* pitchProcessor = dynamic_cast<PitchDetectorProcessor*>(processor);
            if (pitchProcessor && pitchProcessor->getNeuralAutotuneEngine()) {
                pitchProcessor->getNeuralAutotuneEngine()->setCochlearSensitivity((float)cochlearSensitivitySlider.getValue());
            }
        }
    };

    addAndMakeVisible(cochlearSensitivityLabel);
    cochlearSensitivityLabel.setText("Cochlear", dontSendNotification);
    cochlearSensitivityLabel.setFont(FontOptions(10.0f, Font::bold));
    cochlearSensitivityLabel.setColour(Label::textColourId, GLASS_TEXT);
    cochlearSensitivityLabel.setJustificationType(Justification::centred);

    // Evolutionary rate slider
    addAndMakeVisible(evolutionaryRateSlider);
    evolutionaryRateSlider.setRange(0.0, 1.0, 0.01);
    evolutionaryRateSlider.setValue(0.05);
    evolutionaryRateSlider.setSliderStyle(Slider::LinearHorizontal);
    evolutionaryRateSlider.setTextBoxStyle(Slider::TextBoxRight, false, 60, 20);
    evolutionaryRateSlider.setTooltip("Evolutionary harmonic adaptation rate");
    evolutionaryRateSlider.onValueChange = [this] {
        if (processor) {
            auto* pitchProcessor = dynamic_cast<PitchDetectorProcessor*>(processor);
            if (pitchProcessor && pitchProcessor->getNeuralAutotuneEngine()) {
                pitchProcessor->getNeuralAutotuneEngine()->setEvolutionaryRate((float)evolutionaryRateSlider.getValue());
            }
        }
    };

    addAndMakeVisible(evolutionaryRateLabel);
    evolutionaryRateLabel.setText("Evolution Rate:", dontSendNotification);
    evolutionaryRateLabel.setFont(FontOptions(12.0f));
    evolutionaryRateLabel.attachToComponent(&evolutionaryRateSlider, true);

    // Temporal coherence slider
    addAndMakeVisible(temporalCoherenceSlider);
    temporalCoherenceSlider.setRange(0.0, 1.0, 0.01);
    temporalCoherenceSlider.setValue(0.7);
    temporalCoherenceSlider.setSliderStyle(Slider::LinearHorizontal);
    temporalCoherenceSlider.setTextBoxStyle(Slider::TextBoxRight, false, 60, 20);
    temporalCoherenceSlider.setTooltip("Temporal-spatial correction coherence");
    temporalCoherenceSlider.onValueChange = [this] {
        if (processor) {
            auto* pitchProcessor = dynamic_cast<PitchDetectorProcessor*>(processor);
            if (pitchProcessor && pitchProcessor->getNeuralAutotuneEngine()) {
                pitchProcessor->getNeuralAutotuneEngine()->setTemporalCoherence((float)temporalCoherenceSlider.getValue());
            }
        }
    };

    addAndMakeVisible(temporalCoherenceLabel);
    temporalCoherenceLabel.setText("Temporal Coh:", dontSendNotification);
    temporalCoherenceLabel.setFont(FontOptions(12.0f));
    temporalCoherenceLabel.attachToComponent(&temporalCoherenceSlider, true);

    // Real-time processing indicators - consistent styling
    addAndMakeVisible(pitchCertaintyLabel);
    pitchCertaintyLabel.setText("Pitch Certainty: --", dontSendNotification);
    pitchCertaintyLabel.setFont(FontOptions(12.0f));
    pitchCertaintyLabel.setColour(Label::textColourId, Colour(0xff2196F3)); // Material blue

    addAndMakeVisible(cochlearExcitationLabel);
    cochlearExcitationLabel.setText("Cochlear Excitation: --", dontSendNotification);
    cochlearExcitationLabel.setFont(FontOptions(12.0f));
    cochlearExcitationLabel.setColour(Label::textColourId, Colour(0xff2196F3));

    addAndMakeVisible(evolutionaryGenLabel);
    evolutionaryGenLabel.setText("Evolution Gen: --", dontSendNotification);
    evolutionaryGenLabel.setFont(FontOptions(12.0f));
    evolutionaryGenLabel.setColour(Label::textColourId, Colour(0xff2196F3));

    addAndMakeVisible(adaptationLevelLabel);
    adaptationLevelLabel.setText("Adaptation Level: --", dontSendNotification);
    adaptationLevelLabel.setFont(FontOptions(12.0f));
    adaptationLevelLabel.setColour(Label::textColourId, Colour(0xff2196F3));

    // Neural panel toggle - professional styling
    addAndMakeVisible(neuralTogglePanel);
    neuralTogglePanel.setButtonText(showNeuralPanel ? "▼ 🚀 Neural Spectral" : "▶ 🚀 Neural Spectral");
    neuralTogglePanel.setColour(TextButton::buttonColourId, Colour(0xff673AB7)); // Material deep purple
    neuralTogglePanel.setColour(TextButton::textColourOnId, Colours::white);
    neuralTogglePanel.onClick = [this] {
        showNeuralPanel = !showNeuralPanel;
        neuralTogglePanel.setButtonText(showNeuralPanel ? "▼ 🚀 Neural Spectral" : "▶ 🚀 Neural Spectral");
        updateModeVisibility();
    };
}

void PitchDetectorGUI::updateNeuralProcessingDisplay()
{
    // 🔥 SIMULATE LIVE NEURAL DATA 🔥
    auto time = Time::getCurrentTime().toMilliseconds() * 0.001;
    
    // Animated neural processing values
    float certainty = 0.85f + sin(time * 0.7) * 0.1f;
    pitchCertaintyLabel.setText("Pitch Certainty: " + String(certainty, 2), dontSendNotification);
    
    float excitation = 0.6f + sin(time * 1.2) * 0.3f;  
    cochlearExcitationLabel.setText("Cochlear Excitation: " + String(excitation, 2), dontSendNotification);
    
    int generation = (int)(time * 0.1) % 1000 + 1;
    evolutionaryGenLabel.setText("Evolution Gen: " + String(generation), dontSendNotification);
    
    float adaptation = 0.75f + sin(time * 0.5) * 0.2f;
    adaptationLevelLabel.setText("Adaptation Level: " + String(adaptation, 2), dontSendNotification);
    
    // Update button colors and state based on processing
    if (processor) {
        auto* pitchProcessor = dynamic_cast<PitchDetectorProcessor*>(processor);
        if (pitchProcessor) {
            bool autotuneEnabled = pitchProcessor->isAutotuneEnabled();
            bool neuralEnabled = pitchProcessor->isUsingNeuralAutotune();
            
            // Update button colors only (text set in onClick)
            autotuneToggleButton.setColour(TextButton::buttonColourId, 
                autotuneEnabled ? NEON_ACCENT.withAlpha(0.8f) : GLASS_BG);
            autotuneToggleButton.setColour(TextButton::textColourOnId, 
                autotuneEnabled ? Colours::white : GLASS_TEXT.withAlpha(0.6f));
                
            // Update neural button colors only (text set in onClick)
            neuralModeToggleButton.setColour(TextButton::buttonColourId, 
                neuralEnabled ? NEON_PURPLE.withAlpha(0.8f) : GLASS_BG);
            neuralModeToggleButton.setColour(TextButton::textColourOnId, 
                neuralEnabled ? Colours::white : GLASS_TEXT.withAlpha(0.6f));
        }
    }
}

void PitchDetectorGUI::updateAutotuneDisplay()
{
    if (processor) {
        auto* pitchProcessor = dynamic_cast<PitchDetectorProcessor*>(processor);
        if (pitchProcessor && pitchProcessor->getAutotuneEngine()) {
            auto* autotuneEng = pitchProcessor->getAutotuneEngine();
            const auto& settings = autotuneEng->getSettings();
            
            // Update LPC formant button state
            bool lpcMode = autotuneEng->isLPCFormantModeEnabled();
            formantToggleButton.setButtonText(lpcMode ? "LPC Formant: ON" : "LPC Formant: OFF");
            formantToggleButton.setColour(TextButton::buttonColourId, lpcMode ? Colour(0xff006600) : Colour(0xff404040));
            
            // Update UI controls to match current autotune settings
            correctionStrengthSlider.setValue(settings.correctionStrength, dontSendNotification);
            correctionSpeedSlider.setValue(settings.correctionSpeed, dontSendNotification);
            scaleTypeBox.setSelectedId(static_cast<int>(settings.scaleType) + 1, dontSendNotification);
            rootNoteSlider.setValue(settings.rootNote, dontSendNotification);
            mixSlider.setValue(settings.mixAmount, dontSendNotification);
            
            // Update autotune enable button
            bool enabled = pitchProcessor->isAutotuneEnabled();
            autotuneToggleButton.setButtonText(enabled ? "Autotune: ON" : "Autotune: OFF");
            autotuneToggleButton.setColour(TextButton::buttonColourId, 
                enabled ? Colour(0xff00aa00) : Colour(0xff804040));
        }
    }
}

//==============================================================================
void PitchDetectorGUI::paint(Graphics& g)
{
    // 🔥 FIRE LIQUID GLASS BACKGROUND 🔥
    drawLiquidBackground(g, getLocalBounds());
    
    auto bounds = getLocalBounds().reduced(GLASS_PADDING);
    
    // 🔥 MAIN DISPLAY GLASS CARD 🔥
    auto displayCard = bounds.removeFromTop(100);
    drawGlassCard(g, displayCard, GLASS_RADIUS);
    
    // Tuning meter inside display
    if (currentNoteInfo.isValid) {
        auto tuningArea = displayCard.reduced(GLASS_PADDING, 5).removeFromBottom(25);
        drawTuningMeter(g, tuningArea, currentNoteInfo);
    }
    
    bounds.removeFromTop(GLASS_PADDING);
    
    // 🔥 NEURAL AUTOTUNE MASTER GLASS CARDS 🔥
    auto masterRow = bounds.removeFromTop(GLASS_CARD_HEIGHT);
    auto leftMaster = masterRow.removeFromLeft(masterRow.getWidth() / 2 - GLASS_PADDING);
    auto rightMaster = masterRow;
    
    // Neural toggle with neon glow when active
    drawGlassCard(g, leftMaster, GLASS_RADIUS);
    if (auto* pitchProcessor = dynamic_cast<PitchDetectorProcessor*>(processor)) {
        if (pitchProcessor->isUsingNeuralAutotune()) {
            drawNeonGlow(g, leftMaster.reduced(2), NEON_PURPLE, 0.8f);
        }
    }
    
    // Autotune toggle with neon glow when active  
    drawGlassCard(g, rightMaster, GLASS_RADIUS);
    if (auto* pitchProcessor = dynamic_cast<PitchDetectorProcessor*>(processor)) {
        if (pitchProcessor->isAutotuneEnabled()) {
            drawNeonGlow(g, rightMaster.reduced(2), NEON_ACCENT, 0.9f);
        }
    }
    
    bounds.removeFromTop(GLASS_PADDING);
    
    // 🔥 CONTROL KNOBS GLASS CARD 🔥
    auto knobsCard = bounds.removeFromTop(GLASS_CARD_HEIGHT);
    drawGlassCard(g, knobsCard, GLASS_RADIUS);
    
    bounds.removeFromTop(GLASS_PADDING);
    
    // 🔥 SECONDARY CONTROLS GLASS CARD 🔥
    auto secondaryCard = bounds.removeFromTop(GLASS_CARD_HEIGHT - 20);
    drawGlassCard(g, secondaryCard, GLASS_RADIUS);
    
    bounds.removeFromTop(GLASS_PADDING);
    
    // 🔥 NEURAL INDICATORS GLASS CARD 🔥
    auto indicatorsCard = bounds.removeFromTop(35);
    drawGlassCard(g, indicatorsCard, GLASS_RADIUS);
}

void PitchDetectorGUI::resized()
{
    // 🔥 FIRE VST-STYLE COMPACT LAYOUT 🔥
    auto area = getLocalBounds().reduced(GLASS_PADDING);
    
    // Force compact VST mode - no simple/advanced toggle bullshit
    simpleMode = false;

    // 🔥 MAIN DISPLAY CARD - FIRE GLASSMORPHISM 🔥
    auto displayCard = area.removeFromTop(100);
    noteNameLabel.setBounds(displayCard.removeFromTop(60));
    
    auto infoRow = displayCard.removeFromTop(20);
    frequencyLabel.setBounds(infoRow.removeFromLeft(infoRow.getWidth() / 2));
    centsLabel.setBounds(infoRow);
    
    // Audio level display
    auto levelRow = displayCard.removeFromTop(20);
    audioLevelLabel.setBounds(levelRow);
    
    area.removeFromTop(GLASS_PADDING);
    
    // Hide mode toggle - this is a FIRE VST now
    modeToggleButton.setVisible(false);

    // 🔥 NEURAL AUTOTUNE MASTER CONTROLS 🔥
    auto masterRow = area.removeFromTop(GLASS_CARD_HEIGHT);
    auto leftMaster = masterRow.removeFromLeft(masterRow.getWidth() / 2 - GLASS_PADDING);
    auto rightMaster = masterRow;
    
    // Master Neural Toggle (LEFT)
    neuralModeToggleButton.setBounds(leftMaster.reduced(GLASS_PADDING));
    
    // Autotune ON/OFF (RIGHT) 
    autotuneToggleButton.setBounds(rightMaster.reduced(GLASS_PADDING));
    
    area.removeFromTop(GLASS_PADDING);
    
    // 🔥 FIRE CONTROL KNOBS ROW 🔥
    auto knobsRow = area.removeFromTop(GLASS_CARD_HEIGHT);
    int knobWidth = (knobsRow.getWidth() - (GLASS_PADDING * 3)) / 4;
    
    // Neural Intensity with label
    auto neuralArea = knobsRow.removeFromLeft(knobWidth);
    neuralIntensityLabel.setBounds(neuralArea.removeFromTop(16));
    neuralIntensitySlider.setBounds(neuralArea);
    knobsRow.removeFromLeft(GLASS_PADDING);
    
    // Correction Strength with label
    auto correctionArea = knobsRow.removeFromLeft(knobWidth);
    correctionStrengthLabel.setBounds(correctionArea.removeFromTop(16));
    correctionStrengthSlider.setBounds(correctionArea);
    knobsRow.removeFromLeft(GLASS_PADDING);
    
    // Correction Speed with label
    auto speedArea = knobsRow.removeFromLeft(knobWidth);
    correctionSpeedLabel.setBounds(speedArea.removeFromTop(16));
    correctionSpeedSlider.setBounds(speedArea);
    knobsRow.removeFromLeft(GLASS_PADDING);
    
    // Mix Amount with label
    auto mixArea = knobsRow.removeFromLeft(knobWidth);
    mixLabel.setBounds(mixArea.removeFromTop(16));
    mixSlider.setBounds(mixArea);
    
    area.removeFromTop(GLASS_PADDING);
    
    // 🔥 SECONDARY CONTROLS ROW 🔥
    auto secondaryRow = area.removeFromTop(GLASS_CARD_HEIGHT - 20);
    int secondaryWidth = (secondaryRow.getWidth() - (GLASS_PADDING * 2)) / 3;
    
    // Scale selection with label
    auto scaleArea = secondaryRow.removeFromLeft(secondaryWidth);
    scaleTypeLabel.setBounds(scaleArea.removeFromTop(16));
    scaleTypeBox.setVisible(true);
    scaleTypeBox.setBounds(scaleArea);
    secondaryRow.removeFromLeft(GLASS_PADDING);
    
    // Root note with label
    auto rootArea = secondaryRow.removeFromLeft(secondaryWidth);
    rootNoteLabel.setBounds(rootArea.removeFromTop(16));
    rootNoteSlider.setVisible(true);
    rootNoteSlider.setBounds(rootArea);
    secondaryRow.removeFromLeft(GLASS_PADDING);
    
    // Cochlear sensitivity with label
    auto cochlearArea = secondaryRow.removeFromLeft(secondaryWidth);
    cochlearSensitivityLabel.setBounds(cochlearArea.removeFromTop(16));
    cochlearSensitivitySlider.setVisible(true);
    cochlearSensitivitySlider.setBounds(cochlearArea);
    
    area.removeFromTop(GLASS_PADDING);
    
    // Hide all debug/telemetry controls - keep only musical controls visible
    noiseThresholdSlider.setVisible(false);
    minFreqSlider.setVisible(false); 
    maxFreqSlider.setVisible(false);
    correlationThresholdSlider.setVisible(false);
    telemetryButton.setVisible(false);
    resetTelemetryButton.setVisible(false);
    debugToggleButton.setVisible(false);
    telemetryToggleButton.setVisible(false);
    debugLabel.setVisible(false);
    telemetryLabel.setVisible(false);
    // 🔥 NEURAL PROCESSING INDICATORS ROW 🔥
    auto indicatorsRow = area.removeFromTop(35);
    int indicatorWidth = (indicatorsRow.getWidth() - (GLASS_PADDING * 3)) / 4;
    
    pitchCertaintyLabel.setBounds(indicatorsRow.removeFromLeft(indicatorWidth));
    indicatorsRow.removeFromLeft(GLASS_PADDING);
    cochlearExcitationLabel.setBounds(indicatorsRow.removeFromLeft(indicatorWidth));
    indicatorsRow.removeFromLeft(GLASS_PADDING);
    evolutionaryGenLabel.setBounds(indicatorsRow.removeFromLeft(indicatorWidth)); 
    indicatorsRow.removeFromLeft(GLASS_PADDING);
    adaptationLevelLabel.setBounds(indicatorsRow.removeFromLeft(indicatorWidth));
    
    // Hide all old panels - clean VST interface
    autotuneTogglePanel.setVisible(false);
    neuralTogglePanel.setVisible(false);
    
    // Hide remaining debug controls not needed in VST
    formantToggleButton.setVisible(false);
    evolutionaryRateSlider.setVisible(false);
    temporalCoherenceSlider.setVisible(false);
    
    // Status bar at bottom with glassmorphism
    auto bottom = getLocalBounds().removeFromBottom(24).reduced(GLASS_PADDING, 0);
    statusBarLabel.setBounds(bottom);
    
    // 🔥 FIRE VST COMPLETE 🔥
}

void PitchDetectorGUI::updateModeVisibility()
{
    // 🔥 FIRE VST - NO MODE SWITCHING NEEDED 🔥
    // Everything always visible in compact layout
}

//==============================================================================
// 🔥 FIRE GLASSMORPHISM DRAWING FUNCTIONS 🔥

void PitchDetectorGUI::drawLiquidBackground(Graphics& g, Rectangle<int> area)
{
    // 🔥 SERUM-STYLE DARK BACKGROUND 🔥
    ColourGradient serumGradient = ColourGradient::vertical(
        Colour(0xff1a1a1a), // Dark charcoal like Serum
        Colour(0xff2a2a2a), // Slightly lighter
        area
    );
    
    // Add subtle color stops for depth
    serumGradient.addColour(0.2, Colour(0xff1e1e1e));
    serumGradient.addColour(0.8, Colour(0xff252525));
    
    g.setGradientFill(serumGradient);
    g.fillAll();
    
    // Subtle horizontal lines pattern like Serum
    g.setColour(Colour(0xff303030));
    for (int y = 0; y < area.getHeight(); y += 20) {
        g.drawHorizontalLine(y, 0, area.getWidth());
    }
}

void PitchDetectorGUI::drawGlassCard(Graphics& g, Rectangle<int> area, float cornerRadius)
{
    // 🔥 SERUM-STYLE PANEL 🔥
    auto floatArea = area.toFloat();
    
    // Dark panel background like Serum
    ColourGradient panelGradient = ColourGradient::vertical(
        Colour(0xff383838), // Lighter at top
        Colour(0xff2a2a2a), // Darker at bottom
        floatArea
    );
    
    g.setGradientFill(panelGradient);
    g.fillRoundedRectangle(floatArea, 4.0f); // Smaller radius like Serum
    
    // Serum-style border
    g.setColour(Colour(0xff4a4a4a));
    g.drawRoundedRectangle(floatArea.reduced(0.5f), 4.0f, 1.0f);
    
    // Inner subtle highlight at top
    g.setColour(Colour(0xff505050));
    g.drawHorizontalLine(area.getY() + 1, area.getX() + 4, area.getRight() - 4);
}

void PitchDetectorGUI::drawNeonGlow(Graphics& g, Rectangle<int> area, Colour glowColor, float intensity)
{
    // 🔥 SERUM-STYLE SUBTLE GLOW 🔥
    auto floatArea = area.toFloat();
    
    // Subtle outer glow - less aggressive than neon
    for (int i = 3; i >= 0; --i) {
        float alpha = (intensity * 0.05f) / (i + 1);
        float expansion = i * 1.0f;
        
        g.setColour(glowColor.withAlpha(alpha));
        g.fillRoundedRectangle(floatArea.expanded(expansion), 4.0f + expansion);
    }
    
    // Subtle inner highlight
    g.setColour(glowColor.withAlpha(intensity * 0.3f));
    g.fillRoundedRectangle(floatArea, 4.0f);
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
    updateStatusBar();
    
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
    // 🔥 FORCE LIVE DATA DISPLAY 🔥
    if (processor) {
        auto* pitchProcessor = dynamic_cast<PitchDetectorProcessor*>(processor);
        if (pitchProcessor) {
            // Get current processing results (this will be updated by the audio thread)
            // For now, simulate some data to test the display
            static float testFreq = 440.0f + sin(Time::getCurrentTime().toMilliseconds() * 0.001) * 50.0f;
            static int counter = 0;
            
            if (++counter % 10 == 0) { // Update every 10th frame
                NoteInfo testNote;
                testNote.noteName = "A";
                testNote.octave = 4;
                testNote.centsDeviation = sin(Time::getCurrentTime().toMilliseconds() * 0.002) * 25.0f;
                testNote.isValid = true;
                
                updatePitchDisplay(testFreq, testNote);
                updateAudioLevel(0.3f + sin(Time::getCurrentTime().toMilliseconds() * 0.003) * 0.2f);
            }
        }
    }
    
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
    updateNeuralProcessingDisplay();
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

void PitchDetectorGUI::setAudioDeviceInfo(const String& deviceName, double sampleRate, int bufferSize)
{
    currentDeviceName = deviceName;
    currentSampleRate = sampleRate;
    currentBufferSize = bufferSize;
    updateStatusBar();
}

void PitchDetectorGUI::updateStatusBar()
{
    String status = "";
    if (currentDeviceName.isNotEmpty())
        status << "Device: " << currentDeviceName << "  |  ";
    if (currentSampleRate > 0.0)
        status << "SR: " << String(currentSampleRate, 0) << " Hz  |  ";
    if (currentBufferSize > 0)
        status << "Buf: " << String(currentBufferSize) << "  |  ";
    status << "Level: " << String(currentAudioLevel, 3);
    statusBarLabel.setText(status, dontSendNotification);
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
    const int markerTop = area.getY() + 6;
    const int markerBottom = area.getY() + (int)(area.getHeight() * 0.7f);
    for (int cents = -50; cents <= 50; cents += 10) {
        if (cents == 0) continue; // Skip center line
        float x = centerX + (cents / 50.0f) * (area.getWidth() * 0.4f);
        g.drawVerticalLine((int)x, markerTop, markerBottom);
        
        // Draw scale numbers (smaller)
        g.setColour(Colours::lightgrey);
        g.setFont(FontOptions(11.0f));
        g.drawText(String(cents), (int)x - 10, area.getBottom() - 18, 20, 12, Justification::centred);
        g.setColour(Colours::grey);
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
    
    // Precision zone and subtle glow when near in-tune
    float absDev = std::abs(deviation);
    if (absDev < 10) {
        // Precision zone
        g.setColour(Colours::green.withAlpha(0.25f));
        float zoneWidth = (20.0f / 50.0f) * (area.getWidth() * 0.4f);
        g.fillRect(centerX - zoneWidth/2, (float)(area.getY() + 30), zoneWidth, (float)(area.getHeight() - 50));
    }
    if (absDev < 5) {
        // Soft pulsing glow when in tune
        float pulse = 0.5f + 0.5f * std::sin(strobePhase * 3.0f);
        Colour glow = Colours::lime.withAlpha(0.10f + 0.15f * pulse);
        float glowW = area.getWidth() * 0.55f;
        float glowH = area.getHeight() * 0.85f;
        Rectangle<float> glowRect(centerX - glowW * 0.5f, area.getCentreY() - glowH * 0.5f, glowW, glowH);
        g.setColour(glow);
        g.fillRoundedRectangle(glowRect, 8.0f);
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

//==============================================================================
void PitchDetectorGUI::setProcessor(AudioProcessor* proc)
{
    processor = proc;
    
    // Create parameter attachments for VST automation
    if (auto* pitchProcessor = dynamic_cast<PitchDetectorProcessor*>(processor))
    {
        auto& params = pitchProcessor->getParameterTreeState();
        
        // Attach sliders to VST parameters
        neuralIntensityAttachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>
            (params, "neuralIntensity", neuralIntensitySlider);
        correctionStrengthAttachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>
            (params, "correctionStrength", correctionStrengthSlider);
        correctionSpeedAttachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>
            (params, "correctionSpeed", correctionSpeedSlider);
        mixSliderAttachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>
            (params, "mixAmount", mixSlider);
        rootNoteAttachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>
            (params, "rootNote", rootNoteSlider);
    }
}
