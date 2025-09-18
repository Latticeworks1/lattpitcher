#include "WebSocketServer.h"
#include <chrono>
#include <random>

using namespace juce;

//==============================================================================
// FLStreamWebSocketServer Implementation
//==============================================================================

FLStreamWebSocketServer::FLStreamWebSocketServer()
{
    logMessage("FL Stream WebSocket Server initialized");
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
    
    logMessage("Starting FL Stream WebSocket server on port " + String(port));
    
    try {
        serverThread = std::make_unique<std::thread>([this, port]() {
            runServer(port);
        });
        
        // Wait a bit for server to start
        Thread::sleep(100);
        
        if (serverRunning.load()) {
            logMessage("WebSocket server started successfully on port " + String(port));
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
    
    logMessage("Stopping FL Stream WebSocket server...");
    serverRunning.store(false);
    
    if (serverThread && serverThread->joinable()) {
        serverThread->join();
        serverThread.reset();
    }
    
    {
        const ScopedLock lock(usersMutex);
        users.clear();
        roomUsers.clear();
    }
    
    logMessage("WebSocket server stopped");
}

void FLStreamWebSocketServer::broadcastAudio(const AudioBuffer<float>& buffer, const String& roomId)
{
    if (!serverRunning.load()) return;
    
    OptimizedAudioPacket packet(buffer, globalSequenceId.fetch_add(1), 48000.0);
    MemoryBlock serialized = packet.serialize();
    
    std::vector<String> usersInRoom;
    {
        const ScopedLock lock(usersMutex);
        auto roomIt = roomUsers.find(roomId);
        if (roomIt != roomUsers.end()) {
            usersInRoom = roomIt->second;
        }
    }
    
    // Broadcast to all users in the room (except producers)
    for (const auto& userId : usersInRoom) {
        const ScopedLock lock(usersMutex);
        auto userIt = users.find(userId);
        if (userIt != users.end() && userIt->second->userType == "listener") {
            // In production, this would send via actual WebSocket
            userIt->second->packetsSent++;
            userIt->second->bandwidth += serialized.getSize();
        }
    }
    
    // Update statistics
    {
        const ScopedLock lock(statsMutex);
        stats.totalPacketsSent += usersInRoom.size();
        stats.totalBandwidth += serialized.getSize() * usersInRoom.size() / 1024.0 / 1024.0; // MB
    }
}

void FLStreamWebSocketServer::sendAudioToUser(const AudioBuffer<float>& buffer, const String& userId)
{
    if (!serverRunning.load()) return;
    
    OptimizedAudioPacket packet(buffer, globalSequenceId.fetch_add(1), 48000.0);
    MemoryBlock serialized = packet.serialize();
    
    const ScopedLock lock(usersMutex);
    auto userIt = users.find(userId);
    if (userIt != users.end() && userIt->second->isConnected) {
        // In production, this would send via actual WebSocket
        userIt->second->packetsSent++;
        userIt->second->bandwidth += serialized.getSize();
    }
}

int FLStreamWebSocketServer::getUserCount(const String& roomId) const
{
    const ScopedLock lock(usersMutex);
    
    if (roomId.isEmpty()) {
        return static_cast<int>(users.size());
    }
    
    auto roomIt = roomUsers.find(roomId);
    return roomIt != roomUsers.end() ? static_cast<int>(roomIt->second.size()) : 0;
}

std::vector<String> FLStreamWebSocketServer::getRoomList() const
{
    const ScopedLock lock(usersMutex);
    
    std::vector<String> rooms;
    rooms.reserve(roomUsers.size());
    
    for (const auto& [roomId, users] : roomUsers) {
        if (!users.empty()) {
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

void FLStreamWebSocketServer::runServer(int port)
{
    try {
        server = std::make_unique<SimpleWebSocketServer>();
        
        setupWebSocketHandlers();
        
        // Simulate serving the web client
            res->writeHeader("Content-Type", "text/html");
            res->end(R"HTML(
<!DOCTYPE html>
<html>
<head>
    <title>FL Studio Remote Vocal Input</title>
    <style>
        body { font-family: Arial, sans-serif; background: #2c3e50; color: white; text-align: center; padding: 20px; }
        .container { max-width: 800px; margin: 0 auto; }
        .controls { margin: 20px 0; }
        button { padding: 10px 20px; margin: 5px; background: #3498db; color: white; border: none; border-radius: 5px; cursor: pointer; }
        button:hover { background: #2980b9; }
        button:disabled { background: #7f8c8d; cursor: not-allowed; }
        .status { margin: 10px 0; padding: 10px; border-radius: 5px; }
        .connected { background: #27ae60; }
        .disconnected { background: #e74c3c; }
        .audio-controls { margin: 20px 0; }
        input[type="range"] { width: 200px; }
        .room-info { background: #34495e; padding: 15px; border-radius: 5px; margin: 10px 0; }
    </style>
</head>
<body>
    <div class="container">
        <h1>🎤 FL Studio Remote Vocal Input</h1>
        <div class="room-info">
            <h3>Room ID: <span id="roomId">FL-ROOM-001</span></h3>
            <p>Connect your browser to stream vocals to FL Studio</p>
        </div>
        
        <div class="controls">
            <button id="connectBtn">Connect to FL Studio</button>
            <button id="recordBtn" disabled>Start Recording</button>
            <button id="stopBtn" disabled>Stop Recording</button>
        </div>
        
        <div id="status" class="status disconnected">Disconnected</div>
        
        <div class="audio-controls">
            <label>Input Gain: <input type="range" id="gainSlider" min="0" max="200" value="100">%</label>
            <br><br>
            <label>Monitor Volume: <input type="range" id="monitorSlider" min="0" max="100" value="50">%</label>
        </div>
        
        <div id="audioInfo"></div>
    </div>
    
    <script>
        class FLStudioVocalClient {
            constructor() {
                this.ws = null;
                this.audioContext = null;
                this.mediaStream = null;
                this.workletNode = null;
                this.isConnected = false;
                this.isRecording = false;
                
                this.setupUI();
            }
            
            setupUI() {
                document.getElementById('connectBtn').onclick = () => this.connect();
                document.getElementById('recordBtn').onclick = () => this.startRecording();
                document.getElementById('stopBtn').onclick = () => this.stopRecording();
            }
            
            async connect() {
                try {
                    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
                    const wsUrl = `${protocol}//${window.location.host}`;
                    
                    this.ws = new WebSocket(wsUrl);
                    
                    this.ws.onopen = () => {
                        this.isConnected = true;
                        this.updateStatus('Connected to FL Studio', 'connected');
                        document.getElementById('connectBtn').disabled = true;
                        document.getElementById('recordBtn').disabled = false;
                        
                        // Send join room message
                        this.ws.send(JSON.stringify({
                            type: 'join',
                            roomId: 'FL-ROOM-001',
                            userId: 'browser-' + Math.random().toString(36).substr(2, 9),
                            userType: 'producer'
                        }));
                    };
                    
                    this.ws.onclose = () => {
                        this.isConnected = false;
                        this.updateStatus('Disconnected from FL Studio', 'disconnected');
                        document.getElementById('connectBtn').disabled = false;
                        document.getElementById('recordBtn').disabled = true;
                        document.getElementById('stopBtn').disabled = true;
                    };
                    
                    this.ws.onerror = (error) => {
                        console.error('WebSocket error:', error);
                        this.updateStatus('Connection error', 'disconnected');
                    };
                    
                } catch (error) {
                    console.error('Connection failed:', error);
                    this.updateStatus('Failed to connect', 'disconnected');
                }
            }
            
            async startRecording() {
                try {
                    this.mediaStream = await navigator.mediaDevices.getUserMedia({ 
                        audio: { 
                            echoCancellation: false,
                            noiseSuppression: false,
                            autoGainControl: false,
                            sampleRate: 48000
                        } 
                    });
                    
                    this.audioContext = new AudioContext({ sampleRate: 48000 });
                    
                    const source = this.audioContext.createMediaStreamSource(this.mediaStream);
                    
                    // Load audio worklet for real-time processing
                    await this.audioContext.audioWorklet.addModule('data:text/javascript,' + encodeURIComponent(`
                        class AudioStreamProcessor extends AudioWorkletProcessor {
                            constructor() {
                                super();
                                this.bufferSize = 512;
                                this.buffer = new Float32Array(this.bufferSize);
                                this.bufferIndex = 0;
                            }
                            
                            process(inputs) {
                                const input = inputs[0];
                                if (!input || !input[0]) return true;
                                
                                const inputData = input[0];
                                
                                for (let i = 0; i < inputData.length; i++) {
                                    this.buffer[this.bufferIndex] = inputData[i];
                                    this.bufferIndex++;
                                    
                                    if (this.bufferIndex >= this.bufferSize) {
                                        // Send audio data to main thread
                                        this.port.postMessage({
                                            type: 'audioData',
                                            data: Array.from(this.buffer)
                                        });
                                        
                                        this.bufferIndex = 0;
                                    }
                                }
                                
                                return true;
                            }
                        }
                        
                        registerProcessor('audio-stream-processor', AudioStreamProcessor);
                    `));
                    
                    this.workletNode = new AudioWorkletNode(this.audioContext, 'audio-stream-processor');
                    
                    this.workletNode.port.onmessage = (event) => {
                        if (event.data.type === 'audioData' && this.isConnected && this.ws.readyState === WebSocket.OPEN) {
                            // Convert to binary and send via WebSocket
                            const audioData = new Float32Array(event.data.data);
                            this.sendAudioData(audioData);
                        }
                    };
                    
                    source.connect(this.workletNode);
                    
                    this.isRecording = true;
                    this.updateStatus('Recording and streaming to FL Studio', 'connected');
                    document.getElementById('recordBtn').disabled = true;
                    document.getElementById('stopBtn').disabled = false;
                    
                } catch (error) {
                    console.error('Failed to start recording:', error);
                    this.updateStatus('Microphone access denied', 'disconnected');
                }
            }
            
            sendAudioData(audioData) {
                // Create optimized audio packet
                const packet = {
                    timestamp: Date.now(),
                    sequenceId: Math.floor(Math.random() * 1000000),
                    channels: 1,
                    sampleRate: 48,
                    numSamples: audioData.length,
                    audioData: Array.from(audioData)
                };
                
                // Send as binary WebSocket message
                if (this.ws && this.ws.readyState === WebSocket.OPEN) {
                    this.ws.send(JSON.stringify({
                        type: 'audioData',
                        data: packet
                    }));
                }
            }
            
            stopRecording() {
                if (this.mediaStream) {
                    this.mediaStream.getTracks().forEach(track => track.stop());
                    this.mediaStream = null;
                }
                
                if (this.audioContext) {
                    this.audioContext.close();
                    this.audioContext = null;
                }
                
                this.isRecording = false;
                this.updateStatus('Recording stopped', 'connected');
                document.getElementById('recordBtn').disabled = false;
                document.getElementById('stopBtn').disabled = true;
            }
            
            updateStatus(message, className) {
                const statusEl = document.getElementById('status');
                statusEl.textContent = message;
                statusEl.className = `status ${className}`;
            }
        }
        
        // Initialize the vocal client
        const vocalClient = new FLStudioVocalClient();
    </script>
</body>
</html>
            )HTML");
        });
        
        listenSocket = app->listen(port, [this, port](auto* token) {
            if (token) {
                serverRunning.store(true);
                logMessage("WebSocket server listening on port " + String(port));
            } else {
                logMessage("Failed to listen on port " + String(port));
            }
        });
        
        if (serverRunning.load()) {
            app->run();
        }
    }
    catch (const std::exception& e) {
        logMessage("Server error: " + String(e.what()));
        serverRunning.store(false);
    }
#else
    // Fallback implementation without uWebSockets
    logMessage("WebSocket server not available - uWebSockets not included");
    Thread::sleep(1000);
    serverRunning.store(false);
#endif
}

void FLStreamWebSocketServer::setupWebSocketHandlers()
{
#ifdef ENABLE_WEBSOCKET_SERVER
    app->ws<WebSocketUserData>("/*", {
        .compression = uWS::SHARED_COMPRESSOR,
        .maxCompressedSize = 64 * 1024,
        .maxBackpressure = 64 * 1024,
        
        .open = [this](auto* ws) {
            handleUserConnection(ws);
        },
        
        .message = [this](auto* ws, std::string_view message, uWS::OpCode opCode) {
            handleWebSocketMessage(ws, message, opCode);
        },
        
        .close = [this](auto* ws, int code, std::string_view message) {
            handleUserDisconnection(ws);
        }
    });
#endif
}

#ifdef ENABLE_WEBSOCKET_SERVER
void FLStreamWebSocketServer::handleWebSocketMessage(uWS::WebSocket<false, true, WebSocketUserData>* ws,
                                                    std::string_view message, uWS::OpCode opCode)
{
    auto* userData = ws->getUserData();
    
    if (opCode == uWS::OpCode::TEXT) {
        // Handle JSON messages (control, metadata)
        try {
            var parsed = JSON::parse(String(message.data(), message.size()));
            String messageType = parsed["type"].toString();
            
            if (messageType == "join") {
                String roomId = parsed["roomId"].toString();
                String userId = parsed["userId"].toString();
                String userType = parsed["userType"].toString();
                
                userData->roomId = roomId;
                userData->userId = userId;
                userData->userType = userType;
                
                addUserToRoom(userId, roomId, userType);
                
                if (onUserJoined) {
                    onUserJoined(userId, roomId);
                }
                
                logMessage("User " + userId + " joined room " + roomId + " as " + userType);
            }
            else if (messageType == "audioData") {
                // Handle JSON-encoded audio data from browser
                var audioPacketData = parsed["data"];
                
                OptimizedAudioPacket packet;
                packet.timestamp = audioPacketData["timestamp"];
                packet.sequenceId = audioPacketData["sequenceId"];
                packet.channels = audioPacketData["channels"];
                packet.sampleRate = audioPacketData["sampleRate"];
                packet.numSamples = audioPacketData["numSamples"];
                
                // Convert from var array to float vector
                if (audioPacketData["audioData"].isArray()) {
                    Array<var>* audioArray = audioPacketData["audioData"].getArray();
                    packet.audioData.reserve(audioArray->size());
                    for (const auto& sample : *audioArray) {
                        packet.audioData.push_back(static_cast<float>(sample));
                    }
                }
                
                if (onAudioReceived) {
                    onAudioReceived(packet, userData->userId);
                }
                
                userData->messageCount.fetch_add(1);
            }
        }
        catch (const std::exception& e) {
            logMessage("Error parsing message: " + String(e.what()));
        }
    }
    else if (opCode == uWS::OpCode::BINARY) {
        // Handle binary audio data
        MemoryBlock block(message.data(), message.size());
        OptimizedAudioPacket packet = OptimizedAudioPacket::deserialize(block);
        
        if (onAudioReceived) {
            onAudioReceived(packet, userData->userId);
        }
        
        userData->messageCount.fetch_add(1);
    }
}

void FLStreamWebSocketServer::handleUserConnection(uWS::WebSocket<false, true, WebSocketUserData>* ws)
{
    auto* userData = ws->getUserData();
    userData->connectionTime = Time::getCurrentTime().toMilliseconds();
    
    logMessage("New WebSocket connection established");
}

void FLStreamWebSocketServer::handleUserDisconnection(uWS::WebSocket<false, true, WebSocketUserData>* ws)
{
    auto* userData = ws->getUserData();
    
    if (!userData->userId.isEmpty()) {
        removeUserFromRoom(userData->userId);
        
        if (onUserLeft) {
            onUserLeft(userData->userId, userData->roomId);
        }
        
        logMessage("User " + userData->userId + " disconnected from room " + userData->roomId);
    }
}
#endif

void FLStreamWebSocketServer::addUserToRoom(const String& userId, const String& roomId, const String& userType)
{
    const ScopedLock lock(usersMutex);
    
    auto user = std::make_unique<UserSession>(userId, roomId, userType);
    users[userId] = std::move(user);
    
    roomUsers[roomId].push_back(userId);
}

void FLStreamWebSocketServer::removeUserFromRoom(const String& userId)
{
    const ScopedLock lock(usersMutex);
    
    auto userIt = users.find(userId);
    if (userIt != users.end()) {
        String roomId = userIt->second->roomId;
        
        // Remove from room
        auto& roomUserList = roomUsers[roomId];
        roomUserList.erase(std::remove(roomUserList.begin(), roomUserList.end(), userId), roomUserList.end());
        
        // Clean up empty rooms
        if (roomUserList.empty()) {
            roomUsers.erase(roomId);
        }
        
        // Remove user
        users.erase(userIt);
    }
}

void FLStreamWebSocketServer::logMessage(const String& message)
{
    if (onLogMessage) {
        onLogMessage(message);
    }
    
    DBG("FLStreamWebSocketServer: " + message);
}

//==============================================================================
// FLStreamWebSocketClient Implementation
//==============================================================================

FLStreamWebSocketClient::FLStreamWebSocketClient()
{
    logMessage("FL Stream WebSocket Client initialized");
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
    
    logMessage("Connecting to server " + address + ":" + String(port) + " room " + roomId);
    
    try {
        clientThread = std::make_unique<std::thread>([this]() {
            runClient();
        });
        
        // Wait for connection
        Thread::sleep(500);
        
        if (connected.load()) {
            logMessage("Successfully connected to FL Studio server");
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
    
    logMessage("Disconnecting from server...");
    shouldStop.store(true);
    connected.store(false);
    
    if (clientThread && clientThread->joinable()) {
        clientThread->join();
        clientThread.reset();
    }
    
    logMessage("Disconnected from server");
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
    // In a production implementation, this would establish a WebSocket connection
    // For now, we simulate the client connection
    
    logMessage("Starting WebSocket client thread");
    connected.store(true);
    
    while (!shouldStop.load()) {
        // Simulate client operations
        Thread::sleep(10);
        
        // Process send queue
        size_t readIndex = sendReadIndex.load();
        if (readIndex != sendWriteIndex.load()) {
            // Would send packet to server here
            sendReadIndex.store((readIndex + 1) % AUDIO_QUEUE_SIZE);
        }
    }
    
    connected.store(false);
    logMessage("WebSocket client thread stopped");
}

void FLStreamWebSocketClient::logMessage(const String& message)
{
    if (onLogMessage) {
        onLogMessage(message);
    }
    
    DBG("FLStreamWebSocketClient: " + message);
}