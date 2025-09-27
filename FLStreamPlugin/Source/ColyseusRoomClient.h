#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <thread>
#include <atomic>
#include <queue>
#include <mutex>
#include <functional>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <cstring>

using namespace juce;

// Colyseus Protocol Constants
namespace ColyseusProtocol {
    static const uint8_t HANDSHAKE = 9;
    static const uint8_t JOIN_ROOM = 10;
    static const uint8_t ERROR = 11;
    static const uint8_t LEAVE_ROOM = 12;
    static const uint8_t ROOM_DATA = 13;
    static const uint8_t ROOM_STATE = 14;
    static const uint8_t ROOM_STATE_PATCH = 15;
    static const uint8_t ROOM_DATA_SCHEMA = 16;
    static const uint8_t ROOM_DATA_BYTES = 17;
}

struct AudioMessage {
    std::vector<float> audioData;
    std::string sessionId;
    double timestamp;
};

//==============================================================================
/** Professional Colyseus Room Client with WebSocket++ and Push-to-Talk */
class ColyseusRoomClient : private juce::Thread
{
public:
    ColyseusRoomClient();
    ~ColyseusRoomClient() override;

    //==============================================================================
    // Room management
    bool joinRoom(const String& roomName, const String& serverUrl = "ws://voice.latticeworks-ai.com:80");
    void leaveRoom();
    bool isConnected() const { return connected.load(); }
    String getCurrentRoom() const;
    String getCurrentPlayerId() const;

    //==============================================================================
    // Push-to-talk functionality
    void sendPushToTalk(bool isPushing);
    void sendAudioData(const std::vector<float>& audioBuffer);
    bool getNextAudioMessage(AudioMessage& message);

    //==============================================================================
    // User management
    int getConnectedUserCount() const { return connectedUsers.load(); }
    StringArray getConnectedUserList() const;

    //==============================================================================
    // Callbacks
    std::function<void(const String&)> onRoomJoined;
    std::function<void(const String&)> onRoomLeft;
    std::function<void(const String&, const String&)> onUserJoined;
    std::function<void(const String&)> onUserLeft;
    std::function<void(const String&)> onError;
    std::function<void(const String&)> onLogMessage;
    std::function<void(const std::string&)> onConnectionStateChanged;

private:
    //==============================================================================
    // Thread implementation
    void run() override;
    
    //==============================================================================
    // Raw SSL WebSocket connection
    bool performMatchmaking();
    bool connectToServer(const std::string& hostname, int port, const std::string& path);
    void disconnectFromServer();
    bool performWebSocketHandshake(const std::string& hostname, const std::string& path);
    bool sendBinary(const std::vector<uint8_t>& data);
    bool receiveMessages();
    void processIncomingData(const std::vector<uint8_t>& data);
    
    void sendHandshakeMessage();
    void joinRoomInternal();
    void handleColyseusMessage(const std::vector<uint8_t>& data);
    void processAudioMessage(const std::vector<uint8_t>& payload, const std::string& sessionId);
    void processRoomState(const std::vector<uint8_t>& stateData, bool isPatch);
    void parseUserListFromState(const std::vector<uint8_t>& stateData);
    std::string decodeColyseusString(const std::vector<uint8_t>& data, size_t& offset);

    //==============================================================================
    // SSL WebSocket client
    SSL_CTX* ssl_ctx;
    SSL* ssl;
    int sock;
    
    //==============================================================================
    // State management
    std::atomic<bool> connected{false};
    std::atomic<bool> shouldStop{false};
    std::atomic<bool> roomJoined{false};
    std::atomic<int> connectedUsers{0};
    
    String currentRoomName;
    String currentPlayerId; 
    String serverUrl;
    std::string sessionId;
    std::string roomId;
    std::string reconnectionToken;
    mutable CriticalSection roomStateLock;
    
    StringArray userList;
    mutable CriticalSection userListLock;

    //==============================================================================
    // Thread-safe audio message queue
    std::queue<AudioMessage> incomingAudioQueue;
    std::mutex audioQueueMutex;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ColyseusRoomClient)
};