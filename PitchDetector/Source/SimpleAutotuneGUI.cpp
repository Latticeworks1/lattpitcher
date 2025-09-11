#include "SimpleAutotuneGUI.h"
#include "PitchDetectorProcessor.h"

//==============================================================================
SimpleAutotuneGUI::SimpleAutotuneGUI()
{
    // Title
    addAndMakeVisible(titleLabel);
    titleLabel.setText("Real-time Autotune", dontSendNotification);
    titleLabel.setFont(FontOptions(24.0f, Font::bold));
    titleLabel.setJustificationType(Justification::centred);
    titleLabel.setColour(Label::textColourId, Colours::white);

    // Autotune ON/OFF button
    addAndMakeVisible(autotuneButton);
    autotuneButton.setButtonText("Autotune OFF");
    autotuneButton.setColour(TextButton::buttonColourId, Colour(0xff804040));
    autotuneButton.onClick = [this] {
        if (auto* pitchProcessor = dynamic_cast<PitchDetectorProcessor*>(processor)) {
            bool enabled = pitchProcessor->isAutotuneEnabled();
            pitchProcessor->setAutotuneEnabled(!enabled);
            autotuneButton.setButtonText(!enabled ? "Autotune ON" : "Autotune OFF");
            autotuneButton.setColour(TextButton::buttonColourId, 
                !enabled ? Colour(0xff00aa00) : Colour(0xff804040));
        }
    };

    // Correction Strength
    addAndMakeVisible(strengthSlider);
    strengthSlider.setRange(0.0, 1.0, 0.01);
    strengthSlider.setValue(0.8);
    strengthSlider.setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
    strengthSlider.setTextBoxStyle(Slider::TextBoxBelow, false, 60, 16);
    strengthSlider.onValueChange = [this] {
        if (auto* pitchProcessor = dynamic_cast<PitchDetectorProcessor*>(processor)) {
            if (auto* engine = pitchProcessor->getAutotuneEngine()) {
                engine->setCorrectionStrength(strengthSlider.getValue());
            }
        }
    };
    
    addAndMakeVisible(strengthLabel);
    strengthLabel.setText("Strength", dontSendNotification);
    strengthLabel.setJustificationType(Justification::centred);
    strengthLabel.setColour(Label::textColourId, Colours::white);

    // Correction Speed
    addAndMakeVisible(speedSlider);
    speedSlider.setRange(0.0, 1.0, 0.01);
    speedSlider.setValue(0.5);
    speedSlider.setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
    speedSlider.setTextBoxStyle(Slider::TextBoxBelow, false, 60, 16);
    speedSlider.onValueChange = [this] {
        if (auto* pitchProcessor = dynamic_cast<PitchDetectorProcessor*>(processor)) {
            if (auto* engine = pitchProcessor->getAutotuneEngine()) {
                engine->setCorrectionSpeed(speedSlider.getValue());
            }
        }
    };
    
    addAndMakeVisible(speedLabel);
    speedLabel.setText("Speed", dontSendNotification);
    speedLabel.setJustificationType(Justification::centred);
    speedLabel.setColour(Label::textColourId, Colours::white);

    // Mix Amount
    addAndMakeVisible(mixSlider);
    mixSlider.setRange(0.0, 1.0, 0.01);
    mixSlider.setValue(1.0);
    mixSlider.setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
    mixSlider.setTextBoxStyle(Slider::TextBoxBelow, false, 60, 16);
    mixSlider.onValueChange = [this] {
        if (auto* pitchProcessor = dynamic_cast<PitchDetectorProcessor*>(processor)) {
            if (auto* engine = pitchProcessor->getAutotuneEngine()) {
                engine->setMixAmount(mixSlider.getValue());
            }
        }
    };
    
    addAndMakeVisible(mixLabel);
    mixLabel.setText("Mix", dontSendNotification);
    mixLabel.setJustificationType(Justification::centred);
    mixLabel.setColour(Label::textColourId, Colours::white);

    setSize(400, 300);
}

SimpleAutotuneGUI::~SimpleAutotuneGUI()
{
}

void SimpleAutotuneGUI::paint(Graphics& g)
{
    // Dark gradient background
    g.fillAll(Colour(0xff1e1e1e));
    
    ColourGradient gradient(Colour(0xff2d2d2d), 0.0f, 0.0f,
                           Colour(0xff1a1a1a), 0.0f, (float) getHeight(), false);
    g.setGradientFill(gradient);
    g.fillAll();
    
    // Border
    g.setColour(Colour(0xff404040));
    g.drawRect(getLocalBounds(), 1);
}

void SimpleAutotuneGUI::resized()
{
    auto bounds = getLocalBounds().reduced(20);
    
    // Title at top
    titleLabel.setBounds(bounds.removeFromTop(40));
    bounds.removeFromTop(20);
    
    // Button
    autotuneButton.setBounds(bounds.removeFromTop(40));
    bounds.removeFromTop(30);
    
    // Three knobs in a row
    auto knobArea = bounds.removeFromTop(100);
    int knobWidth = knobArea.getWidth() / 3;
    
    auto strengthArea = knobArea.removeFromLeft(knobWidth);
    strengthSlider.setBounds(strengthArea.removeFromTop(80));
    strengthLabel.setBounds(strengthArea);
    
    auto speedArea = knobArea.removeFromLeft(knobWidth);
    speedSlider.setBounds(speedArea.removeFromTop(80));
    speedLabel.setBounds(speedArea);
    
    auto mixArea = knobArea;
    mixSlider.setBounds(mixArea.removeFromTop(80));
    mixLabel.setBounds(mixArea);
}

void SimpleAutotuneGUI::setProcessor(AudioProcessor* proc)
{
    processor = proc;
    
    // Initialize button state
    if (auto* pitchProcessor = dynamic_cast<PitchDetectorProcessor*>(processor)) {
        bool enabled = pitchProcessor->isAutotuneEnabled();
        autotuneButton.setButtonText(enabled ? "Autotune ON" : "Autotune OFF");
        autotuneButton.setColour(TextButton::buttonColourId, 
            enabled ? Colour(0xff00aa00) : Colour(0xff804040));
    }
}
