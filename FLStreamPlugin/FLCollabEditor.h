#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "FLCollabAudioProcessor.h"

class FLCollabEditor : public juce::AudioProcessorEditor,
                       public juce::Button::Listener,
                       public juce::ComboBox::Listener,
                       public juce::TextEditor::Listener
{
public:
    FLCollabEditor(FLCollabAudioProcessor& p);
    ~FLCollabEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    
    // Component listeners
    void buttonClicked(juce::Button* button) override;
    void comboBoxChanged(juce::ComboBox* comboBox) override;
    void textEditorReturnKeyPressed(juce::TextEditor& editor) override;
    void textEditorFocusLost(juce::TextEditor& editor) override;

private:
    FLCollabAudioProcessor& audioProcessor;
    
    // Network Configuration Section
    std::unique_ptr<juce::GroupComponent> networkGroup;
    std::unique_ptr<juce::Label> serverLabel;
    std::unique_ptr<juce::TextEditor> serverAddressEditor;
    std::unique_ptr<juce::Label> portLabel;
    std::unique_ptr<juce::TextEditor> portEditor;
    std::unique_ptr<juce::TextButton> connectButton;
    std::unique_ptr<juce::Label> connectionStatusLabel;
    
    // Room Configuration Section  
    std::unique_ptr<juce::GroupComponent> roomGroup;
    std::unique_ptr<juce::Label> roomCodeLabel;
    std::unique_ptr<juce::TextEditor> roomCodeEditor;
    std::unique_ptr<juce::Label> userTypeLabel;
    std::unique_ptr<juce::ComboBox> userTypeCombo;
    std::unique_ptr<juce::TextButton> joinRoomButton;
    
    // Audio Controls Section
    std::unique_ptr<juce::GroupComponent> audioGroup;
    std::unique_ptr<juce::Label> volumeLabel;
    std::unique_ptr<juce::Slider> volumeSlider;
    std::unique_ptr<juce::ToggleButton> muteButton;
    std::unique_ptr<juce::Label> audioStatusLabel;
    
    // Status Display
    std::unique_ptr<juce::GroupComponent> statusGroup;
    std::unique_ptr<juce::TextEditor> logTextEditor;
    std::unique_ptr<juce::TextButton> clearLogButton;
    
    // Parameter attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> volumeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> userTypeAttachment;
    
    // Timer for status updates
    std::unique_ptr<juce::Timer> updateTimer;
    
public:
    void updateConnectionStatus();
    void updateAudioStatus();
    
private:
    void addLogMessage(const juce::String& message);
    void connectToServer();
    void joinRoom();
    void setupDefaultValues();
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FLCollabEditor)
};