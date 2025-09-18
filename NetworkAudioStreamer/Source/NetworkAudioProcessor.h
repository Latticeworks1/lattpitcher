#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <memory>
#include <atomic>
#include "NetworkManager.h"
#include "AudioPacket.h"
#include "AudioMonitor.h"

using namespace juce;

class NetworkAudioProcessor : public AudioProcessor, 
                             public AudioProcessorValueTreeState::Listener {
public:
    NetworkAudioProcessor();
    ~NetworkAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(AudioBuffer<float>& buffer, MidiBuffer& midiMessages) override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    const String getName() const override { return "Network Audio Streamer"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const String&) override {}

    AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    void getStateInformation(MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    AudioProcessorValueTreeState& getParameters() { return parameters_; }
    void parameterChanged(const String& parameterID, float newValue) override;
    
    // Monitoring access
    AudioMonitor* getAudioMonitor() const { return audioMonitor_.get(); }
    
    // Set log callback for GUI logging
    void setLogCallback(std::function<void(const String&, const String&)> callback) {
        if (audioMonitor_) {
            audioMonitor_->setLogCallback(callback);
        }
        logCallback_ = std::move(callback);
    }
    
    void clearLogCallback() {
        if (audioMonitor_) {
            audioMonitor_->clearLogCallback();
        }
        logCallback_ = nullptr;
    }
    
    void logToGui(const String& level, const String& message) {
        if (logCallback_) {
            logCallback_(level, message);
        }
    }

private:
    AudioProcessorValueTreeState parameters_;
    
    std::unique_ptr<NetworkManager> networkManager_;
    std::unique_ptr<AudioMonitor> audioMonitor_;
    std::atomic<uint32_t> sessionId_{1};
    std::atomic<uint32_t> userId_{1};
    std::atomic<uint32_t> sequenceNumber_{0};
    
    double currentSampleRate_ = 44100.0;
    int currentBufferSize_ = 512;
    
    AudioBuffer<float> networkBuffer_;
    std::vector<float> tempBuffer_;
    
    // GUI logging callback
    std::function<void(const String&, const String&)> logCallback_;
    
    static AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void updateNetworkConnection();
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NetworkAudioProcessor)
};