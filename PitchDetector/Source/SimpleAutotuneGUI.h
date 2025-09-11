#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

using namespace juce;

//==============================================================================
class SimpleAutotuneGUI : public Component
{
public:
    SimpleAutotuneGUI();
    ~SimpleAutotuneGUI() override;

    void paint(Graphics& g) override;
    void resized() override;

    void setProcessor(AudioProcessor* proc);

private:
    // Minimal autotune controls
    TextButton autotuneButton;
    Slider strengthSlider;
    Slider speedSlider; 
    Slider mixSlider;
    
    Label strengthLabel;
    Label speedLabel;
    Label mixLabel;
    Label titleLabel;
    
    AudioProcessor* processor = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SimpleAutotuneGUI)
};