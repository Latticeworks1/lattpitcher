#include "ColyseusRoomClient.h"
#include <cstdlib>
#include <ctime>
#include <sstream>
#include <juce_core/system/juce_PlatformDefs.h>

using namespace juce;

//==============================================================================
ColyseusRoomClient::ColyseusRoomClient() : juce::Thread("ColyseusWebSocket"), 
    ssl_ctx(nullptr), ssl(nullptr), sock(-1)
{
    // Initialize OpenSSL
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
    
    std::cout << "FL Stream: ColyseusRoomClient initialized with SSL WebSocket implementation" << std::endl;
}

ColyseusRoomClient::~ColyseusRoomClient() 
{
    leaveRoom();
    if (ssl_ctx) {
        SSL_CTX_free(ssl_ctx);
    }
}

bool ColyseusRoomClient::joinRoom(const String& roomName, const String& serverUrl)
{
    const ScopedLock lock(roomStateLock);
    
    if (connected.load()) leaveRoom();
    
    currentRoomName = roomName;
    this->serverUrl = serverUrl;
    shouldStop = false;
    
    startThread();
    
    // Wait for connection
    for (int i = 0; i < 50 && !connected.load(); ++i)
        Thread::sleep(100);
    
    return connected.load();
}

void ColyseusRoomClient::leaveRoom()
{
    shouldStop = true;
    disconnectFromServer();
    
    if (isThreadRunning()) {
        stopThread(2000);
    }
    
    connected = false;
    roomJoined = false;
    
    if (onRoomLeft) onRoomLeft(currentRoomName);
    
    const ScopedLock lock(roomStateLock);
    currentRoomName = "";
    currentPlayerId = "";
}

String ColyseusRoomClient::getCurrentRoom() const
{
    const ScopedLock lock(roomStateLock);
    return currentRoomName;
}

String ColyseusRoomClient::getCurrentPlayerId() const
{
    const ScopedLock lock(roomStateLock);
    return String(sessionId);
}

StringArray ColyseusRoomClient::getConnectedUserList() const
{
    const ScopedLock lock(userListLock);
    return userList;
}

void ColyseusRoomClient::run()
{
    try {
        if (onLogMessage) onLogMessage("Starting Colyseus matchmaking");
        std::cout << "FL Stream: Starting Colyseus matchmaking and connection flow" << std::endl;
        
        // Perform HTTP matchmaking first to get room reservation
        if (!performMatchmaking()) {
            if (onError) onError("Matchmaking failed"); 
            return;
        }
        
        // Connect to WebSocket with reserved room path
        std::string wsPath = "/" + roomId + "?sessionId=" + sessionId;
        std::cout << "FL Stream: Attempting WebSocket connection to path: " << wsPath << std::endl;
        if (!connectToServer("voice.latticeworks-ai.com", 443, wsPath)) {
            std::cout << "FL Stream: Failed to connect to WebSocket server" << std::endl;
            if (onError) onError("Failed to connect to WebSocket");
            return;
        }
        
        connected = true;
        if (onLogMessage) onLogMessage("WebSocket connected successfully");
        std::cout << "FL Stream: WebSocket connected successfully" << std::endl;
        
        // Minimize delay to test seat reservation timeout
        std::cout << "FL Stream: DEBUG - Connecting immediately after matchmaking to test seat reservation timing" << std::endl;
        
        // Send JOIN_ROOM immediately to claim the reserved seat
        joinRoomInternal();
        
        // Message receive loop
        while (!shouldStop.load() && connected.load()) {
            if (!receiveMessages()) {
                break;
            }
            juce::Thread::sleep(1); // Minimal delay for tight timing
        }
    }
    catch (const std::exception& e) {
        if (onError) onError("Connection exception: " + String(e.what()));
        std::cout << "FL Stream: Connection exception: " << e.what() << std::endl;
    }
    
    connected = false;
    if (onRoomLeft) onRoomLeft(currentRoomName);
}

bool ColyseusRoomClient::performMatchmaking()
{
    try {
        if (onLogMessage) onLogMessage("Performing HTTP matchmaking...");
        std::cout << "FL Stream: Performing HTTP matchmaking for room: my_room" << std::endl;
        
        // Manual HTTP POST with SSL
        SSL_CTX* ssl_ctx_http = SSL_CTX_new(TLS_client_method());
        if (!ssl_ctx_http) return false;
        
        int sock_http = socket(AF_INET, SOCK_STREAM, 0);
        if (sock_http < 0) {
            SSL_CTX_free(ssl_ctx_http);
            return false;
        }
        
        struct hostent* server = gethostbyname("voice.latticeworks-ai.com");
        if (!server) {
            close(sock_http);
            SSL_CTX_free(ssl_ctx_http);
            return false;
        }
        
        struct sockaddr_in serv_addr;
        memset(&serv_addr, 0, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(443);
        memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
        
        if (::connect(sock_http, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
            close(sock_http);
            SSL_CTX_free(ssl_ctx_http);
            return false;
        }
        
        SSL* ssl_http = SSL_new(ssl_ctx_http);
        SSL_set_fd(ssl_http, sock_http);
        
        if (SSL_connect(ssl_http) != 1) {
            SSL_free(ssl_http);
            close(sock_http);
            SSL_CTX_free(ssl_ctx_http);
            return false;
        }
        
        // Send HTTP POST request with JSON body
        std::string request = 
            "POST /matchmake/joinOrCreate/my_room HTTP/1.1\r\n"
            "Host: voice.latticeworks-ai.com\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: 2\r\n"
            "Connection: close\r\n\r\n"
            "{}";
        
        if (SSL_write(ssl_http, request.c_str(), request.length()) <= 0) {
            SSL_free(ssl_http);
            close(sock_http);
            SSL_CTX_free(ssl_ctx_http);
            return false;
        }
        
        // Read HTTP response
        char buffer[4096];
        int bytes_read = SSL_read(ssl_http, buffer, sizeof(buffer) - 1);
        SSL_free(ssl_http);
        close(sock_http);
        SSL_CTX_free(ssl_ctx_http);
        
        if (bytes_read <= 0) return false;
        
        buffer[bytes_read] = '\0';
        std::string response_str(buffer);
        
        // Extract JSON body (skip HTTP headers)
        size_t json_start = response_str.find("\r\n\r\n");
        if (json_start == std::string::npos) return false;
        json_start += 4;
        
        String rawResponse = String(response_str.substr(json_start));
        std::cout << "FL Stream: Raw response: " << rawResponse.toStdString() << std::endl;
        
        // Find the actual JSON part (starts with '{')
        String response = rawResponse.fromFirstOccurrenceOf("{", true, false);
        // Remove any trailing null characters or extra data after the last '}'
        int lastBrace = response.lastIndexOf("}");
        if (lastBrace >= 0) {
            response = response.substring(0, lastBrace + 1);
        }
        
        std::cout << "FL Stream: Cleaned JSON: " << response.toStdString() << std::endl;
        
        // Parse JSON response to get roomId and sessionId
        var roomData;
        std::cout << "FL Stream: About to parse JSON response: '" << response.toStdString() << "'" << std::endl;
        Result parseResult = JSON::parse(response, roomData);
        if (parseResult.failed()) {
            std::cout << "FL Stream: JSON parsing failed: " << parseResult.getErrorMessage().toStdString() << std::endl;
            if (onError) onError("Failed to parse matchmaking response: " + parseResult.getErrorMessage());
            return false;
        }
        std::cout << "FL Stream: JSON parsing successful" << std::endl;
        
        roomId = roomData["room"]["roomId"].toString().toStdString();
        sessionId = roomData["sessionId"].toString().toStdString();
        
        std::cout << "FL Stream: Parsed roomId: '" << roomId << "', sessionId: '" << sessionId << "'" << std::endl;
        
        if (roomId.empty() || sessionId.empty()) {
            std::cout << "FL Stream: ERROR - Empty roomId or sessionId!" << std::endl;
            if (onError) onError("Invalid room reservation - roomId: " + String(roomId) + ", sessionId: " + String(sessionId));
            return false;
        }
        
        if (onLogMessage) onLogMessage("Matchmaking successful - roomId: " + String(roomId) + ", sessionId: " + String(sessionId));
        std::cout << "FL Stream: Matchmaking successful - roomId: " << roomId << ", sessionId: " << sessionId << std::endl;
        
        // Store player ID from session
        {
            const ScopedLock lock(roomStateLock);
            currentPlayerId = String(sessionId);
        }
        
        return true;
        
    } catch (const std::exception& e) {
        if (onError) onError("Matchmaking exception: " + String(e.what()));
        return false;
    }
}

bool ColyseusRoomClient::connectToServer(const std::string& hostname, int port, const std::string& path)
{
    std::cout << "FL Stream: Creating SSL context for WebSocket connection" << std::endl;
    
    // Create SSL context
    ssl_ctx = SSL_CTX_new(TLS_client_method());
    if (!ssl_ctx) {
        std::cerr << "FL Stream: Failed to create SSL context" << std::endl;
        return false;
    }
    
    // Create socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "FL Stream: Failed to create socket" << std::endl;
        return false;
    }
    
    // Resolve hostname
    struct hostent* server = gethostbyname(hostname.c_str());
    if (!server) {
        std::cerr << "Failed to resolve hostname: " << hostname << std::endl;
        close(sock);
        return false;
    }
    
    // Connect to server
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    
    if (::connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        std::cerr << "Failed to connect to " << hostname << ":" << port << std::endl;
        close(sock);
        return false;
    }
    
    // Create SSL connection
    ssl = SSL_new(ssl_ctx);
    SSL_set_fd(ssl, sock);
    
    if (SSL_connect(ssl) != 1) {
        std::cerr << "SSL connection failed" << std::endl;
        SSL_free(ssl);
        close(sock);
        return false;
    }
    
    // Perform WebSocket handshake
    if (!performWebSocketHandshake(hostname, path)) {
        disconnectFromServer();
        return false;
    }
    
    return true;
}

void ColyseusRoomClient::disconnectFromServer()
{
    if (ssl) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
        ssl = nullptr;
    }
    if (sock >= 0) {
        close(sock);
        sock = -1;
    }
}

bool ColyseusRoomClient::performWebSocketHandshake(const std::string& hostname, const std::string& path)
{
    // Use fixed WebSocket key (matches working reference implementation)
    std::string key = "dGhlIHNhbXBsZSBub25jZQ=="; // Base64 encoded key
    
    // Send WebSocket handshake request (exact format from working implementation)
    std::ostringstream request;
    request << "GET " << path << " HTTP/1.1\r\n";
    request << "Host: " << hostname << "\r\n";
    request << "Upgrade: websocket\r\n";
    request << "Connection: Upgrade\r\n";
    request << "Sec-WebSocket-Key: " << key << "\r\n";
    request << "Sec-WebSocket-Version: 13\r\n";
    request << "\r\n";
    
    std::string req_str = request.str();
    
    if (SSL_write(ssl, req_str.c_str(), req_str.length()) <= 0) {
        std::cerr << "FL Stream: Failed to send WebSocket handshake" << std::endl;
        return false;
    }
    
    // Read handshake response
    char response[4096];
    int bytes_read = SSL_read(ssl, response, sizeof(response) - 1);
    if (bytes_read <= 0) {
        std::cerr << "Failed to read WebSocket handshake response" << std::endl;
        return false;
    }
    
    response[bytes_read] = '\0';
    std::string resp_str(response);
    
    std::cout << "FL Stream: WebSocket handshake response: " << resp_str.substr(0, 300) << std::endl;
    
    if (resp_str.find("HTTP/1.1 101") == std::string::npos) {
        std::cerr << "FL Stream: WebSocket handshake failed. Full response: " << resp_str << std::endl;
        if (onError) onError("WebSocket handshake failed - server response: " + String(resp_str.substr(0, 200)));
        return false;
    }
    
    std::cout << "FL Stream: ✓ WebSocket handshake successful" << std::endl;
    return true;
}

bool ColyseusRoomClient::sendBinary(const std::vector<uint8_t>& data)
{
    if (!connected.load() || !ssl) return false;
    
    // WebSocket frame format for binary data
    std::vector<uint8_t> frame;
    frame.push_back(0x82); // FIN=1, opcode=2 (binary)
    
    if (data.size() < 126) {
        frame.push_back(0x80 | static_cast<uint8_t>(data.size())); // MASK=1, payload length
    } else if (data.size() < 65536) {
        frame.push_back(0x80 | 126); // MASK=1, extended payload length
        frame.push_back((data.size() >> 8) & 0xFF);
        frame.push_back(data.size() & 0xFF);
    } else {
        std::cerr << "Payload too large" << std::endl;
        return false;
    }
    
    // Generate mask key
    uint32_t mask_key = rand();
    frame.push_back((mask_key >> 24) & 0xFF);
    frame.push_back((mask_key >> 16) & 0xFF);
    frame.push_back((mask_key >> 8) & 0xFF);
    frame.push_back(mask_key & 0xFF);
    
    // Add masked payload
    for (size_t i = 0; i < data.size(); ++i) {
        uint8_t mask_byte = (mask_key >> (8 * (3 - (i % 4)))) & 0xFF;
        frame.push_back(data[i] ^ mask_byte);
    }
    
    int result = SSL_write(ssl, frame.data(), frame.size());
    return result > 0;
}

bool ColyseusRoomClient::receiveMessages()
{
    if (!ssl) return false;
    
    uint8_t header[2];
    int bytes_read = SSL_read(ssl, header, 2);
    if (bytes_read != 2) {
        if (bytes_read == 0) {
            std::cout << "FL Stream: Connection closed by server" << std::endl;
            return false; // Connection closed
        }
        if (SSL_get_error(ssl, bytes_read) == SSL_ERROR_WANT_READ) return true;
        std::cout << "FL Stream: Error reading WebSocket header, bytes_read: " << bytes_read << std::endl;
        return false;
    }
    
    uint8_t opcode = header[0] & 0x0F;
    bool masked = (header[1] & 0x80) != 0;
    uint64_t payload_len = header[1] & 0x7F;
    
    // Read extended payload length if needed
    if (payload_len == 126) {
        uint8_t len_bytes[2];
        if (SSL_read(ssl, len_bytes, 2) != 2) return false;
        payload_len = (len_bytes[0] << 8) | len_bytes[1];
    } else if (payload_len == 127) {
        uint8_t len_bytes[8];
        if (SSL_read(ssl, len_bytes, 8) != 8) return false;
        payload_len = 0;
        for (int i = 0; i < 8; ++i) {
            payload_len = (payload_len << 8) | len_bytes[i];
        }
    }
    
    // Skip mask key (server messages shouldn't be masked)
    if (masked) {
        uint8_t mask[4];
        if (SSL_read(ssl, mask, 4) != 4) return false;
    }
    
    // Read payload
    if (payload_len > 0 && payload_len < 1000000) { // Reasonable limit
        std::vector<uint8_t> payload(payload_len);
        size_t total_read = 0;
        while (total_read < payload_len) {
            int chunk = SSL_read(ssl, payload.data() + total_read, payload_len - total_read);
            if (chunk <= 0) return false;
            total_read += chunk;
        }
        
        if (opcode == 2) { // Binary frame
            std::cout << "FL Stream: Received binary WebSocket frame, size: " << payload.size() << " bytes" << std::endl;
            if (payload.size() > 0) {
                std::cout << "FL Stream: First byte (protocol): " << static_cast<int>(payload[0]) << std::endl;
            }
            processIncomingData(payload);
        } else if (opcode == 1) { // Text frame
            std::cout << "FL Stream: Received text WebSocket frame (unexpected)" << std::endl;
        } else {
            std::cout << "FL Stream: Received WebSocket frame with opcode: " << static_cast<int>(opcode) << std::endl;
        }
    }
    
    return true;
}

void ColyseusRoomClient::processIncomingData(const std::vector<uint8_t>& data)
{
    handleColyseusMessage(data);
}

void ColyseusRoomClient::sendHandshakeMessage()
{
    // Send HANDSHAKE message first
    std::vector<uint8_t> message;
    message.push_back(ColyseusProtocol::HANDSHAKE);
    
    sendBinary(message);
    
    std::cout << "FL Stream: Sent HANDSHAKE message" << std::endl;
}

void ColyseusRoomClient::joinRoomInternal()
{
    std::cout << "FL Stream: Joining room: my_room" << std::endl;
    
    // Send JOIN_ROOM message exactly like working reference implementation
    std::vector<uint8_t> message;
    message.push_back(ColyseusProtocol::JOIN_ROOM); // Protocol 10
    
    // Try using reserved roomId instead of hardcoded room name
    std::string roomName = roomId.empty() ? "my_room" : roomId;
    message.insert(message.end(), roomName.begin(), roomName.end());
    
    // Debug: print exact message bytes being sent
    std::cout << "FL Stream: JOIN_ROOM message bytes: ";
    for (uint8_t byte : message) {
        std::cout << static_cast<int>(byte) << " ";
    }
    std::cout << std::endl;
    
    bool success = sendBinary(message);
    if (success) {
        std::cout << "FL Stream: ✓ Sent JOIN_ROOM message (exact reference format)" << std::endl;
        if (onLogMessage) onLogMessage("Sent JOIN_ROOM message for room: " + String(roomName));
    } else {
        std::cout << "FL Stream: ✗ Failed to send JOIN_ROOM message" << std::endl;
        if (onError) onError("Failed to send JOIN_ROOM message");
    }
}

void ColyseusRoomClient::handleColyseusMessage(const std::vector<uint8_t>& data)
{
    if (data.empty()) return;
    
    uint8_t protocolCode = data[0];
    
    switch (protocolCode) {
        case ColyseusProtocol::HANDSHAKE: {
            std::cout << "FL Stream: Received HANDSHAKE response from server" << std::endl;
            break;
        }
        
        case ColyseusProtocol::JOIN_ROOM: {
            roomJoined = true;
            if (data.size() > 1) {
                // Extract session ID
                size_t offset = 1;
                if (offset < data.size()) {
                    sessionId = std::string(data.begin() + offset, data.end());
                    const ScopedLock lock(roomStateLock);
                    currentPlayerId = String(sessionId);
                }
            }
            
            // Update player count - at minimum we have ourselves
            connectedUsers = std::max(connectedUsers.load(), 1);
            
            if (onRoomJoined) onRoomJoined(currentRoomName);
            std::cout << "FL Stream: Successfully joined room, sessionId: " << sessionId << std::endl;
            std::cout << "FL Stream: Connected users count: " << connectedUsers.load() << std::endl;
            break;
        }
        
        case ColyseusProtocol::ROOM_DATA: {
            // Handle regular room messages (MsgPack encoded)
            if (onLogMessage) onLogMessage("Received ROOM_DATA message");
            break;
        }
        
        case ColyseusProtocol::ROOM_DATA_BYTES: {
            if (data.size() > 5) {
                // Extract message type and session ID
                std::string senderSessionId = "unknown";
                
                // Audio data starts after type and session info
                std::vector<uint8_t> audioPayload(data.begin() + 5, data.end());
                processAudioMessage(audioPayload, senderSessionId);
            }
            break;
        }
        
        case ColyseusProtocol::ROOM_STATE:
        case ColyseusProtocol::ROOM_STATE_PATCH: {
            std::cout << "FL Stream: Received room state update, size: " << data.size() << " bytes" << std::endl;
            if (data.size() > 1) {
                // For now, assume we have at least ourselves plus any additional data indicates more players
                // This is simplified - proper implementation would decode MsgPack room state
                int estimatedUsers = roomJoined ? std::max(1, static_cast<int>(data.size() > 10 ? 2 : 1)) : 0;
                connectedUsers = estimatedUsers;
                std::cout << "FL Stream: Updated connected users count to: " << connectedUsers.load() << std::endl;
            }
            break;
        }
        
        case ColyseusProtocol::ERROR: {
            std::string errorMessage = "Unknown error";
            if (data.size() > 1) {
                errorMessage = std::string(data.begin() + 1, data.end());
            }
            std::cout << "FL Stream: ✗ Server error: " << errorMessage << std::endl;
            if (onError) onError("Server error: " + String(errorMessage));
            break;
        }
        
        default:
            std::cout << "FL Stream: Unknown protocol code: " << static_cast<int>(protocolCode) << std::endl;
            break;
    }
}

void ColyseusRoomClient::processAudioMessage(const std::vector<uint8_t>& payload, const std::string& senderSessionId)
{
    // Convert binary audio data to float samples
    std::vector<float> audioData;
    audioData.reserve(payload.size() / sizeof(float));
    
    // Simple conversion - in production, would decode WebM/Opus format
    for (size_t i = 0; i + sizeof(float) <= payload.size(); i += sizeof(float)) {
        float sample;
        std::memcpy(&sample, &payload[i], sizeof(float));
        audioData.push_back(sample);
    }
    
    // Add to thread-safe queue
    AudioMessage message;
    message.audioData = std::move(audioData);
    message.sessionId = senderSessionId;
    message.timestamp = juce::Time::getMillisecondCounterHiRes();
    
    std::lock_guard<std::mutex> lock(audioQueueMutex);
    incomingAudioQueue.push(std::move(message));
}

void ColyseusRoomClient::sendPushToTalk(bool isPushing)
{
    if (!connected.load() || !roomJoined.load()) return;
    
    std::vector<uint8_t> message;
    message.push_back(ColyseusProtocol::ROOM_DATA);
    
    std::string messageType = "push";
    message.insert(message.end(), messageType.begin(), messageType.end());
    
    sendBinary(message);
    
    std::cout << "FL Stream: Sent push-to-talk: " << (isPushing ? "ON" : "OFF") << std::endl;
}

void ColyseusRoomClient::sendAudioData(const std::vector<float>& audioBuffer)
{
    if (!connected.load() || !roomJoined.load() || audioBuffer.empty()) return;
    
    std::vector<uint8_t> message;
    message.push_back(ColyseusProtocol::ROOM_DATA_BYTES);
    
    std::string messageType = "talk";
    message.insert(message.end(), messageType.begin(), messageType.end());
    
    // Convert float samples to bytes
    const uint8_t* audioBytes = reinterpret_cast<const uint8_t*>(audioBuffer.data());
    size_t audioSize = audioBuffer.size() * sizeof(float);
    message.insert(message.end(), audioBytes, audioBytes + audioSize);
    
    sendBinary(message);
}

bool ColyseusRoomClient::getNextAudioMessage(AudioMessage& message)
{
    std::lock_guard<std::mutex> lock(audioQueueMutex);
    if (incomingAudioQueue.empty()) return false;
    
    message = std::move(incomingAudioQueue.front());
    incomingAudioQueue.pop();
    return true;
}