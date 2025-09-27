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
        if (onLogMessage) onLogMessage("Executing matchmaking-WebSocket connection sequence");
        std::cout << "FL Stream: Starting matchmaking-WebSocket connection sequence" << std::endl;
        
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
        
        // Send JOIN_ROOM immediately to claim the reserved seat
        joinRoomInternal();
        
        // WebSocket message processing loop with minimal sleep for real-time performance
        while (!shouldStop.load() && connected.load()) {
            if (!receiveMessages()) {
                break;
            }
            // Use Thread::yield() instead of sleep for better real-time performance
            juce::Thread::yield();
        }
    }
    catch (const std::exception& e) {
        if (onError) onError("Atomic connection exception: " + String(e.what()));
        std::cout << "FL Stream: Atomic connection exception: " << e.what() << std::endl;
    }
    
    connected = false;
    if (onRoomLeft) onRoomLeft(currentRoomName);
}

bool ColyseusRoomClient::performMatchmaking()
{
    try {
        if (onLogMessage) onLogMessage("Performing HTTP matchmaking...");
        std::cout << "FL Stream: Performing HTTP matchmaking for room: " << currentRoomName.toStdString() << std::endl;
        
        // Create persistent SSL context for both matchmaking and WebSocket
        ssl_ctx = SSL_CTX_new(TLS_client_method());
        if (!ssl_ctx) return false;
        
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            SSL_CTX_free(ssl_ctx);
            ssl_ctx = nullptr;
            return false;
        }
        
        struct hostent* server = gethostbyname("voice.latticeworks-ai.com");
        if (!server) {
            std::cout << "FL Stream: DNS resolution failed for voice.latticeworks-ai.com" << std::endl;
            close(sock);
            SSL_CTX_free(ssl_ctx);
            ssl_ctx = nullptr;
            sock = -1;
            return false;
        }
        std::cout << "FL Stream: DNS resolved successfully" << std::endl;
        
        struct sockaddr_in serv_addr;
        memset(&serv_addr, 0, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(443);
        memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
        
        if (::connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
            std::cout << "FL Stream: TCP connection failed to voice.latticeworks-ai.com:443" << std::endl;
            close(sock);
            SSL_CTX_free(ssl_ctx);
            ssl_ctx = nullptr;
            sock = -1;
            return false;
        }
        std::cout << "FL Stream: TCP connection established" << std::endl;
        
        ssl = SSL_new(ssl_ctx);
        SSL_set_fd(ssl, sock);
        
        if (SSL_connect(ssl) != 1) {
            std::cout << "FL Stream: SSL handshake failed" << std::endl;
            SSL_free(ssl);
            close(sock);
            SSL_CTX_free(ssl_ctx);
            ssl = nullptr;
            ssl_ctx = nullptr;
            sock = -1;
            return false;
        }
        std::cout << "FL Stream: SSL handshake successful" << std::endl;
        
        std::cout << "FL Stream: SSL connection established, sending matchmaking request" << std::endl;
        
        // Send HTTP POST request with persistent connection (no WebSocket headers yet)
        std::string request = 
            "POST /matchmake/joinOrCreate/my_room HTTP/1.1\r\n"
            "Host: voice.latticeworks-ai.com\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: 2\r\n"
            "Connection: keep-alive\r\n\r\n"
            "{}";
        
        if (SSL_write(ssl, request.c_str(), request.length()) <= 0) {
            disconnectFromServer();
            return false;
        }
        
        // Read HTTP response
        char buffer[4096];
        int bytes_read = SSL_read(ssl, buffer, sizeof(buffer) - 1);
        
        if (bytes_read <= 0) {
            disconnectFromServer();
            return false;
        }
        
        buffer[bytes_read] = '\0';
        std::string response_str(buffer);
        
        // Extract JSON body
        size_t json_start = response_str.find("\r\n\r\n");
        if (json_start == std::string::npos) {
            disconnectFromServer();
            return false;
        }
        json_start += 4;
        
        String rawResponse = String(response_str.substr(json_start));
        String response = rawResponse.fromFirstOccurrenceOf("{", true, false);
        int lastBrace = response.lastIndexOf("}");
        if (lastBrace >= 0) {
            response = response.substring(0, lastBrace + 1);
        }
        
        std::cout << "FL Stream: Matchmaking response: " << response.toStdString() << std::endl;
        
        // Parse JSON response
        var roomData;
        Result parseResult = JSON::parse(response, roomData);
        if (parseResult.failed()) {
            std::cout << "FL Stream: JSON parsing failed: " << parseResult.getErrorMessage().toStdString() << std::endl;
            if (onError) onError("Failed to parse matchmaking response: " + parseResult.getErrorMessage());
            disconnectFromServer();
            return false;
        }
        
        roomId = roomData["room"]["roomId"].toString().toStdString();
        sessionId = roomData["sessionId"].toString().toStdString();
        
        if (roomId.empty() || sessionId.empty()) {
            std::cout << "FL Stream: ERROR - Empty roomId or sessionId!" << std::endl;
            if (onError) onError("Invalid room reservation");
            disconnectFromServer();
            return false;
        }
        
        std::cout << "FL Stream: Matchmaking SUCCESS - roomId: " << roomId << ", sessionId: " << sessionId << std::endl;
        
        // Close HTTP connection - we'll open a fresh WebSocket connection next
        disconnectFromServer();
        
        {
            const ScopedLock lock(roomStateLock);
            currentPlayerId = String(sessionId);
        }
        
        if (onLogMessage) onLogMessage("HTTP matchmaking successful - roomId: " + String(roomId));
        return true;
        
    } catch (const std::exception& e) {
        if (onError) onError("Atomic connection exception: " + String(e.what()));
        disconnectFromServer();
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
    
    std::cout << "FL Stream: [OK] WebSocket handshake successful" << std::endl;
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
    
    // Check for data availability first to avoid blocking
    int ssl_pending = SSL_pending(ssl);
    if (ssl_pending == 0) {
        // Try a non-blocking read to see if there's any data
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        struct timeval timeout = {0, 0}; // Non-blocking
        
        int select_result = select(sock + 1, &readfds, nullptr, nullptr, &timeout);
        if (select_result <= 0) {
            // No data available, return to avoid blocking
            return true;
        }
    }
    
    std::cout << "FL Stream: SSL data available, " << ssl_pending << " bytes pending" << std::endl;
    
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
    
    // Read payload with timeout protection
    if (payload_len > 0 && payload_len < 1000000) { // Reasonable limit
        std::vector<uint8_t> payload(payload_len);
        size_t total_read = 0;
        int read_attempts = 0;
        const int max_read_attempts = 100; // Prevent infinite blocking
        
        while (total_read < payload_len && read_attempts < max_read_attempts) {
            int chunk = SSL_read(ssl, payload.data() + total_read, payload_len - total_read);
            if (chunk <= 0) {
                int ssl_error = SSL_get_error(ssl, chunk);
                if (ssl_error == SSL_ERROR_WANT_READ) {
                    read_attempts++;
                    juce::Thread::yield(); // Brief yield instead of blocking
                    continue;
                }
                return false;
            }
            total_read += chunk;
            read_attempts = 0; // Reset counter on successful read
        }
        
        if (total_read < payload_len) {
            std::cout << "FL Stream: Payload read timeout, got " << total_read << "/" << payload_len << " bytes" << std::endl;
            return false;
        }
        
        if (opcode == 2) { // Binary frame
            std::cout << "FL Stream: Received binary WebSocket frame, size: " << payload.size() << " bytes" << std::endl;
            if (payload.size() > 0) {
                std::cout << "FL Stream: Protocol byte: " << static_cast<int>(payload[0]) << std::endl;
                std::cout << "FL Stream: Full message bytes: ";
                for (size_t i = 0; i < std::min(payload.size(), static_cast<size_t>(20)); ++i) {
                    std::cout << static_cast<int>(payload[i]) << " ";
                }
                if (payload.size() > 20) std::cout << "...";
                std::cout << std::endl;
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
    std::cout << "FL Stream: Joining room: " << currentRoomName.toStdString() << std::endl;
    
    // The JOIN_ROOM message should be sent to the WebSocket path that already contains the room info
    // So we just send a simple JOIN_ROOM protocol message without room name
    std::vector<uint8_t> message;
    message.push_back(ColyseusProtocol::JOIN_ROOM); // Protocol 10
    
    // Based on JavaScript client: JOIN_ROOM message may contain options (empty object for now)
    // Add empty MsgPack object {} = 0x80
    message.push_back(0x80); // MsgPack empty map
    
    // Debug: print exact message bytes being sent
    std::cout << "FL Stream: JOIN_ROOM message bytes: ";
    for (uint8_t byte : message) {
        std::cout << static_cast<int>(byte) << " ";
    }
    std::cout << std::endl;
    std::cout << "FL Stream: Sending JOIN_ROOM to WebSocket path with roomId: " << roomId << std::endl;
    
    bool success = sendBinary(message);
    if (success) {
        std::cout << "FL Stream: [OK] Sent JOIN_ROOM message (protocol + empty options)" << std::endl;
        if (onLogMessage) onLogMessage("Sent JOIN_ROOM message to room: " + currentRoomName);
    } else {
        std::cout << "FL Stream: [ERROR] Failed to send JOIN_ROOM message" << std::endl;
        if (onError) onError("Failed to send JOIN_ROOM message");
    }
}

void ColyseusRoomClient::handleColyseusMessage(const std::vector<uint8_t>& data)
{
    if (data.empty()) return;
    
    uint8_t protocolCode = data[0];
    std::cout << "FL Stream: Processing protocol " << static_cast<int>(protocolCode) << ", data size: " << data.size() << std::endl;
    
    switch (protocolCode) {
        case ColyseusProtocol::HANDSHAKE: {
            std::cout << "FL Stream: Received HANDSHAKE response from server" << std::endl;
            
            // Server acknowledged handshake, now we can proceed with room operations
            if (onLogMessage) onLogMessage("Server handshake acknowledged");
            break;
        }
        
        case ColyseusProtocol::JOIN_ROOM: {
            std::cout << "FL Stream: Received JOIN_ROOM response" << std::endl;
            roomJoined = true;
            
            // Parse JOIN_ROOM response using Colyseus string decoding
            size_t offset = 1;
            
            // Extract reconnection token (Colyseus string format)
            std::string reconToken;
            if (offset < data.size()) {
                reconToken = decodeColyseusString(data, offset);
                std::cout << "FL Stream: Reconnection token: " << reconToken << std::endl;
            }
            
            // Extract serializer ID (Colyseus string format)
            std::string serializerId;
            if (offset < data.size()) {
                serializerId = decodeColyseusString(data, offset);
                std::cout << "FL Stream: Serializer ID: " << serializerId << std::endl;
            }
            
            // Store full reconnection token
            if (!reconToken.empty()) {
                reconnectionToken = roomId + ":" + reconToken;
            }
            
            const ScopedLock lock(roomStateLock);
            currentPlayerId = String(sessionId);
            
            // Send JOIN_ROOM acknowledgment (required by Colyseus protocol)
            std::vector<uint8_t> ackMessage;
            ackMessage.push_back(ColyseusProtocol::JOIN_ROOM);
            sendBinary(ackMessage);
            std::cout << "FL Stream: Sent JOIN_ROOM acknowledgment" << std::endl;
            
            // Update player count - at minimum we have ourselves
            connectedUsers = std::max(connectedUsers.load(), 1);
            
            if (onRoomJoined) onRoomJoined(currentRoomName);
            std::cout << "FL Stream: Successfully joined room, sessionId: " << sessionId << std::endl;
            std::cout << "FL Stream: Reconnection token: " << reconnectionToken << std::endl;
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
            
            // Process room state to extract user information
            if (data.size() > 1) {
                processRoomState(std::vector<uint8_t>(data.begin() + 1, data.end()), 
                                protocolCode == ColyseusProtocol::ROOM_STATE_PATCH);
            }
            break;
        }
        
        case ColyseusProtocol::ERROR: {
            std::string errorMessage = "Unknown error";
            if (data.size() > 1) {
                errorMessage = std::string(data.begin() + 1, data.end());
            }
            std::cout << "FL Stream: [ERROR] Server error: " << errorMessage << std::endl;
            if (onError) onError("Server error: " + String(errorMessage));
            break;
        }
        
        default:
            std::cout << "FL Stream: Unknown protocol code: " << static_cast<int>(protocolCode) 
                      << ", message size: " << data.size() << " bytes" << std::endl;
            
            // Debug: show first few bytes of unknown messages
            std::cout << "FL Stream: Unknown message bytes: ";
            for (size_t i = 0; i < std::min(data.size(), static_cast<size_t>(10)); ++i) {
                std::cout << static_cast<int>(data[i]) << " ";
            }
            std::cout << std::endl;
            break;
    }
}

void ColyseusRoomClient::processAudioMessage(const std::vector<uint8_t>& payload, const std::string& senderSessionId)
{
    // Convert binary audio data to float samples
    std::vector<float> audioData;
    audioData.reserve(payload.size() / sizeof(float));
    
    // Convert binary audio data to float samples for JUCE AudioBuffer
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

void ColyseusRoomClient::processRoomState(const std::vector<uint8_t>& stateData, bool isPatch)
{
    std::cout << "FL Stream: Processing room state (" << (isPatch ? "patch" : "full") << "), " 
              << stateData.size() << " bytes" << std::endl;
    
    // Basic room state parsing - look for user count patterns
    // This is a simplified approach since full MsgPack decoding would require additional libraries
    parseUserListFromState(stateData);
}

void ColyseusRoomClient::parseUserListFromState(const std::vector<uint8_t>& stateData)
{
    // Simplified user detection based on common MsgPack patterns
    int detectedUsers = 1; // Always count ourselves
    
    // Look for patterns that indicate multiple users
    // MsgPack maps start with 0x80-0x8f for fixmap or 0xde/0xdf for larger maps
    // Arrays start with 0x90-0x9f for fixarray or 0xdc/0xdd for larger arrays
    
    for (size_t i = 0; i < stateData.size(); ++i) {
        uint8_t byte = stateData[i];
        
        // Check for MsgPack map indicators (potential user objects)
        if ((byte >= 0x80 && byte <= 0x8f) || byte == 0xde || byte == 0xdf) {
            // Found a map structure - could be user data
            int mapSize = 0;
            if (byte >= 0x80 && byte <= 0x8f) {
                mapSize = byte & 0x0f; // Extract size from fixmap
            }
            
            // If we find multiple maps, likely multiple users
            if (mapSize > 0) {
                detectedUsers++;
            }
        }
        
        // Look for sessionId-like patterns (strings with reasonable length)
        if (byte >= 0xa0 && byte <= 0xbf) { // fixstr
            int strLen = byte & 0x1f;
            if (strLen >= 8 && strLen <= 12) { // Typical sessionId length
                // This could be a sessionId, indicating another user
                if (i + strLen < stateData.size()) {
                    std::string possibleSessionId(stateData.begin() + i + 1, 
                                                stateData.begin() + i + 1 + strLen);
                    
                    // Check if it looks like a sessionId (alphanumeric)
                    bool isSessionId = true;
                    for (char c : possibleSessionId) {
                        if (!std::isalnum(c)) {
                            isSessionId = false;
                            break;
                        }
                    }
                    
                    if (isSessionId && possibleSessionId != sessionId) {
                        detectedUsers = std::max(detectedUsers, 2);
                        
                        // Add to user list if not already present
                        const ScopedLock lock(userListLock);
                        String juceSid = String(possibleSessionId);
                        if (!userList.contains(juceSid)) {
                            userList.add(juceSid);
                            std::cout << "FL Stream: Detected user: " << possibleSessionId << std::endl;
                            
                            if (onUserJoined) {
                                onUserJoined(juceSid, "User_" + juceSid.substring(0, 4));
                            }
                        }
                    }
                    i += strLen; // Skip the string content
                }
            }
        }
    }
    
    // Update connected users count
    int previousCount = connectedUsers.load();
    connectedUsers = std::max(detectedUsers, 1);
    
    if (connectedUsers.load() != previousCount) {
        std::cout << "FL Stream: User count updated from " << previousCount 
                  << " to " << connectedUsers.load() << std::endl;
    }
}

std::string ColyseusRoomClient::decodeColyseusString(const std::vector<uint8_t>& data, size_t& offset)
{
    if (offset >= data.size()) return "";
    
    // Colyseus uses variable-length string encoding
    // First, check if it's a string length indicator
    uint8_t firstByte = data[offset];
    
    size_t stringLength = 0;
    
    if (firstByte <= 0x7F) {
        // Single byte length (0-127)
        stringLength = firstByte;
        offset++;
    } else if (firstByte == 0xc4) {
        // String with 1-byte length prefix
        if (offset + 1 < data.size()) {
            stringLength = data[offset + 1];
            offset += 2;
        } else {
            return "";
        }
    } else if (firstByte == 0xc5) {
        // String with 2-byte length prefix
        if (offset + 2 < data.size()) {
            stringLength = (data[offset + 1] << 8) | data[offset + 2];
            offset += 3;
        } else {
            return "";
        }
    } else {
        // Try null-terminated string parsing as fallback
        size_t start = offset;
        while (offset < data.size() && data[offset] != 0) {
            offset++;
        }
        stringLength = offset - start;
        if (offset < data.size()) offset++; // Skip null terminator
        
        if (stringLength > 0) {
            return std::string(data.begin() + start, data.begin() + start + stringLength);
        }
        return "";
    }
    
    // Extract the string content
    if (offset + stringLength <= data.size() && stringLength > 0) {
        std::string result(data.begin() + offset, data.begin() + offset + stringLength);
        offset += stringLength;
        return result;
    }
    
    return "";
}