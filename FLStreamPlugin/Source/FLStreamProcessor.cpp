#include "FLStreamProcessor.h"
#include "FLStreamEditor.h"
#include <chrono>

//==============================================================================
// FLStreamProcessor Implementation
//==============================================================================

FLStreamProcessor::FLStreamProcessor()
    : AudioProcessor(BusesProperties()
                     .withInput("Input", AudioChannelSet::stereo(), true)
                     .withOutput("Output", AudioChannelSet::stereo(), true))
    , parameters(*this, nullptr, "FL_STREAM_PARAMETERS", createParameterLayout())
{
    // Initialize parameter pointers
    streamingModeParam = parameters.getRawParameterValue(PARAM_STREAMING_MODE);
    masterTrackParam = parameters.getRawParameterValue(PARAM_MASTER_TRACK);
    inputGainParam = parameters.getRawParameterValue(PARAM_INPUT_GAIN);
    outputGainParam = parameters.getRawParameterValue(PARAM_OUTPUT_GAIN);
    mixAmountParam = parameters.getRawParameterValue(PARAM_MIX_AMOUNT);
    latencyCompParam = parameters.getRawParameterValue(PARAM_LATENCY_COMP);
    wsPortParam = parameters.getRawParameterValue(PARAM_WEBSOCKET_PORT);
    
    // Add parameter listeners
    parameters.addParameterListener(PARAM_STREAMING_MODE, this);
    parameters.addParameterListener(PARAM_MASTER_TRACK, this);
    parameters.addParameterListener(PARAM_LATENCY_COMP, this);
    parameters.addParameterListener(PARAM_WEBSOCKET_PORT, this);
    
    // Initialize WebSocket server
    webSocketServer = std::make_unique<FLStreamWebSocketServer>();
    webSocketClient = std::make_unique<FLStreamWebSocketClient>();
    
    // Setup WebSocket callbacks
    webSocketServer->onAudioReceived = [this](const OptimizedAudioPacket& packet, const String& userId) {
        handleAudioReceived(packet, userId);
    };
    
    webSocketServer->onUserJoined = [this](const String& userId, const String& roomId) {
        handleUserJoined(userId, roomId);
    };
    
    webSocketServer->onUserLeft = [this](const String& userId, const String& roomId) {
        handleUserLeft(userId, roomId);
    };
    
    webSocketServer->onLogMessage = [this](const String& message) {
        handleWebSocketLog(message);
    };
    
    // Initialize statistics
    lastStatsUpdate = std::chrono::steady_clock::now();
    
    logMessage("FL Studio Audio Streamer initialized");
}

FLStreamProcessor::~FLStreamProcessor()
{
    stopWebSocketServer();
    webSocketClient->disconnect();
}

//==============================================================================
// AudioProcessor Implementation
//==============================================================================

void FLStreamProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate.store(sampleRate);
    currentBufferSize.store(samplesPerBlock);
    
    // Initialize audio buffers with thread safety
    const ScopedLock lock(bufferMutex);
    mixBuffer.setSize(2, samplesPerBlock);
    sendBuffer.setSize(2, samplesPerBlock);
    receiveBuffer.setSize(2, samplesPerBlock);
    
    // Setup delay lines for latency compensation
    const int maxDelaySamples = static_cast<int>(sampleRate * 0.1); // 100ms max delay
    inputDelayLine.setSize(2, maxDelaySamples);
    outputDelayLine.setSize(2, maxDelaySamples);
    
    // Update latency compensation
    const int latencyCompSamples = static_cast<int>(latencyCompParam->load());
    setLatencyCompensation(latencyCompSamples);
    
    logMessage("Prepared to play: " + String(sampleRate) + " Hz, " + String(samplesPerBlock) + " samples");
}

void FLStreamProcessor::releaseResources()
{
    mixBuffer.setSize(0, 0);
    sendBuffer.setSize(0, 0);
    receiveBuffer.setSize(0, 0);
    
    inputDelayLine.reset();
    outputDelayLine.reset();
    
    logMessage("Resources released");
}

bool FLStreamProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // Support mono and stereo input/output
    const auto& mainInput = layouts.getMainInputChannelSet();
    const auto& mainOutput = layouts.getMainOutputChannelSet();
    
    return (mainInput == AudioChannelSet::mono() || mainInput == AudioChannelSet::stereo()) &&
           (mainOutput == AudioChannelSet::mono() || mainOutput == AudioChannelSet::stereo()) &&
           mainInput == mainOutput;
}

void FLStreamProcessor::processBlock(AudioBuffer<float>& buffer, MidiBuffer& midiMessages)
{
    ScopedNoDenormals noDenormals;
    
    // Clear any MIDI messages
    midiMessages.clear();
    
    // Thread-safe buffer validation (try_lock to avoid blocking audio thread)
    const ScopedTryLock bufferLock(bufferMutex);
    if (!bufferLock.isLocked()) {
        // If we can't get the lock immediately, skip processing to avoid dropouts
        return;
    }
    
    // Validate buffer sizes match our internal buffers
    const bool buffersValid = (mixBuffer.getNumSamples() == buffer.getNumSamples() &&
                              sendBuffer.getNumSamples() == buffer.getNumSamples() &&
                              receiveBuffer.getNumSamples() == buffer.getNumSamples());
    
    if (!buffersValid) {
        return; // Skip processing if buffers are being resized
    }
    
    // Process based on current streaming mode
    const Mode currentMode = streamingMode.load();
    
    switch (currentMode) {
        case Mode::Server:
            processServerMode(buffer);
            break;
            
        case Mode::Client:
            processClientMode(buffer);
            break;
            
        case Mode::Disabled:
            // Pass-through mode - apply basic gain and latency compensation only
            buffer.applyGain(inputGainParam->load());
            if (latencyCompensationSamples.load() > 0) {
                inputDelayLine.process(buffer);
            }
            buffer.applyGain(outputGainParam->load());
            break;
    }
    
    // Buffer mutex automatically released by ScopedTryLock destructor
    
    // Update statistics periodically
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastStatsUpdate).count() > 1000) {
        updateStatistics();
        lastStatsUpdate = now;
    }
}

//==============================================================================
// Server/Client Processing
//==============================================================================

void FLStreamProcessor::processServerMode(AudioBuffer<float>& buffer)
{
    // Apply input gain
    const float inputGain = inputGainParam->load();
    buffer.applyGain(inputGain);
    
    // Apply latency compensation if enabled
    if (latencyCompensationSamples.load() > 0) {
        inputDelayLine.process(buffer);
    }
    
    // Send audio to connected clients
    sendAudioToNetwork(buffer);
    
    // Mix in any received audio from remote users
    receiveAudioFromNetwork(receiveBuffer);
    
    // Mix received audio with local audio
    const float mixAmount = mixAmountParam->load();
    if (mixAmount > 0.0f && receiveBuffer.getNumSamples() == buffer.getNumSamples()) {
        for (int ch = 0; ch < jmin(buffer.getNumChannels(), receiveBuffer.getNumChannels()); ++ch) {
            buffer.addFromWithRamp(ch, 0, receiveBuffer.getReadPointer(ch), 
                                 buffer.getNumSamples(), mixAmount, mixAmount);
        }
    }
    
    // Apply output gain
    const float outputGain = outputGainParam->load();
    buffer.applyGain(outputGain);
}

void FLStreamProcessor::processClientMode(AudioBuffer<float>& buffer)
{
    // In client mode, we primarily receive audio from the server
    // and optionally send our input back
    
    // Store original input
    sendBuffer.makeCopyOf(buffer);
    
    // Clear output buffer first
    buffer.clear();
    
    // Receive audio from server
    receiveAudioFromNetwork(buffer);
    
    // If we're also sending audio (two-way mode)
    const float inputGain = inputGainParam->load();
    if (inputGain > 0.0f) {
        sendBuffer.applyGain(inputGain);
        
        // Apply latency compensation to input
        if (latencyCompensationSamples.load() > 0) {
            inputDelayLine.process(sendBuffer);
        }
        
        // Send our processed input to the server
        sendAudioToNetwork(sendBuffer);
        
        // Mix our input with received audio for monitoring
        const float mixAmount = mixAmountParam->load();
        if (mixAmount > 0.0f) {
            for (int ch = 0; ch < jmin(buffer.getNumChannels(), sendBuffer.getNumChannels()); ++ch) {
                buffer.addFromWithRamp(ch, 0, sendBuffer.getReadPointer(ch), 
                                     buffer.getNumSamples(), mixAmount, mixAmount);
            }
        }
    }
    
    // Apply output gain
    const float outputGain = outputGainParam->load();
    buffer.applyGain(outputGain);
}

//==============================================================================
// Network Audio Processing
//==============================================================================

void FLStreamProcessor::sendAudioToNetwork(const AudioBuffer<float>& buffer)
{
    if (streamingMode.load() == Mode::Server && webSocketServer->isRunning()) {
        // Broadcast to all connected clients in the room (thread-safe room ID access)
        String roomId;
        {
            const ScopedLock lock(roomIdMutex);
            roomId = currentRoomId;
        }
        webSocketServer->broadcastAudio(buffer, roomId);
    }
    else if (streamingMode.load() == Mode::Client && webSocketClient->isConnected()) {
        // Send to server
        webSocketClient->sendAudio(buffer);
    }
    
    // Update statistics
    {
        const ScopedLock lock(statsMutex);
        currentStats.packetsSent++;
    }
}

void FLStreamProcessor::receiveAudioFromNetwork(AudioBuffer<float>& buffer)
{
    buffer.clear();
    
    if (streamingMode.load() == Mode::Client && webSocketClient->isConnected()) {
        // Receive from server
        if (webSocketClient->receiveAudio(buffer)) {
            const ScopedLock lock(statsMutex);
            currentStats.packetsReceived++;
        }
    }
    else if (streamingMode.load() == Mode::Server) {
        // In server mode, we could receive audio from clients for mixing
        // This would be handled through the WebSocket server callbacks
        // For now, we'll process any queued incoming audio
        
        size_t readIndex = incomingReadIndex.load();
        if (readIndex != incomingWriteIndex.load()) {
            const auto& packet = incomingAudioQueue[readIndex];
            packet.toAudioBuffer(buffer);
            incomingReadIndex.store((readIndex + 1) % AUDIO_QUEUE_SIZE);
            
            const ScopedLock lock(statsMutex);
            currentStats.packetsReceived++;
        }
    }
}

//==============================================================================
// WebSocket Server Control
//==============================================================================

bool FLStreamProcessor::startWebSocketServer(int port)
{
    if (webSocketServer->isRunning()) {
        logMessage("WebSocket server already running");
        return true;
    }
    
    webSocketPort.store(port);
    const bool success = webSocketServer->startServer(port);
    
    if (success) {
        logMessage("WebSocket server started on port " + String(port));
        setStreamingMode(true); // Switch to server mode
    } else {
        logMessage("Failed to start WebSocket server on port " + String(port));
    }
    
    return success;
}

void FLStreamProcessor::stopWebSocketServer()
{
    if (webSocketServer->isRunning()) {
        webSocketServer->stopServer();
        logMessage("WebSocket server stopped");
    }
    
    if (streamingMode.load() == Mode::Server) {
        streamingMode.store(Mode::Disabled);
    }
}

bool FLStreamProcessor::isServerRunning() const
{
    return webSocketServer->isRunning();
}

int FLStreamProcessor::getConnectedUserCount() const
{
    String roomId;
    {
        const ScopedLock lock(roomIdMutex);
        roomId = currentRoomId;
    }
    return webSocketServer->getUserCount(roomId);
}

//==============================================================================
// Client Connection
//==============================================================================

bool FLStreamProcessor::connectToServer(const String& serverAddress, int port, const String& roomId)
{
    if (webSocketClient->isConnected()) {
        logMessage("Already connected to server");
        return true;
    }
    
    // Set room ID first
    setRoomId(roomId);
    
    // Switch to client mode
    streamingMode.store(Mode::Client);
    
    // Stop server if running
    stopWebSocketServer();
    
    // Attempt connection
    const bool success = webSocketClient->connectToServer(serverAddress, port, roomId);
    
    if (success) {
        logMessage("Connected to server: " + serverAddress + ":" + String(port) + " (Room: " + roomId + ")");
    } else {
        logMessage("Failed to connect to server: " + serverAddress + ":" + String(port));
        streamingMode.store(Mode::Disabled);
    }
    
    return success;
}

void FLStreamProcessor::disconnectFromServer()
{
    if (webSocketClient->isConnected()) {
        webSocketClient->disconnect();
        logMessage("Disconnected from server");
    }
    
    if (streamingMode.load() == Mode::Client) {
        streamingMode.store(Mode::Disabled);
    }
}

//==============================================================================
// Streaming Mode and Configuration
//==============================================================================

void FLStreamProcessor::setStreamingMode(Mode mode)
{
    streamingMode.store(mode);
    switch (mode) {
        case Mode::Disabled:
            logMessage("Streaming disabled");
            break;
        case Mode::Server:
            logMessage("Switched to server mode");
            break;
        case Mode::Client:
            logMessage("Switched to client mode");
            break;
    }
}

void FLStreamProcessor::setStreamingMode(bool isServer)
{
    if (isServer) {
        setStreamingMode(Mode::Server);
    } else {
        setStreamingMode(Mode::Client);
    }
}

void FLStreamProcessor::setRoomId(const String& roomId)
{
    {
        const ScopedLock lock(roomIdMutex);
        currentRoomId = roomId;
    }
    logMessage("Room ID set to: " + roomId);
}

std::vector<String> FLStreamProcessor::getConnectedUsers() const
{
    String roomId;
    {
        const ScopedLock lock(roomIdMutex);
        roomId = currentRoomId;
    }
    const auto users = webSocketServer->getUsersInRoom(roomId);
    std::vector<String> userIds;
    userIds.reserve(users.size());
    
    for (const auto& user : users) {
        userIds.push_back(user.userId);
    }
    
    return userIds;
}

//==============================================================================
// FL Studio Integration
//==============================================================================

void FLStreamProcessor::setMasterTrackMode(bool enabled)
{
    masterTrackMode.store(enabled);
    
    if (enabled) {
        logMessage("Master track mode enabled - capturing system audio");
        // In master track mode, we capture the final mix from FL Studio
        setLatencySamples(0); // Minimize latency for master output
    } else {
        logMessage("Master track mode disabled - using track input");
    }
}

void FLStreamProcessor::setLatencyCompensation(int samples)
{
    const double sampleRate = currentSampleRate.load();
    const int clampedSamples = jlimit(0, static_cast<int>(sampleRate * 0.1), samples);
    latencyCompensationSamples.store(clampedSamples);
    
    inputDelayLine.setDelay(clampedSamples);
    outputDelayLine.setDelay(clampedSamples);
    
    const double latencyMs = (clampedSamples / sampleRate) * 1000.0;
    logMessage("Latency compensation set to " + String(clampedSamples) + 
               " samples (" + String(latencyMs, 1) + " ms)");
}

//==============================================================================
// Statistics and Monitoring
//==============================================================================

FLStreamProcessor::StreamingStats FLStreamProcessor::getStreamingStats() const
{
    const ScopedLock lock(statsMutex);
    
    StreamingStats stats = currentStats;
    stats.connectedUsers = getConnectedUserCount();
    
    // Calculate real-time statistics
    if (webSocketServer->isRunning()) {
        const auto serverStats = webSocketServer->getServerStats();
        stats.bandwidth = serverStats.totalBandwidth;
        stats.latency = serverStats.avgLatency;
    }
    
    return stats;
}

void FLStreamProcessor::updateStatistics()
{
    const ScopedLock lock(statsMutex);
    
    // Update CPU usage (simplified)
    // In a real implementation, this would measure actual processing time
    currentStats.cpuUsage = streamingMode.load() != Mode::Disabled ? 5.0 : 1.0;
    
    // Update audio level from recent processing
    // This would be calculated during audio processing
    currentStats.audioLevel = 0.5; // Placeholder
    
    // Trigger callback if set
    if (onStatsUpdate) {
        onStatsUpdate(currentStats);
    }
}

//==============================================================================
// Parameter Management
//==============================================================================

AudioProcessorValueTreeState::ParameterLayout FLStreamProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<RangedAudioParameter>> parameters;
    
    // Streaming mode (0=Disabled, 1=Server, 2=Client)
    parameters.push_back(std::make_unique<AudioParameterInt>(
        PARAM_STREAMING_MODE, "Streaming Mode", 0, 2, 0));
    
    // Master track mode
    parameters.push_back(std::make_unique<AudioParameterBool>(
        PARAM_MASTER_TRACK, "Master Track Mode", false));
    
    // Audio levels
    parameters.push_back(std::make_unique<AudioParameterFloat>(
        PARAM_INPUT_GAIN, "Input Gain", 0.0f, 2.0f, 1.0f));
    
    parameters.push_back(std::make_unique<AudioParameterFloat>(
        PARAM_OUTPUT_GAIN, "Output Gain", 0.0f, 2.0f, 1.0f));
    
    parameters.push_back(std::make_unique<AudioParameterFloat>(
        PARAM_MIX_AMOUNT, "Mix Amount", 0.0f, 1.0f, 1.0f));
    
    // Latency compensation
    parameters.push_back(std::make_unique<AudioParameterInt>(
        PARAM_LATENCY_COMP, "Latency Compensation", 0, 4800, 0));
    
    // WebSocket port
    parameters.push_back(std::make_unique<AudioParameterInt>(
        PARAM_WEBSOCKET_PORT, "WebSocket Port", 9000, 9999, 9001));
    
    return { parameters.begin(), parameters.end() };
}

void FLStreamProcessor::parameterChanged(const String& parameterID, float newValue)
{
    if (parameterID == PARAM_STREAMING_MODE) {
        const Mode newMode = static_cast<Mode>(static_cast<int>(newValue));
        streamingMode.store(newMode);
        updateStreamingMode();
    }
    else if (parameterID == PARAM_MASTER_TRACK) {
        setMasterTrackMode(newValue > 0.5f);
    }
    else if (parameterID == PARAM_LATENCY_COMP) {
        setLatencyCompensation(static_cast<int>(newValue));
    }
    else if (parameterID == PARAM_WEBSOCKET_PORT) {
        const int newPort = static_cast<int>(newValue);
        if (newPort != webSocketPort.load() && !webSocketServer->isRunning()) {
            webSocketPort.store(newPort);
            logMessage("WebSocket port changed to " + String(newPort));
        }
    }
}

void FLStreamProcessor::updateStreamingMode()
{
    const Mode currentMode = streamingMode.load();
    
    switch (currentMode) {
        case Mode::Disabled:
            stopWebSocketServer();
            webSocketClient->disconnect();
            logMessage("Streaming disabled");
            break;
            
        case Mode::Server:
            webSocketClient->disconnect();
            if (!webSocketServer->isRunning()) {
                startWebSocketServer(webSocketPort.load());
            }
            break;
            
        case Mode::Client:
            stopWebSocketServer();
            // Client connection would be initiated through GUI
            logMessage("Client mode ready - use GUI to connect to server");
            break;
    }
}

//==============================================================================
// Editor and State
//==============================================================================

AudioProcessorEditor* FLStreamProcessor::createEditor()
{
    return new FLStreamEditor(*this);
}

void FLStreamProcessor::getStateInformation(MemoryBlock& destData)
{
    auto state = parameters.copyState();
    
    // Add custom state information
    String roomId;
    {
        const ScopedLock lock(roomIdMutex);
        roomId = currentRoomId;
    }
    state.setProperty("roomId", roomId, nullptr);
    state.setProperty("webSocketPort", webSocketPort.load(), nullptr);
    
    std::unique_ptr<XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void FLStreamProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    
    if (xmlState.get() != nullptr) {
        if (xmlState->hasTagName(parameters.state.getType())) {
            ValueTree state = ValueTree::fromXml(*xmlState);
            parameters.replaceState(state);
            
            // Restore custom state
            if (state.hasProperty("roomId")) {
                {
                    const ScopedLock lock(roomIdMutex);
                    currentRoomId = state.getProperty("roomId").toString();
                }
            }
            if (state.hasProperty("webSocketPort")) {
                webSocketPort.store(state.getProperty("webSocketPort"));
            }
        }
    }
}

//==============================================================================
// WebSocket Event Handlers
//==============================================================================

void FLStreamProcessor::handleUserJoined(const String& userId, const String& roomId)
{
    logMessage("User joined: " + userId + " in room " + roomId);
    
    if (onUserJoined) {
        onUserJoined(userId, "Remote User");
    }
}

void FLStreamProcessor::handleUserLeft(const String& userId, const String& roomId)
{
    logMessage("User left: " + userId + " from room " + roomId);
    
    if (onUserLeft) {
        onUserLeft(userId);
    }
}

void FLStreamProcessor::handleAudioReceived(const OptimizedAudioPacket& packet, const String&)
{
    // Add received audio to incoming queue
    size_t writeIndex = incomingWriteIndex.load();
    size_t nextWriteIndex = (writeIndex + 1) % AUDIO_QUEUE_SIZE;
    
    if (nextWriteIndex != incomingReadIndex.load()) {
        incomingAudioQueue[writeIndex] = packet;
        incomingWriteIndex.store(nextWriteIndex);
    }
}

void FLStreamProcessor::handleWebSocketLog(const String& message)
{
    logMessage("WebSocket: " + message);
}

void FLStreamProcessor::logMessage(const String& message)
{
    if (onLogMessage) {
        onLogMessage(message);
    }
    
    DBG("FLStreamProcessor: " + message);
}

//==============================================================================
// DelayLine Implementation
//==============================================================================

void FLStreamProcessor::DelayLine::setSize(int numChannels, int maxDelaySamples)
{
    maxDelaySize = maxDelaySamples;
    delayBuffer.setSize(numChannels, maxDelaySize + 1);
    delayBuffer.clear();
    
    writePositions.resize(numChannels);
    std::fill(writePositions.begin(), writePositions.end(), 0);
}

void FLStreamProcessor::DelayLine::setDelay(int delaySamples)
{
    delayInSamples = jlimit(0, maxDelaySize, delaySamples);
}

void FLStreamProcessor::DelayLine::process(AudioBuffer<float>& buffer)
{
    if (delayInSamples == 0) return;
    
    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();
    
    for (int ch = 0; ch < numChannels; ++ch) {
        if (ch >= writePositions.size()) break;
        
        const float* input = buffer.getReadPointer(ch);
        float* output = buffer.getWritePointer(ch);
        float* delayData = delayBuffer.getWritePointer(ch);
        
        int writePos = writePositions[ch];
        
        for (int i = 0; i < numSamples; ++i) {
            // Read delayed sample
            int readPos = writePos - delayInSamples;
            if (readPos < 0) readPos += maxDelaySize;
            
            const float delayedSample = delayData[readPos];
            
            // Write current sample to delay buffer
            delayData[writePos] = input[i];
            
            // Output delayed sample
            output[i] = delayedSample;
            
            // Advance write position
            writePos = (writePos + 1) % maxDelaySize;
        }
        
        writePositions[ch] = writePos;
    }
}

void FLStreamProcessor::DelayLine::reset()
{
    delayBuffer.clear();
    std::fill(writePositions.begin(), writePositions.end(), 0);
}

//==============================================================================
// Audio Processor Factory
//==============================================================================

AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FLStreamProcessor();
}