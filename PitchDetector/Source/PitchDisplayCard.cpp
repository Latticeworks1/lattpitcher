#include "PitchDisplayCard.h"

const Colour PitchDisplayCard::GLASS_BG = Colour(0xff383838);
const Colour PitchDisplayCard::GLASS_BORDER = Colour(0xff4a4a4a);
const Colour PitchDisplayCard::NEON_ACCENT = Colour(0xff00ff88);
const Colour PitchDisplayCard::GLASS_TEXT = Colour::fromFloatRGBA(1.0f, 1.0f, 1.0f, 0.95f);

PitchDisplayCard::PitchDisplayCard()
{
    // Note name display - large and centered
    addAndMakeVisible(noteNameLabel);
    noteNameLabel.setFont(FontOptions(72.0f, Font::bold));
    noteNameLabel.setJustificationType(Justification::centred);
    noteNameLabel.setText("--", dontSendNotification);
    noteNameLabel.setColour(Label::textColourId, GLASS_TEXT);
    
    // Frequency display
    addAndMakeVisible(frequencyLabel);
    frequencyLabel.setFont(FontOptions(24.0f));
    frequencyLabel.setJustificationType(Justification::centred);
    frequencyLabel.setText("-- Hz", dontSendNotification);
    frequencyLabel.setColour(Label::textColourId, NEON_ACCENT);
    
    // Cents deviation display
    addAndMakeVisible(centsLabel);
    centsLabel.setFont(FontOptions(18.0f));
    centsLabel.setJustificationType(Justification::centred);
    centsLabel.setText("-- cents", dontSendNotification);
    centsLabel.setColour(Label::textColourId, Colours::yellow);
    
    // Audio level meter
    addAndMakeVisible(audioLevelLabel);
    audioLevelLabel.setFont(FontOptions(14.0f));
    audioLevelLabel.setJustificationType(Justification::centred);
    audioLevelLabel.setText("Audio Level: --", dontSendNotification);
    audioLevelLabel.setColour(Label::textColourId, Colours::orange);
    
    startTimerHz(30); // 30fps for smooth glow effects
}

void PitchDisplayCard::updatePitchDisplay(float frequency, const NoteInfo& noteInfo)
{
    if (frequency > 0.0f) {
        currentNote = noteInfo.noteName + String(noteInfo.octave);
        currentFrequency = String(frequency, 1) + " Hz";
        
        // Color cents display based on tuning accuracy
        float absCents = std::abs(noteInfo.centsDeviation);
        if (absCents < 5.0f) {
            currentGlowColor = Colours::green;
            centsLabel.setColour(Label::textColourId, Colours::lightgreen);
        } else if (absCents < 15.0f) {
            currentGlowColor = Colours::yellow;
            centsLabel.setColour(Label::textColourId, Colours::yellow);
        } else {
            currentGlowColor = Colours::red;
            centsLabel.setColour(Label::textColourId, Colours::lightcoral);
        }
        
        currentCents = String(noteInfo.centsDeviation > 0 ? "+" : "") + 
                      String(noteInfo.centsDeviation, 1) + " cents";
        
        glowIntensity = jmin(1.0f, frequency / 1000.0f); // Stronger glow for higher freq
    } else {
        currentNote = "--";
        currentFrequency = "-- Hz";
        currentCents = "-- cents";
        glowIntensity = 0.0f;
    }
    
    noteNameLabel.setText(currentNote, dontSendNotification);
    frequencyLabel.setText(currentFrequency, dontSendNotification);
    centsLabel.setText(currentCents, dontSendNotification);
}

void PitchDisplayCard::updateAudioLevel(float level)
{
    audioLevel = level;
    audioLevelLabel.setText("Audio Level: " + String(20.0f * std::log10(jmax(0.001f, level)), 1) + " dB", 
                           dontSendNotification);
}

void PitchDisplayCard::timerCallback()
{
    // Animate glow effects
    repaint();
}

void PitchDisplayCard::paint(Graphics& g)
{
    drawGlassCard(g, getLocalBounds());
    
    if (glowIntensity > 0.1f) {
        drawNeonGlow(g, getLocalBounds().reduced(5), currentGlowColor);
    }
}

void PitchDisplayCard::drawGlassCard(Graphics& g, Rectangle<int> area)
{
    // Glassmorphism background
    g.setColour(GLASS_BG.withAlpha(0.8f));
    g.fillRoundedRectangle(area.toFloat(), 16.0f);
    
    // Glass border
    g.setColour(GLASS_BORDER);
    g.drawRoundedRectangle(area.toFloat(), 16.0f, 1.0f);
}

void PitchDisplayCard::drawNeonGlow(Graphics& g, Rectangle<int> area, Colour color)
{
    // Animated neon glow effect
    for (int i = 0; i < 3; ++i) {
        float alpha = glowIntensity * 0.3f * (1.0f - i * 0.3f);
        g.setColour(color.withAlpha(alpha));
        g.drawRoundedRectangle(area.expanded(i * 2).toFloat(), 16.0f + i, 1.0f + i);
    }
}

void PitchDisplayCard::resized()
{
    auto area = getLocalBounds().reduced(20);
    
    // Large note name takes most space
    noteNameLabel.setBounds(area.removeFromTop(80));
    
    area.removeFromTop(5);
    
    // Frequency display
    frequencyLabel.setBounds(area.removeFromTop(30));
    
    area.removeFromTop(5);
    
    // Cents deviation
    centsLabel.setBounds(area.removeFromTop(25));
    
    area.removeFromTop(5);
    
    // Audio level at bottom
    audioLevelLabel.setBounds(area.removeFromTop(20));
}
