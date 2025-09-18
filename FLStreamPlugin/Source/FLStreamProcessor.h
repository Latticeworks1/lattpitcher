#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <memory>
#include <atomic>
#include <thread>
#include <queue>
#include "WebSocketServer.h"

using namespace juce;

//==============================================================================
/** Enhanced FL Studio Audio Processor with WebSocket Integration */
class FLStreamProcessor : public AudioProcessor,
                         public AudioProcessorValueTreeState::Listener
{
public:
    FLStreamProcessor();
    ~FLStreamProcessor() override;

    //==============================================================================
    // AudioProcessor overrides
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(AudioBuffer<float>& buffer, MidiBuffer& midiMessages) override;

    //==============================================================================
    // Plugin information
    const String getName() const override { return "FL Studio Audio Streamer"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const String&) override {}

    //==============================================================================
    // Editor and state
    AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    void getStateInformation(MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    //==============================================================================
    // Parameter management
    AudioProcessorValueTreeState& getParameters() { return parameters; }
    void parameterChanged(const String& parameterID, float newValue) override;

    //==============================================================================
    // WebSocket server control
    bool startWebSocketServer(int port = 9001);
    void stopWebSocketServer();
    bool isServerRunning() const;
    int getConnectedUserCount() const;
    
    //==============================================================================
    // Audio streaming
    enum class Mode { Disabled = 0, Server = 1, Client = 2 };
    void setStreamingMode(Mode mode);
    void setStreamingMode(bool isServer); // Legacy compatibility
    bool isStreamingServer() const { return streamingMode.load() == Mode::Server; }
    bool isStreamingClient() const { return streamingMode.load() == Mode::Client; }
    
    //==============================================================================
    // Room management
    void setRoomId(const String& roomId);
    String getRoomId() const { 
        const ScopedLock lock(roomIdMutex); 
        return currentRoomId; 
    }
    std::vector<String> getConnectedUsers() const;
    
    //==============================================================================
    // Client connection
    bool connectToServer(const String& serverAddress, int port, const String& roomId);
    void disconnectFromServer();
    
    //==============================================================================
    // FL Studio integration
    void setMasterTrackMode(bool enabled);
    bool isMasterTrackMode() const { return masterTrackMode.load(); }
    void setLatencyCompensation(int samples);
    int getLatencyCompensation() const { return latencyCompensationSamples.load(); }
    
    //==============================================================================
    // Statistics and monitoring
    struct StreamingStats {
        int connectedUsers = 0;
        double bandwidth = 0.0; // Mbps
        double latency = 0.0; // ms
        int64_t packetsSent = 0;
        int64_t packetsReceived = 0;
        double cpuUsage = 0.0;
        double audioLevel = 0.0;
    };
    StreamingStats getStreamingStats() const;
    
    //==============================================================================
    // Callbacks for GUI
    std::function<void(const String& message)> onLogMessage;
    std::function<void(const String& userId, const String& userName)> onUserJoined;
    std::function<void(const String& userId)> onUserLeft;
    std::function<void(const StreamingStats& stats)> onStatsUpdate;

private:
    //==============================================================================
    // Core components
    AudioProcessorValueTreeState parameters;
    std::unique_ptr<FLStreamWebSocketServer> webSocketServer;
    std::unique_ptr<FLStreamWebSocketClient> webSocketClient;
    
    //==============================================================================
    // Processing state
    std::atomic<Mode> streamingMode{Mode::Disabled};
    std::atomic<bool> masterTrackMode{false};
    std::atomic<int> latencyCompensationSamples{0};
    
    //==============================================================================
    // Audio processing (thread-safe)
    std::atomic<double> currentSampleRate{48000.0};
    std::atomic<int> currentBufferSize{512};
    std::atomic<int> webSocketPort{9001};
    String currentRoomId = "FL-ROOM-001";
    mutable CriticalSection roomIdMutex;
    
    //==============================================================================
    // Audio buffers and processing (protected by bufferMutex)
    AudioBuffer<float> mixBuffer;
    AudioBuffer<float> sendBuffer;
    AudioBuffer<float> receiveBuffer;
    mutable CriticalSection bufferMutex;
    std::vector<float> tempAudioData;
    
    //==============================================================================
    // Latency compensation
    class DelayLine {
    public:
        void setSize(int numChannels, int maxDelaySamples);
        void setDelay(int delaySamples);
        void process(AudioBuffer<float>& buffer);
        void reset();
        
    private:
        AudioBuffer<float> delayBuffer;
        std::vector<int> writePositions;
        int delayInSamples = 0;
        int maxDelaySize = 0;
    };
    DelayLine inputDelayLine;
    DelayLine outputDelayLine;
    
    //==============================================================================
    // Thread-safe audio queues
    static constexpr size_t AUDIO_QUEUE_SIZE = 32;
    std::array<OptimizedAudioPacket, AUDIO_QUEUE_SIZE> outgoingAudioQueue;
    std::array<OptimizedAudioPacket, AUDIO_QUEUE_SIZE> incomingAudioQueue;
    std::atomic<size_t> outgoingWriteIndex{0};
    std::atomic<size_t> outgoingReadIndex{0};
    std::atomic<size_t> incomingWriteIndex{0};
    std::atomic<size_t> incomingReadIndex{0};
    
    //==============================================================================
    // Statistics and monitoring
    mutable CriticalSection statsMutex;
    StreamingStats currentStats;
    std::chrono::steady_clock::time_point lastStatsUpdate;
    std::atomic<uint32_t> sequenceNumber{0};
    
    //==============================================================================
    // Parameter IDs and management
    static constexpr const char* PARAM_STREAMING_MODE = "streamingMode";
    static constexpr const char* PARAM_MASTER_TRACK = "masterTrack";
    static constexpr const char* PARAM_INPUT_GAIN = "inputGain";
    static constexpr const char* PARAM_OUTPUT_GAIN = "outputGain";
    static constexpr const char* PARAM_MIX_AMOUNT = "mixAmount";
    static constexpr const char* PARAM_LATENCY_COMP = "latencyComp";
    static constexpr const char* PARAM_WEBSOCKET_PORT = "wsPort";
    
    // Parameter atomic values
    std::atomic<float>* streamingModeParam = nullptr;
    std::atomic<float>* masterTrackParam = nullptr;
    std::atomic<float>* inputGainParam = nullptr;
    std::atomic<float>* outputGainParam = nullptr;
    std::atomic<float>* mixAmountParam = nullptr;
    std::atomic<float>* latencyCompParam = nullptr;
    std::atomic<float>* wsPortParam = nullptr;
    
    //==============================================================================
    // Private methods
    static AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void updateStreamingMode();
    void processServerMode(AudioBuffer<float>& buffer);
    void processClientMode(AudioBuffer<float>& buffer);
    void sendAudioToNetwork(const AudioBuffer<float>& buffer);
    void receiveAudioFromNetwork(AudioBuffer<float>& buffer);
    void updateStatistics();
    void logMessage(const String& message);
    
    //==============================================================================
    // WebSocket event handlers
    void handleUserJoined(const String& userId, const String& roomId);
    void handleUserLeft(const String& userId, const String& roomId);
    void handleAudioReceived(const OptimizedAudioPacket& packet, const String& userId);
    void handleWebSocketLog(const String& message);
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FLStreamProcessor)
};

//==============================================================================
/** FL Studio Latency Compensator for PDC Integration */
class FLStudioLatencyCompensator
{
public:
    FLStudioLatencyCompensator();
    
    void setTargetLatency(int samples);
    void setSampleRate(double sampleRate);
    void setBufferSize(int bufferSize);
    
    int calculateOptimalDelay() const;
    void processBlock(AudioBuffer<float>& buffer, int currentDelay);
    
    double getEstimatedNetworkLatency() const { return estimatedNetworkLatency; }
    double getEstimatedProcessingLatency() const { return estimatedProcessingLatency; }
    double getTotalLatency() const { return totalLatency; }
    
private:
    int targetLatencySamples = 0;
    double sampleRate = 48000.0;
    int bufferSize = 512;
    
    double estimatedNetworkLatency = 10.0; // ms
    double estimatedProcessingLatency = 5.0; // ms
    double totalLatency = 15.0; // ms
    
    // Adaptive latency estimation
    std::vector<double> latencyHistory;
    size_t historyIndex = 0;
    static constexpr size_t HISTORY_SIZE = 100;
    
    void updateLatencyEstimate(double measuredLatency);
};

//==============================================================================
/** Audio Router for Multi-User Management */
class FLStudioAudioRouter
{
public:
    struct UserChannel {
        String userId;
        String userName;
        bool isActive = false;
        float gain = 1.0f;
        bool isMuted = false;
        AudioBuffer<float> buffer;
        int64_t lastPacketTime = 0;
    };
    
    FLStudioAudioRouter(int maxUsers = 8);
    
    void addUser(const String& userId, const String& userName);
    void removeUser(const String& userId);
    void updateUserAudio(const String& userId, const AudioBuffer<float>& audioData);
    void mixAllUsers(AudioBuffer<float>& outputBuffer);
    
    void setUserGain(const String& userId, float gain);
    void setUserMuted(const String& userId, bool muted);
    
    int getActiveUserCount() const;
    std::vector<UserChannel> getAllUsers() const;
    
private:
    mutable std::mutex usersMutex;
    std::unordered_map<String, std::unique_ptr<UserChannel>> users;
    int maxUsers;
    
    AudioBuffer<float> mixingBuffer;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FLStudioAudioRouter)
};