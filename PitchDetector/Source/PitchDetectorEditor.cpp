#include "PitchDetectorEditor.h"

//==============================================================================
PitchDetectorEditor::PitchDetectorEditor (PitchDetectorProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    addAndMakeVisible(gui);
    audioProcessor.setGui(&gui);
    gui.setProcessor(&audioProcessor);

    // Record button not needed in VST plugin
    // addAndMakeVisible (recordButton);
    // recordButton.setButtonText ("Record");
    // recordButton.onClick = [this] { audioProcessor.startStopRecording(); };

    setSize (800, 550); // Proper VST size for the glassmorphism interface
}

PitchDetectorEditor::~PitchDetectorEditor()
{
}

//==============================================================================
void PitchDetectorEditor::paint(Graphics& g)
{
    // Use the glassmorphism background from the GUI - don't override it
    // g.fillAll(getLookAndFeel().findColour(ResizableWindow::backgroundColourId));
}

void PitchDetectorEditor::resized()
{
    gui.setBounds (getLocalBounds());
    // recordButton.setBounds (getLocalBounds().removeFromBottom(30).reduced(10));
}



