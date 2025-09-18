#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <thread>
#include <atomic>
#include <memory>
#include <unordered_map>
#include <queue>

// JUCE native WebSocket implementation

using namespace juce;

//==============================================================================
/** Audio packet optimized for real-time streaming */
struct OptimizedAudioPacket 
{
    int64_t timestamp;
    uint32_t sequenceId;
    uint16_t channels;
    uint16_t sampleRate; // Sample rate in kHz (e.g., 48 for 48kHz)
    uint32_t numSamples;
    std::vector<float> audioData; // Interleaved audio
    
    OptimizedAudioPacket() = default;
    
    OptimizedAudioPacket(const AudioBuffer<float>& buffer, uint32_t seqId, double sr)
        : timestamp(Time::getCurrentTime().toMilliseconds())
        , sequenceId(seqId)
        , channels(static_cast<uint16_t>(buffer.getNumChannels()))
        , sampleRate(static_cast<uint16_t>(sr / 1000.0))
        , numSamples(static_cast<uint32_t>(buffer.getNumSamples()))
    {
        // Interleave audio data for efficient network transmission
        audioData.reserve(numSamples * channels);
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample) {
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
                audioData.push_back(buffer.getSample(ch, sample));
            }
        }
    }
    
    void toAudioBuffer(AudioBuffer<float>& buffer) const {
        buffer.setSize(static_cast<int>(channels), static_cast<int>(numSamples), false, true, true);
        
        size_t dataIndex = 0;
        for (int sample = 0; sample < static_cast<int>(numSamples); ++sample) {
            for (int ch = 0; ch < channels; ++ch) {
                if (dataIndex < audioData.size()) {
                    buffer.setSample(ch, sample, audioData[dataIndex++]);
                }
            }
        }
    }
    
    // Serialize to binary for network transmission
    MemoryBlock serialize() const {
        size_t headerSize = sizeof(timestamp) + sizeof(sequenceId) + sizeof(channels) + 
                           sizeof(sampleRate) + sizeof(numSamples);
        size_t totalSize = headerSize + audioData.size() * sizeof(float);
        
        MemoryBlock block(totalSize);
        uint8_t* data = static_cast<uint8_t*>(block.getData());
        
        memcpy(data, &timestamp, sizeof(timestamp)); data += sizeof(timestamp);
        memcpy(data, &sequenceId, sizeof(sequenceId)); data += sizeof(sequenceId);
        memcpy(data, &channels, sizeof(channels)); data += sizeof(channels);
        memcpy(data, &sampleRate, sizeof(sampleRate)); data += sizeof(sampleRate);
        memcpy(data, &numSamples, sizeof(numSamples)); data += sizeof(numSamples);
        memcpy(data, audioData.data(), audioData.size() * sizeof(float));
        
        return block;
    }
    
    // Deserialize from binary network data
    static OptimizedAudioPacket deserialize(const MemoryBlock& block) {
        OptimizedAudioPacket packet;
        const uint8_t* data = static_cast<const uint8_t*>(block.getData());
        
        memcpy(&packet.timestamp, data, sizeof(packet.timestamp)); data += sizeof(packet.timestamp);
        memcpy(&packet.sequenceId, data, sizeof(packet.sequenceId)); data += sizeof(packet.sequenceId);
        memcpy(&packet.channels, data, sizeof(packet.channels)); data += sizeof(packet.channels);
        memcpy(&packet.sampleRate, data, sizeof(packet.sampleRate)); data += sizeof(packet.sampleRate);
        memcpy(&packet.numSamples, data, sizeof(packet.numSamples)); data += sizeof(packet.numSamples);
        
        size_t audioDataSize = packet.numSamples * packet.channels;
        packet.audioData.resize(audioDataSize);
        memcpy(packet.audioData.data(), data, audioDataSize * sizeof(float));
        
        return packet;
    }
};

//==============================================================================
/** User session for managing individual connections */
struct UserSession 
{
    String userId;
    String roomId;
    String userType; // "producer" or "listener"
    bool isConnected = false;
    int64_t lastActivity = 0;
    uint32_t packetsReceived = 0;
    uint32_t packetsSent = 0;
    double bandwidth = 0.0; // bytes per second
    
    UserSession(const String& id, const String& room, const String& type)
        : userId(id), roomId(room), userType(type), isConnected(true)
        , lastActivity(Time::getCurrentTime().toMilliseconds()) {}
        
    // Make copyable by removing atomic members
    UserSession(const UserSession& other)
        : userId(other.userId), roomId(other.roomId), userType(other.userType)
        , isConnected(other.isConnected), lastActivity(other.lastActivity)
        , packetsReceived(other.packetsReceived), packetsSent(other.packetsSent)
        , bandwidth(other.bandwidth) {}
        
    UserSession& operator=(const UserSession& other) {
        if (this != &other) {
            userId = other.userId;
            roomId = other.roomId;
            userType = other.userType;
            isConnected = other.isConnected;
            lastActivity = other.lastActivity;
            packetsReceived = other.packetsReceived;
            packetsSent = other.packetsSent;
            bandwidth = other.bandwidth;
        }
        return *this;
    }
};

//==============================================================================
/** Enhanced WebSocket server using uWebSockets for production-grade performance */
class FLStreamWebSocketServer 
{
public:
    FLStreamWebSocketServer();
    ~FLStreamWebSocketServer();
    
    // Server control
    bool startServer(int port = 9001);
    void stopServer();
    bool isRunning() const { return serverRunning.load(); }
    
    // Audio streaming
    void broadcastAudio(const AudioBuffer<float>& buffer, const String& roomId);
    void sendAudioToUser(const AudioBuffer<float>& buffer, const String& userId);
    
    // User management
    int getUserCount(const String& roomId = {}) const;
    std::vector<String> getRoomList() const;
    std::vector<UserSession> getUsersInRoom(const String& roomId) const;
    
    // Statistics
    struct ServerStats {
        int totalConnections = 0;
        int activeRooms = 0;
        double totalBandwidth = 0.0; // Mbps
        int64_t totalPacketsSent = 0;
        int64_t totalPacketsReceived = 0;
        double avgLatency = 0.0; // ms
    };
    ServerStats getServerStats() const;
    
    // Callbacks for FL Studio integration
    std::function<void(const OptimizedAudioPacket&, const String& userId)> onAudioReceived;
    std::function<void(const String& userId, const String& roomId)> onUserJoined;
    std::function<void(const String& userId, const String& roomId)> onUserLeft;
    std::function<void(const String& message)> onLogMessage;
    
private:
    struct WebSocketUserData {
        String userId;
        String roomId; 
        String userType;
        int64_t connectionTime;
        uint32_t messageCount = 0;
    };
    
    std::unique_ptr<StreamingSocket> serverSocket;
    std::unordered_map<String, std::unique_ptr<class WebSocketConnection>> connections;
    std::atomic<bool> serverRunning{false};
    std::unique_ptr<std::thread> serverThread;
    
    // Thread-safe user management
    mutable CriticalSection usersMutex;
    std::unordered_map<String, std::unique_ptr<UserSession>> users;
    std::unordered_map<String, std::vector<String>> roomUsers; // roomId -> userIds
    
    // Sequence management for audio packets
    std::atomic<uint32_t> globalSequenceId{0};
    
    // Statistics
    mutable CriticalSection statsMutex;
    ServerStats stats;
    
    // Private methods
    void runServer(int port);
    void handleNewConnection(std::unique_ptr<StreamingSocket> clientSocket);
    String readHttpRequest(StreamingSocket* socket);
    bool performWebSocketHandshake(StreamingSocket* socket, const String& request);
    String extractWebSocketKey(const String& request);
    String generateWebSocketResponseKey(const String& clientKey);
    void serveWebClientPage(StreamingSocket* socket);
    void handleTextMessage(const String& userId, const String& message);
    void handleBinaryMessage(const String& userId, const MemoryBlock& data);
    void handleJoinRoom(const String& userId, const var& jsonData);
    void handlePing(const String& userId, const var& jsonData);
    void handleUserDisconnection(const String& userId);
    void sendWelcomeMessage(const String& userId);
    void sendTextToUser(const String& userId, const String& message);
    void logMessage(const String& message);
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FLStreamWebSocketServer)
};

//==============================================================================
/** WebSocket client for receiving remote vocal input */
class FLStreamWebSocketClient
{
public:
    FLStreamWebSocketClient();
    ~FLStreamWebSocketClient();
    
    // Connection management
    bool connectToServer(const String& serverAddress, int port, const String& roomId);
    void disconnect();
    bool isConnected() const { return connected.load(); }
    
    // Audio transmission
    void sendAudio(const AudioBuffer<float>& buffer);
    bool receiveAudio(AudioBuffer<float>& buffer); // Returns true if new audio received
    
    // User management
    void setUserInfo(const String& userId, const String& userType);
    String getUserId() const { return currentUserId; }
    
    // Callbacks
    std::function<void(const OptimizedAudioPacket&)> onAudioReceived;
    std::function<void(const String& message)> onLogMessage;
    
private:
    std::unique_ptr<StreamingSocket> clientSocket;
    std::unique_ptr<std::thread> clientThread;
    std::atomic<bool> connected{false};
    std::atomic<bool> shouldStop{false};
    
    String serverAddress;
    int serverPort = 9001;
    String currentRoomId;
    String currentUserId;
    String currentUserType = "listener";
    
    // Audio queues (lock-free)
    static constexpr size_t AUDIO_QUEUE_SIZE = 32;
    std::array<OptimizedAudioPacket, AUDIO_QUEUE_SIZE> receiveQueue;
    std::atomic<size_t> receiveWriteIndex{0};
    std::atomic<size_t> receiveReadIndex{0};
    
    std::array<OptimizedAudioPacket, AUDIO_QUEUE_SIZE> sendQueue;
    std::atomic<size_t> sendWriteIndex{0};
    std::atomic<size_t> sendReadIndex{0};
    
    std::atomic<uint32_t> sequenceId{0};
    
    void runClient();
    void logMessage(const String& message);
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FLStreamWebSocketClient)
};