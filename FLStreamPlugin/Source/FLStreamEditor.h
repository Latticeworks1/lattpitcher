#pragma once

#include "FLStreamProcessor.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

using namespace juce;

//==============================================================================
/** Professional FL Studio Streamer GUI Component */
class FLStreamEditor : public AudioProcessorEditor,
                      private Timer,
                      private Button::Listener,
                      private Slider::Listener,
                      private ComboBox::Listener
{
public:
    FLStreamEditor(FLStreamProcessor& processor);
    ~FLStreamEditor() override;

    //==============================================================================
    void paint(Graphics&) override;
    void resized() override;
    
    //==============================================================================
    // Component listeners
    void buttonClicked(Button* button) override;
    void sliderValueChanged(Slider* slider) override;
    void comboBoxChanged(ComboBox* comboBoxThatHasChanged) override;
    
    //==============================================================================
    // Timer callback for real-time updates
    void timerCallback() override;

private:
    //==============================================================================
    // Reference to processor
    FLStreamProcessor& audioProcessor;
    
    //==============================================================================
    // Visual constants (FL Studio theme)
    static constexpr int EDITOR_WIDTH = 600;
    static constexpr int EDITOR_HEIGHT = 750;
    static constexpr int MARGIN = 10;
    static constexpr int COMPONENT_HEIGHT = 25;
    static constexpr int SECTION_SPACING = 15;
    
    // FL Studio color scheme
    const Colour FL_BACKGROUND = Colour(0xff393f47);
    const Colour FL_PANEL = Colour(0xff4a5058);
    const Colour FL_ACCENT = Colour(0xff5fb3d4);
    const Colour FL_SUCCESS = Colour(0xff7cb518);
    const Colour FL_WARNING = Colour(0xfff5a623);
    const Colour FL_DANGER = Colour(0xffe74c3c);
    const Colour FL_TEXT = Colour(0xffffffff);
    const Colour FL_TEXT_SECONDARY = Colour(0xffb8bcc2);
    
    //==============================================================================
    // Main control sections
    
    // Header section
    Label titleLabel;
    Label statusLabel;
    
    // Connection section
    GroupComponent connectionGroup;
    ComboBox streamingModeCombo;
    Label streamingModeLabel;
    TextButton serverStartButton;
    TextButton connectButton;
    Label portLabel;
    Slider portSlider;
    TextEditor roomIdEditor;
    Label roomIdLabel;
    
    // Client connection fields
    Label serverAddressLabel;
    TextEditor serverAddressEditor;
    Label serverPortLabel;
    Slider serverPortSlider;
    
    // Audio controls section
    GroupComponent audioGroup;
    
    Slider inputGainSlider;
    Label inputGainLabel;
    Label inputGainValueLabel;
    
    Slider outputGainSlider;
    Label outputGainLabel;
    Label outputGainValueLabel;
    
    Slider mixAmountSlider;
    Label mixAmountLabel;
    Label mixAmountValueLabel;
    
    // Advanced settings section
    GroupComponent advancedGroup;
    
    ToggleButton masterTrackToggle;
    
    Slider latencyCompSlider;
    Label latencyCompLabel;
    Label latencyCompValueLabel;
    
    // Status and monitoring section
    GroupComponent statusGroup;
    
    Label connectedUsersLabel;
    Label connectedUsersValue;
    
    Label bandwidthLabel;
    Label bandwidthValue;
    
    Label latencyLabel;
    Label latencyValue;
    
    Label cpuUsageLabel;
    Label cpuUsageValue;
    
    // Audio level meters
    Component inputMeterComponent;
    Component outputMeterComponent;
    Label inputMeterLabel;
    Label outputMeterLabel;
    
    // Log section
    GroupComponent logGroup;
    TextEditor logTextEditor;
    TextButton clearLogButton;
    TextButton openWebClientButton;
    
    //==============================================================================
    // Parameter attachments for automation
    std::unique_ptr<AudioProcessorValueTreeState::ComboBoxAttachment> streamingModeAttachment;
    std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment> inputGainAttachment;
    std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment> outputGainAttachment;
    std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment> mixAmountAttachment;
    std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment> latencyCompAttachment;
    std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment> portAttachment;
    std::unique_ptr<AudioProcessorValueTreeState::ButtonAttachment> masterTrackAttachment;
    
    //==============================================================================
    // Audio level visualization
    struct AudioLevelMeter {
        float rmsLevel = 0.0f;
        float peakLevel = 0.0f;
        float peakHold = 0.0f;
        int64_t peakHoldTime = 0;
        
        void update(float rms, float peak) {
            rmsLevel = rms;
            peakLevel = peak;
            
            if (peak > peakHold) {
                peakHold = peak;
                peakHoldTime = Time::getCurrentTime().toMilliseconds();
            } else if (Time::getCurrentTime().toMilliseconds() - peakHoldTime > 1000) {
                peakHold *= 0.95f;
            }
        }
        
        void paint(Graphics& g, Rectangle<int> bounds) {
            // Background
            g.setColour(Colour(0xff2c2c2c));
            g.fillRect(bounds);
            
            // RMS level
            const float rmsWidth = rmsLevel * bounds.getWidth();
            g.setColour(Colour(0xff27ae60));
            g.fillRect(bounds.getX(), bounds.getY(), static_cast<int>(rmsWidth), bounds.getHeight());
            
            // Peak hold
            if (peakHold > 0.01f) {
                const int peakX = static_cast<int>(peakHold * bounds.getWidth());
                g.setColour(Colour(0xfff39c12));
                g.fillRect(bounds.getX() + peakX - 1, bounds.getY(), 2, bounds.getHeight());
            }
            
            // Clip indicator
            if (peakLevel > 0.95f) {
                g.setColour(Colour(0xffe74c3c));
                g.fillRect(bounds.getRight() - 10, bounds.getY(), 10, bounds.getHeight());
            }
            
            // Scale markings
            g.setColour(Colour(0xff7f8c8d));
            for (int db = -60; db <= 0; db += 10) {
                const float position = jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);
                const int x = bounds.getX() + static_cast<int>(position * bounds.getWidth());
                g.drawVerticalLine(x, bounds.getY(), bounds.getBottom());
            }
            
            // Border
            g.setColour(Colour(0xff5a5a5a));
            g.drawRect(bounds, 1);
        }
    };
    
    AudioLevelMeter inputMeter;
    AudioLevelMeter outputMeter;
    
    //==============================================================================
    // Status tracking
    bool isConnected = false;
    bool isServerMode = false;
    FLStreamProcessor::StreamingStats lastStats;
    
    //==============================================================================
    // Helper methods
    void setupComponents();
    void setupParameterAttachments();
    void updateConnectionStatus();
    void updateStreamingMode();
    void updateAudioLevels();
    void updateStatistics();
    void addLogMessage(const String& message);
    void openWebClient();
    
    String formatBandwidth(double mbps);
    String formatLatency(double ms);
    String formatPercentage(double value);
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FLStreamEditor)
};