#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

using namespace juce;
#include "NetworkAudioProcessor.h"
#include "ModernTheme.h"

class NetworkControlPanel : public Component {
public:
    explicit NetworkControlPanel(NetworkAudioProcessor& processor);
    ~NetworkControlPanel() override;
    
    void paint(Graphics& g) override;
    void resized() override;
    void updateStatus();
    
private:
    NetworkAudioProcessor& processor_;
    
    // Network control components
    ComboBox modeBox_;
    Slider portSlider_;
    TextEditor addressEditor_;
    Slider sessionSlider_;
    Slider userSlider_;
    Label statusLabel_;
    TextButton connectButton_;
    TextButton resetStatsButton_;
    
    // Labels for controls
    Label modeLabel_;
    Label portLabel_;
    Label addressLabel_;
    Label sessionLabel_;
    Label userLabel_;
    
    void setupControls();
    void connectButtonClicked();
    void resetStatsButtonClicked();
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NetworkControlPanel)
};