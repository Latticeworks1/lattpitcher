#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <curl/curl.h>
#include <thread>
#include <atomic>
#include <queue>
#include <mutex>
#include <chrono>

class CloudAPI
{
public:
    CloudAPI();
    ~CloudAPI();
    
    bool setKV(const juce::String& key, const juce::String& value);
    juce::String getKV(const juce::String& key);
    bool isConnected() const { return connected; }
    
private:
    bool connected = false;
    juce::String serverAddress = "your-worker.puter.site";
    int serverPort = 443;
    juce::String baseUrl = "https://your-worker.puter.site/api"; // Puter worker
    CURL* curl = nullptr;
    
public:
    void updateServerAddress(const juce::String& address, int port) {
        serverAddress = address;
        serverPort = port;
        juce::String protocol = (port == 443) ? "https://" : "http://";
        juce::String portStr = (port == 443 || port == 80) ? "" : ":" + juce::String(port);
        baseUrl = protocol + address + portStr + "/api";
    }
    
    struct HTTPResponse {
        juce::String data;
        long responseCode = 0;
    };
    
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, HTTPResponse* response);
    HTTPResponse makeRequest(const juce::String& url, const juce::String& postData = "");
};

class FLCollabAudioProcessor : public juce::AudioProcessor
{
public:
    FLCollabAudioProcessor();
    ~FLCollabAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;
    void releaseResources() override;

    const juce::String getName() const override { return "FL Collab Audio"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int index) override {}
    const juce::String getProgramName(int index) override { return {}; }
    void changeProgramName(int index, const juce::String& newName) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;
    
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    juce::AudioProcessorValueTreeState parameters;
    
    void setRoomCode(const juce::String& code);
    void setUserType(bool isProducer);
    void setServerAddress(const juce::String& address, int port);
    bool isConnected() const { return connected.load(); }

private:
    std::unique_ptr<CloudAPI> cloudAPI;
    
    // Parameters
    std::atomic<float>* volumeParam = nullptr;
    std::atomic<float>* muteParam = nullptr;
    std::atomic<float>* userTypeParam = nullptr;
    
    // Network state
    juce::String currentRoomCode;
    std::atomic<bool> isProducerMode { true };
    std::atomic<bool> connected { false };
    std::atomic<bool> networkRunning { false };
    
    // Audio processing
    double currentSampleRate = 48000.0;
    int currentBufferSize = 512;
    
    // Thread-safe audio queues
    std::queue<std::vector<float>> sendQueue;
    std::queue<std::vector<float>> receiveQueue;
    std::mutex sendMutex;
    std::mutex receiveMutex;
    
    // Network thread
    std::unique_ptr<std::thread> networkThread;
    std::chrono::steady_clock::time_point lastHeartbeat;
    
    // Methods
    void sendAudioData(const juce::AudioBuffer<float>& buffer);
    bool receiveAudioData(juce::AudioBuffer<float>& buffer);
    void startNetworkThread();
    void stopNetworkThread();
    void networkLoop();
    bool sendToPuter(const std::vector<float>& audioData);
    std::vector<float> receiveFromPuter();
    void updateHeartbeat();
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FLCollabAudioProcessor)
};