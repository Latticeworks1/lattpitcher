#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PitchDetectionEngine.h"
#include "PitchDetectorGUI.h"

using namespace juce;

//==============================================================================
class StandalonePitchDetector : public AudioAppComponent,
                                private Timer
{
public:
    StandalonePitchDetector();
    ~StandalonePitchDetector() override;
    
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock(const AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;
    
    void paint(Graphics& g) override;
    void resized() override;
    
private:
    void timerCallback() override;
    void initializePermissions();
    void checkPermissionStatus();
    void updatePermissionUI();
    void requestMicrophonePermission();
    void setupAudioWithPermission();
    void exportTelemetryData();
    void resetTelemetryData();
    void showAudioSettings();
    
    // Permission handling
    enum PermissionState { Unknown, Granted, Denied, NotDetermined };
    PermissionState currentPermissionState = Unknown;
    
    // Audio processing
    static constexpr int fifoSize = 8192;
    float fifo[fifoSize];
    float processingBuffer[fifoSize];
    int fifoIndex = 0;
    bool nextBlockReady = false;
    
    // Components
    PitchDetectorGUI gui;
    PitchDetectionEngine engine;
    
    // Permission UI
    Label permissionStatusLabel;
    TextButton permissionButton;
    TextButton audioSettingsButton;
    
    // Debug and stats
    int audioBlockCount = 0;
    float currentAudioLevel = 0.0f;
    bool audioSetupFailed = true;
    
    void processAudioBlock();
    void pushSamplesToFifo(const float* samples, int numSamples);
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StandalonePitchDetector)
};
