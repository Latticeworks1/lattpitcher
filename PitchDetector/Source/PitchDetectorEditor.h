#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PitchDetectorProcessor.h"
#include "PitchDetectorGUI.h"

using namespace juce;

//==============================================================================
class PitchDetectorEditor : public AudioProcessorEditor
{
public:
    PitchDetectorEditor(PitchDetectorProcessor& p);
    ~PitchDetectorEditor() override;

    void paint (Graphics&) override;
    void resized() override;
    
    
    
    PitchDetectorProcessor& audioProcessor;
    PitchDetectorGUI gui;

    TextButton recordButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PitchDetectorEditor)
};