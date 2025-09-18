#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "NetworkAudioProcessor.h"
#include "DebugConsole.h"
#include "NetworkControlPanel.h"
#include "ModernTheme.h"

using namespace juce;

class NetworkAudioEditor : public AudioProcessorEditor, public Timer {
public:
    explicit NetworkAudioEditor(NetworkAudioProcessor& p);
    ~NetworkAudioEditor() override;

    void paint(Graphics& g) override;
    void resized() override;
    void timerCallback() override;

private:
    NetworkAudioProcessor& processor_;
    
    // Tabbed interface
    TabbedComponent tabbedComponent_;
    
    // Settings tab with extracted control panel
    std::unique_ptr<NetworkControlPanel> controlPanel_;
    
    // Debug tab with extracted debug console
    std::unique_ptr<DebugConsole> debugConsole_;
    
    // Statistics display
    Label networkStatsLabel_;
    Label audioStatsLabel_;
    Label performanceLabel_;
    
    void setupSettingsTab();
    void setupDebugTab(); 
    void updateStatusDisplay();
    void logMessage(const String& level, const String& message);
    
    std::atomic<bool> editorBeingDeleted{false};
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NetworkAudioEditor)
};