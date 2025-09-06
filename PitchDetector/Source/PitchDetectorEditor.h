#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PitchDetectorProcessor.h"
#include "PitchDetectorGUI.h"

using namespace juce;

//==============================================================================
class PitchDetectorEditor : public AudioProcessorEditor,
                            private Timer
{
public:
    PitchDetectorEditor(PitchDetectorProcessor& p);
    ~PitchDetectorEditor() override;
    
    void paint(Graphics& g) override;
    void resized() override;
    
private:
    void timerCallback() override;
    void exportTelemetryData();
    void resetTelemetryData();
    
    PitchDetectorProcessor& audioProcessor;
    PitchDetectorGUI gui;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PitchDetectorEditor)
};