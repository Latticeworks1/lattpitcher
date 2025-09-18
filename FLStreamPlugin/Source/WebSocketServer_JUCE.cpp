#include "WebSocketServer.h"
#include <juce_core/juce_core.h>
#include <random>

// Minimal SHA-1 implementation for WebSocket handshake and a client-side frame encoder
namespace {
    struct SHA1Ctx {
        uint32_t h[5];
        uint64_t messageLengthBits = 0;
        uint8_t buffer[64];
        size_t bufferSize = 0;
    };

    inline uint32_t rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

    void sha1Init(SHA1Ctx& ctx) {
        ctx.h[0] = 0x67452301u;
        ctx.h[1] = 0xEFCDAB89u;
        ctx.h[2] = 0x98BADCFEu;
        ctx.h[3] = 0x10325476u;
        ctx.h[4] = 0xC3D2E1F0u;
        ctx.messageLengthBits = 0;
        ctx.bufferSize = 0;
    }

    void sha1ProcessBlock(SHA1Ctx& ctx, const uint8_t* block) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = (uint32_t(block[i * 4 + 0]) << 24) |
                   (uint32_t(block[i * 4 + 1]) << 16) |
                   (uint32_t(block[i * 4 + 2]) << 8)  |
                   (uint32_t(block[i * 4 + 3]));
        }
        for (int i = 16; i < 80; ++i)
            w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

        uint32_t a = ctx.h[0], b = ctx.h[1], c = ctx.h[2], d = ctx.h[3], e = ctx.h[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20)      { f = (b & c) | ((~b) & d); k = 0x5A827999u; }
            else if (i < 40) { f = b ^ c ^ d;             k = 0x6ED9EBA1u; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
            else             { f = b ^ c ^ d;             k = 0xCA62C1D6u; }
            uint32_t temp = rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol(b, 30); b = a; a = temp;
        }
        ctx.h[0] += a; ctx.h[1] += b; ctx.h[2] += c; ctx.h[3] += d; ctx.h[4] += e;
    }

    void sha1Update(SHA1Ctx& ctx, const void* data, size_t len) {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        ctx.messageLengthBits += uint64_t(len) * 8ull;
        while (len > 0) {
            size_t copy = juce::jmin(len, 64 - ctx.bufferSize);
            memcpy(ctx.buffer + ctx.bufferSize, p, copy);
            ctx.bufferSize += copy; p += copy; len -= copy;
            if (ctx.bufferSize == 64) {
                sha1ProcessBlock(ctx, ctx.buffer);
                ctx.bufferSize = 0;
            }
        }
    }

    void sha1Final(SHA1Ctx& ctx, uint8_t out[20]) {
        // Pad
        uint8_t pad = 0x80;
        sha1Update(ctx, &pad, 1);
        uint8_t zero = 0x00;
        while (ctx.bufferSize != 56) {
            if (ctx.bufferSize == 64) {
                sha1ProcessBlock(ctx, ctx.buffer);
                ctx.bufferSize = 0;
            }
            sha1Update(ctx, &zero, 1);
        }
        // Append length (big-endian 64-bit)
        uint8_t lenBytes[8];
        for (int i = 0; i < 8; ++i)
            lenBytes[7 - i] = uint8_t((ctx.messageLengthBits >> (i * 8)) & 0xFF);
        sha1Update(ctx, lenBytes, 8);
        if (ctx.bufferSize == 64) {
            sha1ProcessBlock(ctx, ctx.buffer);
            ctx.bufferSize = 0;
        }
        // Output
        for (int i = 0; i < 5; ++i) {
            out[i * 4 + 0] = uint8_t((ctx.h[i] >> 24) & 0xFF);
            out[i * 4 + 1] = uint8_t((ctx.h[i] >> 16) & 0xFF);
            out[i * 4 + 2] = uint8_t((ctx.h[i] >> 8) & 0xFF);
            out[i * 4 + 3] = uint8_t((ctx.h[i]) & 0xFF);
        }
    }

    juce::String websocketAcceptKey(const juce::String& clientKey) {
        static const char* kGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        juce::String combined = clientKey + kGuid;
        SHA1Ctx ctx; sha1Init(ctx);
        auto utf8 = combined.toRawUTF8();
        sha1Update(ctx, utf8, strlen(utf8));
        uint8_t digest[20];
        sha1Final(ctx, digest);
        return juce::Base64::toBase64(digest, 20);
    }

    // Encode a client WebSocket frame (masked) for sending
    juce::MemoryBlock encodeWebSocketFrameClient(uint8_t opcode, const void* payload, size_t size) {
        juce::MemoryOutputStream os;
        const uint8_t finOpcode = 0x80 | (opcode & 0x0F);
        os.writeByte((char)finOpcode);
        // Client frames MUST be masked
        if (size < 126) {
            os.writeByte((char)(0x80 | uint8_t(size)));
        } else if (size < 65536) {
            os.writeByte((char)(0x80 | 126));
            os.writeShort((short)size);
        } else {
            os.writeByte((char)(0x80 | 127));
            os.writeInt64((int64_t)size);
        }
        // Masking key
        std::mt19937 rng{ std::random_device{}() };
        uint32_t maskingKey = rng();
        os.writeInt((int)maskingKey);
        // Mask payload
        juce::MemoryBlock buf(payload, size);
        auto* data = static_cast<uint8_t*>(buf.getData());
        auto* key = reinterpret_cast<uint8_t*>(&maskingKey);
        for (size_t i = 0; i < size; ++i)
            data[i] ^= key[i % 4];
        os.write(buf.getData(), buf.getSize());
        return os.getMemoryBlock();
    }
}

using namespace juce;

//==============================================================================
// JUCE-based WebSocket Implementation (Production Ready)
//==============================================================================

// Simple WebSocket frame structure
struct WebSocketFrame {
    enum OpCode : uint8_t {
        CONTINUATION = 0x0,
        TEXT = 0x1,
        BINARY = 0x2,
        CLOSE = 0x8,
        PING = 0x9,
        PONG = 0xA
    };
    
    uint8_t opcode;
    bool fin = true;
    bool masked = false;
    uint64_t payloadLength = 0;
    uint32_t maskingKey = 0;
    MemoryBlock payload;
    
    MemoryBlock encode() const {
        MemoryOutputStream stream;
        
        // First byte: FIN (1) + RSV (3) + OpCode (4)
        uint8_t firstByte = (fin ? 0x80 : 0x00) | (opcode & 0x0F);
        stream.writeByte(firstByte);
        
        // Payload length
        if (payloadLength < 126) {
            stream.writeByte(static_cast<uint8_t>(payloadLength));
        } else if (payloadLength < 65536) {
            stream.writeByte(126);
            stream.writeShort(static_cast<uint16_t>(payloadLength));
        } else {
            stream.writeByte(127);
            stream.writeInt64(static_cast<int64_t>(payloadLength));
        }
        
        // Payload
        if (payload.getSize() > 0) {
            stream.write(payload.getData(), payload.getSize());
        }
        
        return stream.getMemoryBlock();
    }
    
    static WebSocketFrame decode(const MemoryBlock& data) {
        WebSocketFrame frame;
        MemoryInputStream stream(data, false);
        
        if (data.getSize() < 2) return frame;
        
        uint8_t firstByte = stream.readByte();
        frame.fin = (firstByte & 0x80) != 0;
        frame.opcode = firstByte & 0x0F;
        
        uint8_t secondByte = stream.readByte();
        frame.masked = (secondByte & 0x80) != 0;
        frame.payloadLength = secondByte & 0x7F;
        
        if (frame.payloadLength == 126) {
            frame.payloadLength = stream.readShort();
        } else if (frame.payloadLength == 127) {
            frame.payloadLength = stream.readInt64();
        }
        
        if (frame.masked) {
            frame.maskingKey = stream.readInt();
        }
        
        // Read payload
        size_t remaining = static_cast<size_t>(frame.payloadLength);
        if (remaining > 0 && stream.getNumBytesRemaining() >= remaining) {
            frame.payload.setSize(remaining);
            stream.read(frame.payload.getData(), remaining);
            
            // Unmask if needed
            if (frame.masked) {
                uint8_t* data = static_cast<uint8_t*>(frame.payload.getData());
                uint8_t* mask = reinterpret_cast<uint8_t*>(&frame.maskingKey);
                for (size_t i = 0; i < remaining; ++i) {
                    data[i] ^= mask[i % 4];
                }
            }
        }
        
        return frame;
    }
};

// WebSocket connection wrapper around JUCE socket
class WebSocketConnection {
public:
    WebSocketConnection(std::unique_ptr<StreamingSocket> socket, const String& userId)
        : socket(std::move(socket)), userId(userId), connected(true) {
        startReceiveThread();
    }
    
    ~WebSocketConnection() {
        connected = false;
        if (receiveThread.joinable()) {
            if (std::this_thread::get_id() == receiveThread.get_id())
                receiveThread.detach();
            else
                receiveThread.join();
        }
    }
    
    bool sendText(const String& message) {
        WebSocketFrame frame;
        frame.opcode = WebSocketFrame::TEXT;
        frame.payloadLength = message.getNumBytesAsUTF8();
        frame.payload.setSize(frame.payloadLength);
        message.copyToUTF8(static_cast<char*>(frame.payload.getData()), frame.payloadLength + 1);
        
        return sendFrame(frame);
    }
    
    bool sendBinary(const MemoryBlock& data) {
        WebSocketFrame frame;
        frame.opcode = WebSocketFrame::BINARY;
        frame.payloadLength = data.getSize();
        frame.payload = data;
        
        return sendFrame(frame);
    }
    
    String getUserId() const { return userId; }
    bool isConnected() const { return connected.load(); }
    
    std::function<void(const String&)> onTextMessage;
    std::function<void(const MemoryBlock&)> onBinaryMessage;
    std::function<void()> onDisconnected;
    
private:
    std::unique_ptr<StreamingSocket> socket;
    String userId;
    std::atomic<bool> connected{true};
    std::thread receiveThread;
    
    bool sendFrame(const WebSocketFrame& frame) {
        if (!connected.load()) return false;
        
        MemoryBlock encoded = frame.encode();
        int result = socket->write(encoded.getData(), static_cast<int>(encoded.getSize()));
        return result == static_cast<int>(encoded.getSize());
    }
    
    void startReceiveThread() {
        receiveThread = std::thread([this]() {
            uint8_t buffer[8192];
            
            while (connected.load()) {
                if (!socket->waitUntilReady(true, 50)) {
                    continue;
                }
                int bytesRead = socket->read(buffer, sizeof(buffer), false);
                if (bytesRead <= 0) {
                    // Treat <=0 as disconnect only after readiness
                    connected = false;
                    if (onDisconnected) onDisconnected();
                    break;
                }
                
                MemoryBlock data(buffer, (size_t)bytesRead);
                WebSocketFrame frame = WebSocketFrame::decode(data);
                
                if (frame.opcode == WebSocketFrame::TEXT) {
                    String message = String::fromUTF8(static_cast<const char*>(frame.payload.getData()),
                                                    static_cast<int>(frame.payload.getSize()));
                    if (onTextMessage) onTextMessage(message);
                }
                else if (frame.opcode == WebSocketFrame::BINARY) {
                    if (onBinaryMessage) onBinaryMessage(frame.payload);
                }
                else if (frame.opcode == WebSocketFrame::PING) {
                    // Respond with pong
                    WebSocketFrame pong;
                    pong.opcode = WebSocketFrame::PONG;
                    pong.payload = frame.payload;
                    pong.payloadLength = frame.payloadLength;
                    sendFrame(pong);
                }
            }
        });
    }
};

FLStreamWebSocketServer::FLStreamWebSocketServer()
{
    logMessage("FL Stream WebSocket Server initialized (JUCE native implementation)");
}

FLStreamWebSocketServer::~FLStreamWebSocketServer()
{
    stopServer();
}

bool FLStreamWebSocketServer::startServer(int port)
{
    if (serverRunning.load()) {
        logMessage("Server already running on port " + String(port));
        return true;
    }
    
    logMessage("Starting JUCE WebSocket server on port " + String(port));
    
    try {
        serverSocket = std::make_unique<StreamingSocket>();
        if (!serverSocket->createListener(port)) {
            logMessage("Failed to create listener on port " + String(port));
            return false;
        }
        
        serverRunning.store(true);
        
        serverThread = std::make_unique<std::thread>([this, port]() {
            runServer(port);
        });
        
        Thread::sleep(100);
        
        if (serverRunning.load()) {
            logMessage("JUCE WebSocket server started successfully on port " + String(port));
            return true;
        }
    }
    catch (const std::exception& e) {
        logMessage("Failed to start server: " + String(e.what()));
    }
    
    return false;
}

void FLStreamWebSocketServer::stopServer()
{
    if (!serverRunning.load()) return;
    
    logMessage("Stopping JUCE WebSocket server...");
    serverRunning.store(false);
    
    if (serverSocket) {
        serverSocket->close();
        serverSocket.reset();
    }
    
    if (serverThread && serverThread->joinable()) {
        serverThread->join();
        serverThread.reset();
    }
    
    {
        const ScopedLock lock(usersMutex);
        connections.clear();
        users.clear();
        roomUsers.clear();
    }
    
    logMessage("JUCE WebSocket server stopped");
}

void FLStreamWebSocketServer::runServer(int port)
{
    logMessage("JUCE WebSocket server thread started on port " + String(port));
    
    while (serverRunning.load()) {
        auto clientSocket = std::unique_ptr<StreamingSocket>(serverSocket->waitForNextConnection());
        if (clientSocket && serverRunning.load()) {
            handleNewConnection(std::move(clientSocket));
        }
        
        Thread::sleep(10); // Small delay to prevent busy waiting
    }
    
    logMessage("JUCE WebSocket server thread stopped");
}

void FLStreamWebSocketServer::handleNewConnection(std::unique_ptr<StreamingSocket> clientSocket)
{
    // Perform WebSocket handshake
    String handshakeRequest = readHttpRequest(clientSocket.get());
    if (!performWebSocketHandshake(clientSocket.get(), handshakeRequest)) {
        logMessage("WebSocket handshake failed");
        return;
    }
    
    // Generate unique user ID
    String userId = "user_" + String::toHexString(Random::getSystemRandom().nextInt64());
    
    // Create WebSocket connection
    auto connection = std::make_unique<WebSocketConnection>(std::move(clientSocket), userId);
    
    // Set up event handlers
    connection->onTextMessage = [this, userId](const String& message) {
        handleTextMessage(userId, message);
    };
    
    connection->onBinaryMessage = [this, userId](const MemoryBlock& data) {
        handleBinaryMessage(userId, data);
    };
    
    connection->onDisconnected = [this, userId]() {
        handleUserDisconnection(userId);
    };
    
    // Add to connections
    {
        const ScopedLock lock(usersMutex);
        connections[userId] = std::move(connection);
        
        // Add user to default room
        String roomId = "default-room";
        auto user = std::make_unique<UserSession>(userId, roomId, "listener");
        users[userId] = std::move(user);
        roomUsers[roomId].push_back(userId);
    }
    
    logMessage("User connected: " + userId);
    
    if (onUserJoined) {
        onUserJoined(userId, "default-room");
    }
    
    // Skip welcome message for now to debug connection issues
    logMessage("User connection established, skipping welcome message");
}

String FLStreamWebSocketServer::readHttpRequest(StreamingSocket* socket)
{
    String request;
    char buffer[1024];
    const int maxWaitMs = 500; // short timeout
    int waited = 0;
    while (waited < maxWaitMs && !request.contains("\r\n\r\n")) {
        if (socket->waitUntilReady(true, 50)) {
            int bytesRead = socket->read(buffer, (int)sizeof(buffer) - 1, false);
            if (bytesRead > 0) {
                buffer[bytesRead] = '\0';
                request += String(buffer);
                if (request.contains("\r\n\r\n")) break;
            } else if (bytesRead < 0) {
                break;
            }
        } else {
            waited += 50;
        }
    }
    
    return request;
}

bool FLStreamWebSocketServer::performWebSocketHandshake(StreamingSocket* socket, const String& request)
{
    // Extract WebSocket key
    String wsKey = extractWebSocketKey(request);
    if (wsKey.isEmpty()) return false;
    
    // Generate response key
    String responseKey = generateWebSocketResponseKey(wsKey);
    
    // Send handshake response
    String response = 
        "HTTP/1.1 101 Switching Protocols\\r\\n"
        "Upgrade: websocket\\r\\n"
        "Connection: Upgrade\\r\\n"
        "Sec-WebSocket-Accept: " + responseKey + "\\r\\n"
        "\\r\\n";
    
    return socket->write(response.toRawUTF8(), response.getNumBytesAsUTF8()) > 0;
}

String FLStreamWebSocketServer::extractWebSocketKey(const String& request)
{
    StringArray lines = StringArray::fromLines(request);
    for (const String& line : lines) {
        if (line.startsWithIgnoreCase("Sec-WebSocket-Key:")) {
            return line.substring(18).trim();
        }
    }
    return {};
}

String FLStreamWebSocketServer::generateWebSocketResponseKey(const String& clientKey)
{
    // RFC 6455: Sec-WebSocket-Accept = Base64(SHA1(clientKey + GUID))
    return websocketAcceptKey(clientKey);
}

void FLStreamWebSocketServer::handleTextMessage(const String& userId, const String& message)
{
    logMessage("Received text from " + userId + ": " + message.substring(0, 100));
    
    // Parse JSON message
    var jsonData = JSON::parse(message);
    if (jsonData.isObject()) {
        String msgType = jsonData["type"].toString();
        
        if (msgType == "join_room" || msgType == "join") {
            handleJoinRoom(userId, jsonData);
        }
        else if (msgType == "ping") {
            handlePing(userId, jsonData);
        }
    }
}

void FLStreamWebSocketServer::handleBinaryMessage(const String& userId, const MemoryBlock& data)
{
    // Handle audio data
    if (data.getSize() > 0) {
        OptimizedAudioPacket packet = OptimizedAudioPacket::deserialize(data);
        
        if (onAudioReceived) {
            onAudioReceived(packet, userId);
        }
        
        // Update user statistics
        {
            const ScopedLock lock(usersMutex);
            auto userIt = users.find(userId);
            if (userIt != users.end()) {
                userIt->second->packetsReceived++;
            }
        }
    }
}

void FLStreamWebSocketServer::handleJoinRoom(const String& userId, const var& jsonData)
{
    String roomId = jsonData["roomId"].toString();
    String userType = jsonData.getProperty("userType", "listener").toString();
    
    {
        const ScopedLock lock(usersMutex);
        auto userIt = users.find(userId);
        if (userIt != users.end()) {
            // Remove from old room
            String oldRoomId = userIt->second->roomId;
            auto& oldRoomList = roomUsers[oldRoomId];
            oldRoomList.erase(std::remove(oldRoomList.begin(), oldRoomList.end(), userId), oldRoomList.end());
            
            // Add to new room
            userIt->second->roomId = roomId;
            userIt->second->userType = userType;
            roomUsers[roomId].push_back(userId);
        }
    }
    
    // Send confirmation
    var response = var(new DynamicObject());
    response.getDynamicObject()->setProperty("type", "room_joined");
    response.getDynamicObject()->setProperty("roomId", roomId);
    response.getDynamicObject()->setProperty("userType", userType);
    
    sendTextToUser(userId, JSON::toString(response));
    
    logMessage("User " + userId + " joined room " + roomId + " as " + userType);
}

void FLStreamWebSocketServer::handlePing(const String& userId, const var& jsonData)
{
    var response = var(new DynamicObject());
    response.getDynamicObject()->setProperty("type", "pong");
    response.getDynamicObject()->setProperty("timestamp", jsonData["timestamp"]);
    response.getDynamicObject()->setProperty("serverTime", Time::getCurrentTime().toMilliseconds());
    
    sendTextToUser(userId, JSON::toString(response));
}

void FLStreamWebSocketServer::sendWelcomeMessage(const String& userId)
{
    // Simplified welcome message to avoid JSON/var issues
    String message = "{\"type\":\"welcome\",\"userId\":\"" + userId + "\",\"roomId\":\"default-room\"}";
    sendTextToUser(userId, message);
}

void FLStreamWebSocketServer::sendTextToUser(const String& userId, const String& message)
{
    const ScopedLock lock(usersMutex);
    auto connIt = connections.find(userId);
    if (connIt != connections.end() && connIt->second->isConnected()) {
        connIt->second->sendText(message);
    }
}

void FLStreamWebSocketServer::broadcastAudio(const AudioBuffer<float>& buffer, const String& roomId)
{
    if (!serverRunning.load()) return;
    
    OptimizedAudioPacket packet(buffer, globalSequenceId.fetch_add(1), 48000.0);
    MemoryBlock serialized = packet.serialize();
    
    // Broadcast to all users in the room
    std::vector<String> usersInRoom;
    {
        const ScopedLock lock(usersMutex);
        auto roomIt = roomUsers.find(roomId);
        if (roomIt != roomUsers.end()) {
            usersInRoom = roomIt->second;
        }
    }
    
    for (const auto& userId : usersInRoom) {
        const ScopedLock lock(usersMutex);
        auto connIt = connections.find(userId);
        if (connIt != connections.end() && connIt->second->isConnected()) {
            connIt->second->sendBinary(serialized);
        }
    }
    
    // Update statistics
    {
        const ScopedLock lock(statsMutex);
        stats.totalPacketsSent += usersInRoom.size();
        stats.totalBandwidth += serialized.getSize() * usersInRoom.size() / 1024.0 / 1024.0;
    }
}

void FLStreamWebSocketServer::handleUserDisconnection(const String& userId)
{
    logMessage("User disconnected: " + userId);
    
    {
        const ScopedLock lock(usersMutex);
        
        // Remove from connections
        connections.erase(userId);
        
        // Remove from rooms and users
        auto userIt = users.find(userId);
        if (userIt != users.end()) {
            String roomId = userIt->second->roomId;
            
            auto& roomUserList = roomUsers[roomId];
            roomUserList.erase(std::remove(roomUserList.begin(), roomUserList.end(), userId), roomUserList.end());
            
            if (roomUserList.empty()) {
                roomUsers.erase(roomId);
            }
            
            users.erase(userIt);
        }
    }
    
    if (onUserLeft) {
        onUserLeft(userId, "");
    }
}

// Implement remaining methods...
int FLStreamWebSocketServer::getUserCount(const String& roomId) const
{
    const ScopedLock lock(usersMutex);
    
    if (roomId.isEmpty()) {
        return static_cast<int>(users.size());
    }
    
    auto roomIt = roomUsers.find(roomId);
    return roomIt != roomUsers.end() ? static_cast<int>(roomIt->second.size()) : 0;
}

void FLStreamWebSocketServer::logMessage(const String& message)
{
    if (onLogMessage) {
        onLogMessage(message);
    }
    
    DBG("FLStreamWebSocketServer (JUCE Native): " + message);
}

void FLStreamWebSocketServer::sendAudioToUser(const AudioBuffer<float>& buffer, const String& userId)
{
    if (!serverRunning.load()) return;
    
    OptimizedAudioPacket packet(buffer, globalSequenceId.fetch_add(1), 48000.0);
    MemoryBlock serialized = packet.serialize();
    
    // Send to specific user
    const ScopedLock lock(usersMutex);
    auto connIt = connections.find(userId);
    if (connIt != connections.end() && connIt->second->isConnected()) {
        connIt->second->sendBinary(serialized);
        
        auto userIt = users.find(userId);
        if (userIt != users.end()) {
            userIt->second->packetsSent++;
            userIt->second->bandwidth += serialized.getSize();
        }
    }
}

std::vector<String> FLStreamWebSocketServer::getRoomList() const
{
    const ScopedLock lock(usersMutex);
    
    std::vector<String> rooms;
    rooms.reserve(roomUsers.size());
    
    for (const auto& [roomId, userList] : roomUsers) {
        if (!userList.empty()) {
            rooms.push_back(roomId);
        }
    }
    
    return rooms;
}

std::vector<UserSession> FLStreamWebSocketServer::getUsersInRoom(const String& roomId) const
{
    const ScopedLock lock(usersMutex);
    
    std::vector<UserSession> result;
    auto roomIt = roomUsers.find(roomId);
    
    if (roomIt != roomUsers.end()) {
        result.reserve(roomIt->second.size());
        for (const auto& userId : roomIt->second) {
            auto userIt = users.find(userId);
            if (userIt != users.end()) {
                result.push_back(*userIt->second);
            }
        }
    }
    
    return result;
}

FLStreamWebSocketServer::ServerStats FLStreamWebSocketServer::getServerStats() const
{
    const ScopedLock lock(statsMutex);
    
    ServerStats currentStats = stats;
    currentStats.totalConnections = static_cast<int>(users.size());
    currentStats.activeRooms = static_cast<int>(roomUsers.size());
    
    return currentStats;
}

//==============================================================================
// JUCE-based WebSocket Client Implementation
//==============================================================================

FLStreamWebSocketClient::FLStreamWebSocketClient()
{
    logMessage("FL Stream WebSocket Client initialized (JUCE native implementation)");
}

FLStreamWebSocketClient::~FLStreamWebSocketClient()
{
    disconnect();
}

bool FLStreamWebSocketClient::connectToServer(const String& address, int port, const String& roomId)
{
    if (connected.load()) {
        logMessage("Already connected to server");
        return true;
    }
    
    serverAddress = address;
    serverPort = port;
    currentRoomId = roomId;
    shouldStop.store(false);
    
    logMessage("Connecting to JUCE server " + address + ":" + String(port) + " room " + roomId);
    
    try {
        clientSocket = std::make_unique<StreamingSocket>();
        if (!clientSocket->connect(address, port)) {
            logMessage("Failed to connect to server");
            return false;
        }
        // Perform WebSocket handshake (simplified)
        String hostHeader = address + ":" + String(port);
        uint8_t keyBytes[16];
        for (auto& b : keyBytes) b = (uint8_t)juce::Random::getSystemRandom().nextInt(256);
        String secKey = Base64::toBase64(keyBytes, 16);
        String request =
            "GET / HTTP/1.1\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Host: " + hostHeader + "\r\n"
            "Sec-WebSocket-Key: " + secKey + "\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "\r\n";
        if (clientSocket->write(request.toRawUTF8(), (int)request.getNumBytesAsUTF8()) <= 0) {
            logMessage("Failed to write handshake request");
            return false;
        }
        // Read handshake response with short wait until end-of-headers
        String resp;
        char respBuf[1024];
        const int maxWaitMs = 2000;
        int waited = 0;
        while (waited < maxWaitMs && !resp.contains("\r\n\r\n")) {
            if (clientSocket->waitUntilReady(false, 50)) {
                int n = clientSocket->read(respBuf, (int)sizeof(respBuf)-1, false);
                if (n > 0) {
                    respBuf[n] = '\0';
                    resp += String(respBuf);
                    if (resp.contains("\r\n\r\n")) break;
                } else if (n < 0) {
                    break;
                }
            } else {
                waited += 50;
            }
        }
        if (!resp.containsIgnoreCase("101 Switching Protocols")) {
            logMessage("No handshake response from server");
            return false;
        }
        if (!resp.containsIgnoreCase("101 Switching Protocols") || !resp.containsIgnoreCase("Sec-WebSocket-Accept")) {
            logMessage("Invalid handshake response");
            return false;
        }

        connected.store(true);
        
        clientThread = std::make_unique<std::thread>([this]() {
            runClient();
        });
        
        Thread::sleep(500);
        
        if (connected.load()) {
            logMessage("Successfully connected to JUCE server");
            return true;
        }
    }
    catch (const std::exception& e) {
        logMessage("Failed to connect: " + String(e.what()));
    }
    
    return false;
}

void FLStreamWebSocketClient::disconnect()
{
    if (!connected.load()) return;
    
    logMessage("Disconnecting from JUCE server...");
    shouldStop.store(true);
    connected.store(false);
    
    if (clientSocket) {
        clientSocket->close();
        clientSocket.reset();
    }
    
    if (clientThread && clientThread->joinable()) {
        clientThread->join();
        clientThread.reset();
    }
    
    logMessage("Disconnected from JUCE server");
}

void FLStreamWebSocketClient::sendAudio(const AudioBuffer<float>& buffer)
{
    if (!connected.load()) return;
    
    OptimizedAudioPacket packet(buffer, sequenceId.fetch_add(1), 48000.0);
    
    // Add to send queue (lock-free)
    size_t writeIndex = sendWriteIndex.load();
    size_t nextWriteIndex = (writeIndex + 1) % AUDIO_QUEUE_SIZE;
    
    if (nextWriteIndex != sendReadIndex.load()) {
        sendQueue[writeIndex] = std::move(packet);
        sendWriteIndex.store(nextWriteIndex);
    }
}

bool FLStreamWebSocketClient::receiveAudio(AudioBuffer<float>& buffer)
{
    size_t readIndex = receiveReadIndex.load();
    if (readIndex == receiveWriteIndex.load()) {
        return false; // No new audio
    }
    
    const auto& packet = receiveQueue[readIndex];
    packet.toAudioBuffer(buffer);
    
    receiveReadIndex.store((readIndex + 1) % AUDIO_QUEUE_SIZE);
    return true;
}

void FLStreamWebSocketClient::setUserInfo(const String& userId, const String& userType)
{
    currentUserId = userId;
    currentUserType = userType;
}

void FLStreamWebSocketClient::runClient()
{
    logMessage("Starting JUCE WebSocket client thread");
    
    while (!shouldStop.load() && connected.load()) {
        // Try to read any incoming frames (non-blocking semantics are handled by JUCE read)
        uint8_t buffer[8192];
        int bytesRead = clientSocket ? clientSocket->read(buffer, (int)sizeof(buffer), false) : -1;
        if (bytesRead > 0) {
            MemoryBlock data(buffer, (size_t)bytesRead);
            auto frame = WebSocketFrame::decode(data);
            if (frame.opcode == WebSocketFrame::BINARY && frame.payload.getSize() > 0) {
                // Deserialize audio packet and enqueue
                OptimizedAudioPacket packet = OptimizedAudioPacket::deserialize(frame.payload);
                size_t writeIndex = receiveWriteIndex.load();
                size_t nextWrite = (writeIndex + 1) % AUDIO_QUEUE_SIZE;
                if (nextWrite != receiveReadIndex.load()) {
                    receiveQueue[writeIndex] = std::move(packet);
                    receiveWriteIndex.store(nextWrite);
                } // else: drop if queue full
            }
            // Ignore text/other opcodes for now
        } else if (bytesRead == 0) {
            // No data; small sleep to avoid busy-wait
            Thread::sleep(2);
        } else {
            // Error or disconnect
            break;
        }

        // Process send queue: send as masked binary WebSocket frames
        size_t readIndex = sendReadIndex.load();
        while (readIndex != sendWriteIndex.load()) {
            const auto& pkt = sendQueue[readIndex];
            MemoryBlock sdata = pkt.serialize();
            auto frame = encodeWebSocketFrameClient(0x2, sdata.getData(), sdata.getSize());
            if (clientSocket)
                clientSocket->write(frame.getData(), (int)frame.getSize());
            readIndex = (readIndex + 1) % AUDIO_QUEUE_SIZE;
            sendReadIndex.store(readIndex);
        }
    }
    
    connected.store(false);
    logMessage("JUCE WebSocket client thread stopped");
}

void FLStreamWebSocketClient::logMessage(const String& message)
{
    if (onLogMessage) {
        onLogMessage(message);
    }
    
    DBG("FLStreamWebSocketClient (JUCE Native): " + message);
}
