#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PitchDetectionEngine.h"

using namespace juce;

class PitchDisplayCard : public Component,
                         private Timer
{
public:
    PitchDisplayCard();
    ~PitchDisplayCard() override = default;
    
    void paint(Graphics& g) override;
    void resized() override;
    
    void updatePitchDisplay(float frequency, const NoteInfo& noteInfo);
    void updateAudioLevel(float level);
    
private:
    void timerCallback() override;
    void drawGlassCard(Graphics& g, Rectangle<int> area);
    void drawNeonGlow(Graphics& g, Rectangle<int> area, Colour color);
    
    // Display data
    String currentNote = "--";
    String currentFrequency = "-- Hz";
    String currentCents = "-- cents";
    float audioLevel = 0.0f;
    
    // Display components
    Label noteNameLabel;
    Label frequencyLabel;
    Label centsLabel;
    Label audioLevelLabel;
    
    // Visual effects
    float glowIntensity = 0.0f;
    Colour currentGlowColor = Colours::cyan;
    
    // Glass styling
    static const Colour GLASS_BG;
    static const Colour GLASS_BORDER;
    static const Colour NEON_ACCENT;
    static const Colour GLASS_TEXT;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PitchDisplayCard)
};
