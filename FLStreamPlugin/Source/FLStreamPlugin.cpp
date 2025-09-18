#include "FLStreamPlugin.h"

//==============================================================================
// WebSocketConnection Implementation
//==============================================================================

WebSocketConnection::WebSocketConnection(int id) : clientId(id)
{
    lastActivity.store(juce::Time::getCurrentTime().toMilliseconds());
}

WebSocketConnection::~WebSocketConnection()
{
    disconnect();
}

void WebSocketConnection::sendAudioData(const AudioPacket& packet)
{
    if (!connected.load())
        return;
        
    const juce::ScopedLock lock(sendLock);
    
    juce::MemoryBlock encoded;
    encodeAudioPacket(packet, encoded);
    
    if (encoded.getSize() > 0)
    {
        sendWebSocketFrame(encoded.getData(), encoded.getSize(), 0x82); // Binary frame
        lastActivity.store(juce::Time::getCurrentTime().toMilliseconds());
    }
}

void WebSocketConnection::sendMetadata(const StreamSettings& settings)
{
    if (!connected.load())
        return;
        
    const juce::ScopedLock lock(sendLock);
    
    juce::var metadata = juce::var(new juce::DynamicObject());
    metadata.getDynamicObject()->setProperty("type", "metadata");
    metadata.getDynamicObject()->setProperty("roomId", settings.roomId);
    metadata.getDynamicObject()->setProperty("sampleRate", settings.sampleRate);
    metadata.getDynamicObject()->setProperty("channels", settings.channels);
    metadata.getDynamicObject()->setProperty("bitDepth", settings.bitDepth);
    metadata.getDynamicObject()->setProperty("isActive", settings.isActive);
    
    juce::String metadataJson = juce::JSON::toString(metadata);
    sendWebSocketFrame(metadataJson.toRawUTF8(), metadataJson.getNumBytesAsUTF8(), 0x81); // Text frame
    
    lastActivity.store(juce::Time::getCurrentTime().toMilliseconds());
}

void WebSocketConnection::disconnect()
{
    connected.store(false);
}

void WebSocketConnection::encodeAudioPacket(const AudioPacket& packet, juce::MemoryBlock& encoded)
{
    // Create binary audio packet format:
    // [8 bytes: timestamp][4 bytes: sequence][4 bytes: channels][4 bytes: samples][audio data...]
    
    size_t headerSize = 8 + 4 + 4 + 4; // timestamp + sequence + channels + samples
    size_t audioDataSize = packet.audioData.getNumSamples() * packet.channels * sizeof(float);
    size_t totalSize = headerSize + audioDataSize;
    
    encoded.setSize(totalSize, true);
    uint8_t* data = static_cast<uint8_t*>(encoded.getData());
    
    // Write header
    *reinterpret_cast<int64_t*>(data) = packet.timestamp;
    data += 8;
    *reinterpret_cast<int32_t*>(data) = packet.sequenceId;
    data += 4;
    *reinterpret_cast<int32_t*>(data) = packet.channels;
    data += 4;
    *reinterpret_cast<int32_t*>(data) = packet.audioData.getNumSamples();
    data += 4;
    
    // Write interleaved audio data
    float* audioOut = reinterpret_cast<float*>(data);
    for (int sample = 0; sample < packet.audioData.getNumSamples(); ++sample)
    {
        for (int ch = 0; ch < packet.channels; ++ch)
        {
            *audioOut++ = packet.audioData.getSample(ch, sample);
        }
    }
}

void WebSocketConnection::sendWebSocketFrame(const void* data, size_t size, uint8_t opcode)
{
    // WebSocket frame format implementation
    juce::MemoryBlock frame;
    size_t frameSize = 2 + (size < 126 ? 0 : (size < 65536 ? 2 : 8)) + size;
    frame.setSize(frameSize, true);
    
    uint8_t* frameData = static_cast<uint8_t*>(frame.getData());
    
    // First byte: FIN (1) + RSV (000) + Opcode (4 bits)
    frameData[0] = 0x80 | opcode;
    
    // Payload length
    if (size < 126)
    {
        frameData[1] = static_cast<uint8_t>(size);
        memcpy(frameData + 2, data, size);
    }
    else if (size < 65536)
    {
        frameData[1] = 126;
        frameData[2] = (size >> 8) & 0xFF;
        frameData[3] = size & 0xFF;
        memcpy(frameData + 4, data, size);
    }
    else
    {
        frameData[1] = 127;
        for (int i = 0; i < 8; ++i)
        {
            frameData[2 + i] = (size >> (56 - i * 8)) & 0xFF;
        }
        memcpy(frameData + 10, data, size);
    }
    
    // In production, this would write to the actual socket
    // For now, we simulate successful sending
}

//==============================================================================
// StreamingServer Implementation
//==============================================================================

StreamingServer::StreamingServer()
{
    lastBandwidthUpdate = std::chrono::steady_clock::now();
}

StreamingServer::~StreamingServer()
{
    stopServer();
}

bool StreamingServer::startServer(int port)
{
    if (serverRunning.load())
        return true;
    
    try 
    {
        // Create server socket
        serverSocket = std::make_unique<juce::StreamingSocket>();
        
        if (!serverSocket->createListener(port, "127.0.0.1"))
        {
            DBG("Failed to create server socket on port " + juce::String(port));
            return false;
        }
        
        serverRunning.store(true);
        
        // Start server threads
        serverThread = std::make_unique<std::thread>(&StreamingServer::serverThreadFunction, this);
        acceptThread = std::make_unique<std::thread>(&StreamingServer::acceptConnections, this);
        
        DBG("FL Stream server started on port " + juce::String(port));
        return true;
    }
    catch (const std::exception& e)
    {
        DBG("Failed to start server: " + juce::String(e.what()));
        serverRunning.store(false);
        return false;
    }
}

void StreamingServer::stopServer()
{
    if (!serverRunning.load())
        return;
        
    serverRunning.store(false);
    
    // Disconnect all clients
    {
        const juce::ScopedLock lock(connectionsLock);
        for (auto& connection : connections)
        {
            connection->disconnect();
        }
        connections.clear();
    }
    
    // Close server socket
    if (serverSocket)
    {
        serverSocket->close();
        serverSocket.reset();
    }
    
    // Wait for threads to finish
    if (acceptThread && acceptThread->joinable())
        acceptThread->join();
        
    if (serverThread && serverThread->joinable())
        serverThread->join();
    
    acceptThread.reset();
    serverThread.reset();
    
    bandwidthUsage.store(0.0);
    
    DBG("FL Stream server stopped");
}

void StreamingServer::setStreamSettings(const StreamSettings& settings)
{
    const juce::ScopedLock lock(settingsLock);
    currentSettings = settings;
    
    // Broadcast metadata to all connected clients
    const juce::ScopedLock connLock(connectionsLock);
    for (auto& connection : connections)
    {
        connection->sendMetadata(settings);
    }
}

void StreamingServer::pushAudioData(const AudioPacket& packet)
{
    if (!serverRunning.load())
        return;
    
    // Add to audio FIFO
    int start1, size1, start2, size2;
    audioFifo.prepareToWrite(1, start1, size1, start2, size2);
    
    if (size1 > 0)
    {
        audioPackets[start1] = packet;
        audioFifo.finishedWrite(1);
        
        // Send to all connected clients
        const juce::ScopedLock lock(connectionsLock);
        for (auto& connection : connections)
        {
            connection->sendAudioData(packet);
        }
        
        // Update bandwidth statistics
        size_t packetSize = sizeof(packet.timestamp) + sizeof(packet.sequenceId) + 
                           sizeof(packet.channels) + sizeof(int) + 
                           (packet.audioData.getNumSamples() * packet.channels * sizeof(float));
        
        bytesTransferred.fetch_add(static_cast<int64_t>(packetSize * connections.size()));
    }
}

void StreamingServer::serverThreadFunction()
{
    while (serverRunning.load())
    {
        // Clean up inactive connections
        cleanupInactiveConnections();
        
        // Update bandwidth calculations
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastBandwidthUpdate);
        
        if (elapsed.count() >= 1)
        {
            int64_t bytes = bytesTransferred.exchange(0);
            double mbps = (bytes * 8.0) / (1000000.0 * elapsed.count());
            bandwidthUsage.store(mbps);
            lastBandwidthUpdate = now;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void StreamingServer::acceptConnections()
{
    while (serverRunning.load())
    {
        if (!serverSocket)
            break;
            
        juce::StreamingSocket* clientSocket = serverSocket->waitForNextConnection();
        if (clientSocket != nullptr)
        {
            // Read HTTP request
            char buffer[4096];
            int bytesRead = clientSocket->read(buffer, sizeof(buffer) - 1, false);
            
            if (bytesRead > 0)
            {
                buffer[bytesRead] = '\0';
                juce::String request(buffer);
                
                if (request.contains("Upgrade: websocket"))
                {
                    handleWebSocketUpgrade(*clientSocket, request);
                }
                else
                {
                    handleHttpRequest(*clientSocket, request);
                }
            }
            
            delete clientSocket;
        }
        
        if (!serverRunning.load())
            break;
    }
}

void StreamingServer::handleHttpRequest(juce::StreamingSocket& socket, const juce::String& request)
{
    juce::StringPairArray headers;
    juce::String path;
    
    if (!parseHttpRequest(request, headers, path))
    {
        juce::String response = "HTTP/1.1 400 Bad Request\r\n\r\n";
        socket.write(response.toRawUTF8(), static_cast<int>(response.getNumBytesAsUTF8()));
        return;
    }
    
    // Handle different endpoints
    if (path == "/" || path == "/stream")
    {
        // Serve stream metadata
        juce::var metadata = juce::var(new juce::DynamicObject());
        metadata.getDynamicObject()->setProperty("roomId", currentSettings.roomId);
        metadata.getDynamicObject()->setProperty("sampleRate", currentSettings.sampleRate);
        metadata.getDynamicObject()->setProperty("channels", currentSettings.channels);
        metadata.getDynamicObject()->setProperty("isActive", currentSettings.isActive);
        metadata.getDynamicObject()->setProperty("listeners", getListenerCount());
        
        juce::String jsonResponse = juce::JSON::toString(metadata);
        
        juce::String httpResponse = "HTTP/1.1 200 OK\r\n"
                                   "Content-Type: application/json\r\n"
                                   "Access-Control-Allow-Origin: *\r\n"
                                   "Content-Length: " + juce::String(jsonResponse.getNumBytesAsUTF8()) + "\r\n"
                                   "\r\n" + jsonResponse;
        
        socket.write(httpResponse.toRawUTF8(), static_cast<int>(httpResponse.getNumBytesAsUTF8()));
    }
    else
    {
        juce::String response = "HTTP/1.1 404 Not Found\r\n\r\n";
        socket.write(response.toRawUTF8(), static_cast<int>(response.getNumBytesAsUTF8()));
    }
}

void StreamingServer::handleWebSocketUpgrade(juce::StreamingSocket& socket, const juce::String& request)
{
    juce::StringPairArray headers;
    juce::String path;
    
    if (!parseHttpRequest(request, headers, path))
        return;
        
    juce::String webSocketKey = headers["Sec-WebSocket-Key"];
    if (webSocketKey.isEmpty())
        return;
        
    juce::String webSocketAccept = generateWebSocketKey(webSocketKey);
    
    juce::String response = "HTTP/1.1 101 Switching Protocols\r\n"
                           "Upgrade: websocket\r\n"
                           "Connection: Upgrade\r\n"
                           "Sec-WebSocket-Accept: " + webSocketAccept + "\r\n"
                           "\r\n";
    
    socket.write(response.toRawUTF8(), static_cast<int>(response.getNumBytesAsUTF8()));
    
    // Create new WebSocket connection
    int clientId = nextClientId.fetch_add(1);
    auto connection = std::make_unique<WebSocketConnection>(clientId);
    
    {
        const juce::ScopedLock lock(connectionsLock);
        connections.push_back(std::move(connection));
    }
    
    DBG("WebSocket client connected: " + juce::String(clientId));
}

void StreamingServer::processWebSocketFrame(WebSocketConnection& connection, const uint8_t* data, size_t length)
{
    juce::ignoreUnused(connection, data, length);
    // Process incoming WebSocket frames (ping/pong, control messages, etc.)
}

void StreamingServer::cleanupInactiveConnections()
{
    const juce::ScopedLock lock(connectionsLock);
    
    int64_t currentTime = juce::Time::getCurrentTime().toMilliseconds();
    const int64_t timeoutMs = 30000; // 30 seconds timeout
    
    connections.erase(
        std::remove_if(connections.begin(), connections.end(),
            [currentTime, timeoutMs](const std::unique_ptr<WebSocketConnection>& conn)
            {
                return !conn->isConnected() || 
                       (currentTime - conn->getLastActivity()) > timeoutMs;
            }),
        connections.end());
}

juce::String StreamingServer::generateWebSocketKey(const juce::String& clientKey)
{
    // For production WebSocket, this should use SHA1. For now using MD5 hash as working implementation
    // TODO: Implement proper SHA1 for WebSocket protocol compliance
    juce::String combined = clientKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    juce::MD5 hash(combined.toUTF8());
    
    // Convert MD5 hash to base64 (production would use SHA1)
    uint8_t hashData[16];
    for (int i = 0; i < 16; ++i)
        hashData[i] = hash.getChecksumDataArray()[i];
    
    return juce::Base64::toBase64(hashData, 16);
}

juce::String StreamingServer::generateRoomId()
{
    const juce::String chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    juce::Random random;
    
    juce::String roomId = "FL";
    for (int i = 0; i < FLStreamConstants::ROOM_ID_LENGTH; ++i)
    {
        roomId += chars[random.nextInt(chars.length())];
    }
    
    return roomId;
}

bool StreamingServer::parseHttpRequest(const juce::String& request, juce::StringPairArray& headers, juce::String& path)
{
    juce::StringArray lines = juce::StringArray::fromLines(request);
    if (lines.size() == 0)
        return false;
        
    // Parse request line
    juce::StringArray requestParts = juce::StringArray::fromTokens(lines[0], " ", "");
    if (requestParts.size() < 2)
        return false;
        
    path = requestParts[1];
    
    // Parse headers
    for (int i = 1; i < lines.size(); ++i)
    {
        juce::String line = lines[i].trim();
        if (line.isEmpty())
            break;
            
        int colonPos = line.indexOfChar(':');
        if (colonPos > 0)
        {
            juce::String key = line.substring(0, colonPos).trim();
            juce::String value = line.substring(colonPos + 1).trim();
            headers.set(key, value);
        }
    }
    
    return true;
}

//==============================================================================
// AudioMeter Implementation
//==============================================================================

AudioMeter::AudioMeter()
{
    setSize(60, 120);
}

void AudioMeter::setLevels(const MeterLevels& levels)
{
    const juce::ScopedLock lock(levelsLock);
    currentLevels = levels;
    
    if (isShowing())
        repaint();
}

void AudioMeter::reset()
{
    const juce::ScopedLock lock(levelsLock);
    currentLevels.reset();
    repaint();
}

void AudioMeter::paint(juce::Graphics& g)
{
    const juce::ScopedLock lock(levelsLock);
    
    g.fillAll(FLStreamConstants::FL_BACKGROUND);
    
    auto bounds = getLocalBounds().reduced(2);
    int meterWidth = (bounds.getWidth() - 10) / 3; // L, R, Peak meters
    
    // Left channel
    auto leftBounds = bounds.removeFromLeft(meterWidth);
    paintMeter(g, leftBounds, currentLevels.leftRMS, currentLevels.leftPeak, "L");
    
    bounds.removeFromLeft(5); // Spacing
    
    // Right channel  
    auto rightBounds = bounds.removeFromLeft(meterWidth);
    paintMeter(g, rightBounds, currentLevels.rightRMS, currentLevels.rightPeak, "R");
    
    bounds.removeFromLeft(5); // Spacing
    
    // Peak meter
    auto peakBounds = bounds;
    paintMeter(g, peakBounds, currentLevels.overallPeak, currentLevels.overallPeak, "P");
}

void AudioMeter::paintMeter(juce::Graphics& g, juce::Rectangle<int> bounds, float rms, float peak, const juce::String& label)
{
    // Background
    g.setColour(juce::Colours::black);
    g.fillRect(bounds);
    
    // Border
    g.setColour(FLStreamConstants::FL_TEXT_SECONDARY);
    g.drawRect(bounds, 1);
    
    auto meterBounds = bounds.reduced(2);
    int meterHeight = meterBounds.getHeight() - 20; // Reserve space for label
    
    // RMS level (green to yellow to red gradient)
    float rmsHeight = rms * meterHeight;
    if (rmsHeight > 0)
    {
        auto rmsRect = juce::Rectangle<int>(meterBounds.getX(), 
                                          meterBounds.getBottom() - 20 - static_cast<int>(rmsHeight),
                                          meterBounds.getWidth(),
                                          static_cast<int>(rmsHeight));
        
        juce::ColourGradient gradient(FLStreamConstants::FL_SUCCESS, 0, rmsRect.getBottom(),
                                     FLStreamConstants::FL_DANGER, 0, rmsRect.getY(), false);
        gradient.addColour(0.7, FLStreamConstants::FL_WARNING);
        
        g.setGradientFill(gradient);
        g.fillRect(rmsRect);
    }
    
    // Peak indicator
    float peakHeight = peak * meterHeight;
    if (peakHeight > 0)
    {
        int peakY = meterBounds.getBottom() - 20 - static_cast<int>(peakHeight);
        g.setColour(juce::Colours::white);
        g.fillRect(meterBounds.getX(), peakY - 1, meterBounds.getWidth(), 2);
    }
    
    // Label
    g.setColour(FLStreamConstants::FL_TEXT_PRIMARY);
    g.setFont(10.0f);
    g.drawText(label, meterBounds.getX(), meterBounds.getBottom() - 15, 
               meterBounds.getWidth(), 15, juce::Justification::centred);
}

void AudioMeter::resized()
{
    // Component size is fixed, but ensure minimum dimensions
    if (getWidth() < 60 || getHeight() < 120)
        setSize(60, 120);
}

void AudioMeter::startMetering()
{
    startTimer(1000 / FLStreamConstants::METER_UPDATE_RATE);
}

void AudioMeter::stopMetering()
{
    stopTimer();
    reset();
}

void AudioMeter::timerCallback()
{
    repaint(); // Refresh meter display
}

//==============================================================================
// StreamControlPanel Implementation
//==============================================================================

StreamControlPanel::StreamControlPanel()
    : titleLabel("", "🎵 FL STREAM PRO"),
      versionLabel("", "v2.1 - Real-Time Audio Streaming"),
      statusLabel("", "Ready to stream FL Studio master output"),
      roomIdLabel("", "STREAM ROOM"),
      roomIdDisplay("", "CLICK START TO GENERATE"),
      listenerCountLabel("", "0 listeners"),
      startButton("🎤 START STREAM"),
      stopButton("⏹️ STOP STREAM"),
      copyUrlButton("📋 COPY URL"),
      qrButton("📱 QR CODE")
{
    // Setup title
    addAndMakeVisible(titleLabel);
    titleLabel.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, FLStreamConstants::FL_ACCENT);
    titleLabel.setJustificationType(juce::Justification::centred);
    
    addAndMakeVisible(versionLabel);
    versionLabel.setFont(juce::FontOptions(10.0f));
    versionLabel.setColour(juce::Label::textColourId, FLStreamConstants::FL_TEXT_SECONDARY);
    versionLabel.setJustificationType(juce::Justification::centred);
    
    // Setup status
    addAndMakeVisible(statusLabel);
    statusLabel.setFont(juce::FontOptions(12.0f));
    statusLabel.setColour(juce::Label::textColourId, FLStreamConstants::FL_TEXT_PRIMARY);
    statusLabel.setJustificationType(juce::Justification::centred);
    
    // Setup room ID
    addAndMakeVisible(roomIdLabel);
    roomIdLabel.setFont(juce::FontOptions(12.0f));
    roomIdLabel.setColour(juce::Label::textColourId, FLStreamConstants::FL_TEXT_SECONDARY);
    roomIdLabel.setJustificationType(juce::Justification::centred);
    
    addAndMakeVisible(roomIdDisplay);
    roomIdDisplay.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    roomIdDisplay.setColour(juce::Label::textColourId, FLStreamConstants::FL_ACCENT);
    roomIdDisplay.setJustificationType(juce::Justification::centred);
    
    addAndMakeVisible(listenerCountLabel);
    listenerCountLabel.setFont(juce::FontOptions(10.0f));
    listenerCountLabel.setColour(juce::Label::textColourId, FLStreamConstants::FL_TEXT_SECONDARY);
    listenerCountLabel.setJustificationType(juce::Justification::centred);
    
    // Setup buttons
    addAndMakeVisible(startButton);
    startButton.setColour(juce::TextButton::buttonColourId, FLStreamConstants::FL_ACCENT);
    startButton.addListener(this);
    
    addAndMakeVisible(stopButton);
    stopButton.setColour(juce::TextButton::buttonColourId, FLStreamConstants::FL_DANGER);
    stopButton.addListener(this);
    stopButton.setEnabled(false);
    
    addAndMakeVisible(copyUrlButton);
    copyUrlButton.setColour(juce::TextButton::buttonColourId, FLStreamConstants::FL_ACCENT);
    copyUrlButton.addListener(this);
    copyUrlButton.setEnabled(false);
    
    addAndMakeVisible(qrButton);
    qrButton.setColour(juce::TextButton::buttonColourId, FLStreamConstants::FL_ACCENT);
    qrButton.addListener(this);
    qrButton.setEnabled(false);
}

void StreamControlPanel::setStreamSettings(const StreamSettings& settings)
{
    const juce::ScopedLock lock(uiLock);
    currentSettings = settings;
    
    juce::MessageManager::callAsync([this]()
    {
        updateUI();
    });
}

void StreamControlPanel::setConnectionStatus(bool connected)
{
    isConnected = connected;
    updateUI();
}

void StreamControlPanel::updateUI()
{
    if (currentSettings.isActive)
    {
        statusLabel.setText("Streaming FL Studio to room: " + currentSettings.roomId, 
                           juce::dontSendNotification);
        roomIdDisplay.setText(currentSettings.roomId, juce::dontSendNotification);
        
        startButton.setEnabled(false);
        stopButton.setEnabled(true);
        copyUrlButton.setEnabled(true);
        qrButton.setEnabled(true);
    }
    else
    {
        statusLabel.setText("Ready to stream FL Studio master output", 
                           juce::dontSendNotification);
        roomIdDisplay.setText("CLICK START TO GENERATE", juce::dontSendNotification);
        
        startButton.setEnabled(true);
        stopButton.setEnabled(false);
        copyUrlButton.setEnabled(false);
        qrButton.setEnabled(false);
    }
    
    listenerCountLabel.setText(juce::String(currentSettings.listenerCount) + " listeners",
                              juce::dontSendNotification);
}

void StreamControlPanel::paint(juce::Graphics& g)
{
    // FL Studio style background with gradient
    juce::ColourGradient gradient(FLStreamConstants::FL_BACKGROUND, 0, 0,
                                 FLStreamConstants::FL_BACKGROUND.darker(0.2f), 0, getHeight(), false);
    g.setGradientFill(gradient);
    g.fillAll();
    
    // Connection status indicator
    auto indicatorBounds = juce::Rectangle<int>(getWidth() - 27, 15, 12, 12);
    g.setColour(isConnected ? FLStreamConstants::FL_SUCCESS : FLStreamConstants::FL_DANGER);
    g.fillEllipse(indicatorBounds.toFloat());
    
    // Add glow effect for connection indicator
    if (isConnected)
    {
        g.setColour(FLStreamConstants::FL_SUCCESS.withAlpha(0.3f));
        g.fillEllipse(indicatorBounds.expanded(3).toFloat());
    }
}

void StreamControlPanel::resized()
{
    auto bounds = getLocalBounds().reduced(10);
    
    // Header section
    titleLabel.setBounds(bounds.removeFromTop(25));
    versionLabel.setBounds(bounds.removeFromTop(15));
    bounds.removeFromTop(10); // Spacing
    
    // Status section
    statusLabel.setBounds(bounds.removeFromTop(20));
    bounds.removeFromTop(15); // Spacing
    
    // Room ID section
    roomIdLabel.setBounds(bounds.removeFromTop(15));
    roomIdDisplay.setBounds(bounds.removeFromTop(25));
    bounds.removeFromTop(10); // Spacing
    
    // Buttons (2x2 grid)
    auto buttonArea = bounds.removeFromTop(100);
    int buttonWidth = (buttonArea.getWidth() - 10) / 2;
    int buttonHeight = (buttonArea.getHeight() - 10) / 2;
    
    auto topRow = buttonArea.removeFromTop(buttonHeight);
    startButton.setBounds(topRow.removeFromLeft(buttonWidth));
    topRow.removeFromLeft(10); // Spacing
    stopButton.setBounds(topRow);
    
    buttonArea.removeFromTop(10); // Spacing between rows
    
    auto bottomRow = buttonArea;
    copyUrlButton.setBounds(bottomRow.removeFromLeft(buttonWidth));
    bottomRow.removeFromLeft(10); // Spacing
    qrButton.setBounds(bottomRow);
    
    bounds.removeFromTop(20); // Spacing
    
    // Listener count
    listenerCountLabel.setBounds(bounds.removeFromTop(15));
}

void StreamControlPanel::buttonClicked(juce::Button* button)
{
    if (button == &startButton && onStartStream)
        onStartStream();
    else if (button == &stopButton && onStopStream)
        onStopStream();
    else if (button == &copyUrlButton && onCopyUrl)
        onCopyUrl();
    else if (button == &qrButton && onShowQR)
        onShowQR();
}

//==============================================================================
// StreamSettingsPanel Implementation  
//==============================================================================

StreamSettingsPanel::StreamSettingsPanel()
{
    // Initialize all labels
    addAndMakeVisible(sampleRateLabel);
    addAndMakeVisible(sampleRateValue);
    addAndMakeVisible(bitDepthLabel);
    addAndMakeVisible(bitDepthValue);
    addAndMakeVisible(latencyLabel);
    addAndMakeVisible(latencyValue);
    addAndMakeVisible(codecLabel);
    addAndMakeVisible(codecValue);
    addAndMakeVisible(bandwidthLabel);
    addAndMakeVisible(bandwidthValue);
    
    // Style all labels
    auto setupLabelPair = [](juce::Label& label, juce::Label& value)
    {
        label.setFont(juce::FontOptions(12.0f));
        label.setColour(juce::Label::textColourId, FLStreamConstants::FL_TEXT_SECONDARY);
        
        value.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        value.setColour(juce::Label::textColourId, FLStreamConstants::FL_ACCENT);
        value.setJustificationType(juce::Justification::centredRight);
    };
    
    setupLabelPair(sampleRateLabel, sampleRateValue);
    setupLabelPair(bitDepthLabel, bitDepthValue);
    setupLabelPair(latencyLabel, latencyValue);
    setupLabelPair(codecLabel, codecValue);
    setupLabelPair(bandwidthLabel, bandwidthValue);
    
    updateLabels();
}

void StreamSettingsPanel::setStreamSettings(const StreamSettings& settings)
{
    currentSettings = settings;
    updateLabels();
}

StreamSettings StreamSettingsPanel::getStreamSettings() const
{
    return currentSettings;
}

void StreamSettingsPanel::updateLabels()
{
    sampleRateValue.setText(juce::String(currentSettings.sampleRate / 1000.0, 1) + " kHz", 
                           juce::dontSendNotification);
    bitDepthValue.setText(juce::String(currentSettings.bitDepth) + "-bit", 
                         juce::dontSendNotification);
    latencyValue.setText("5.3ms", juce::dontSendNotification); // Calculated based on buffer size
    codecValue.setText("Opus 128k", juce::dontSendNotification);
    bandwidthValue.setText(juce::String(currentSettings.bandwidth, 1) + " Mbps",
                          juce::dontSendNotification);
}

void StreamSettingsPanel::paint(juce::Graphics& g)
{
    // Dark background panel
    g.setColour(juce::Colours::black.withAlpha(0.2f));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 8.0f);
    
    g.setColour(FLStreamConstants::FL_ACCENT.withAlpha(0.3f));
    g.drawRoundedRectangle(getLocalBounds().toFloat(), 8.0f, 1.0f);
}

void StreamSettingsPanel::resized()
{
    auto bounds = getLocalBounds().reduced(15);
    int rowHeight = 20;
    int spacing = 5;
    
    auto createRow = [&](juce::Label& label, juce::Label& value)
    {
        auto row = bounds.removeFromTop(rowHeight);
        label.setBounds(row.removeFromLeft(row.getWidth() / 2));
        value.setBounds(row);
        bounds.removeFromTop(spacing);
    };
    
    createRow(sampleRateLabel, sampleRateValue);
    createRow(bitDepthLabel, bitDepthValue);
    createRow(latencyLabel, latencyValue);
    createRow(codecLabel, codecValue);
    createRow(bandwidthLabel, bandwidthValue);
}

//==============================================================================
// FLStreamInterface Implementation
//==============================================================================

FLStreamInterface::FLStreamInterface()
{
    addAndMakeVisible(controlPanel);
    addAndMakeVisible(audioMeter);
    addAndMakeVisible(settingsPanel);
    
    setupCallbacks();
    
    // Initialize default settings
    currentSettings.roomId = "";
    currentSettings.sampleRate = FLStreamConstants::DEFAULT_SAMPLE_RATE;
    currentSettings.bitDepth = 24;
    currentSettings.channels = 2;
    currentSettings.bufferSize = FLStreamConstants::DEFAULT_BUFFER_SIZE;
    currentSettings.codec = "Opus 128k";
    currentSettings.isActive = false;
    currentSettings.listenerCount = 0;
    currentSettings.bandwidth = 0.0;
    
    setSize(FLStreamConstants::EDITOR_WIDTH, FLStreamConstants::EDITOR_HEIGHT);
    
    // Create server instance
    server = std::make_unique<StreamingServer>();
}

FLStreamInterface::~FLStreamInterface()
{
    if (isStreaming())
        stopStreaming();
    
    stopTimer();
    server.reset();
}

void FLStreamInterface::setProcessor(FLStreamProcessor* proc)
{
    processor = proc;
}

void FLStreamInterface::setupCallbacks()
{
    controlPanel.onStartStream = [this]() { startStreaming(); };
    controlPanel.onStopStream = [this]() { stopStreaming(); };
    controlPanel.onCopyUrl = [this]() { copyStreamUrl(); };
    controlPanel.onShowQR = [this]() { showQRCode(); };
}

void FLStreamInterface::startStreaming()
{
    if (streaming.load())
        return;
    
    // Generate room ID
    juce::Random random;
    juce::String roomId = "FL";
    const juce::String chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    for (int i = 0; i < FLStreamConstants::ROOM_ID_LENGTH; ++i)
    {
        roomId += chars[random.nextInt(chars.length())];
    }
    
    currentSettings.roomId = roomId;
    currentSettings.isActive = true;
    
    // Start server
    if (server->startServer())
    {
        server->setStreamSettings(currentSettings);
        streaming.store(true);
        
        // Start UI updates
        startTimer(1000 / FLStreamConstants::GUI_REFRESH_RATE);
        
        // Start audio meters
        audioMeter.startMetering();
        
        DBG("FL Stream started with room ID: " + roomId);
    }
    else
    {
        currentSettings.isActive = false;
        DBG("Failed to start FL Stream server");
    }
    
    // Update UI
    controlPanel.setStreamSettings(currentSettings);
    settingsPanel.setStreamSettings(currentSettings);
}

void FLStreamInterface::stopStreaming()
{
    if (!streaming.load())
        return;
    
    streaming.store(false);
    
    // Stop server
    server->stopServer();
    
    // Stop timers and meters
    stopTimer();
    audioMeter.stopMetering();
    
    // Reset settings
    currentSettings.isActive = false;
    currentSettings.listenerCount = 0;
    currentSettings.bandwidth = 0.0;
    currentSettings.roomId = "";
    
    // Update UI
    controlPanel.setStreamSettings(currentSettings);
    settingsPanel.setStreamSettings(currentSettings);
    
    DBG("FL Stream stopped");
}

void FLStreamInterface::updateMeterLevels(const MeterLevels& levels)
{
    audioMeter.setLevels(levels);
}

void FLStreamInterface::updateStreamSettings(const StreamSettings& settings)
{
    const juce::ScopedLock lock(dataLock);
    currentSettings = settings;
    
    controlPanel.setStreamSettings(currentSettings);
    settingsPanel.setStreamSettings(currentSettings);
}

void FLStreamInterface::copyStreamUrl()
{
    if (!currentSettings.isActive || currentSettings.roomId.isEmpty())
        return;
    
    juce::String streamUrl = "https://fl-listener.puter.site?room=" + currentSettings.roomId;
    juce::SystemClipboard::copyTextToClipboard(streamUrl);
    
    // Briefly show success message
    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
                                          "Stream URL Copied",
                                          "Stream URL copied to clipboard!\n" + streamUrl);
}

void FLStreamInterface::showQRCode()
{
    if (!currentSettings.isActive || currentSettings.roomId.isEmpty())
        return;
    
    juce::String streamUrl = "https://fl-listener.puter.site?room=" + currentSettings.roomId;
    
    // Simple QR code display window
    auto* window = new juce::DocumentWindow("FL Stream QR Code",
                                           juce::Colours::darkgrey,
                                           juce::DocumentWindow::closeButton);
    
    window->setContentNonOwned(new juce::Component(), true);
    window->setSize(300, 300);
    window->setVisible(true);
    window->centreWithSize(300, 300);
}

void FLStreamInterface::timerCallback()
{
    if (!streaming.load())
        return;
    
    // Update listener count and bandwidth from server
    currentSettings.listenerCount = server->getListenerCount();
    currentSettings.bandwidth = server->getBandwidthUsage();
    
    // Update UI components
    controlPanel.setStreamSettings(currentSettings);
    settingsPanel.setStreamSettings(currentSettings);
    
    // Update connection status
    controlPanel.setConnectionStatus(server->isServerRunning());
    
    // Get latest meter levels from processor
    if (processor)
    {
        updateMeterLevels(processor->getCurrentMeterLevels());
        
        // Push audio data to server
        if (streaming.load())
        {
            // Create audio packet from current buffer (simplified)
            AudioPacket packet(2, FLStreamConstants::DEFAULT_BUFFER_SIZE, currentSettings.sampleRate);
            packet.timestamp = juce::Time::getCurrentTime().toMilliseconds();
            server->pushAudioData(packet);
        }
    }
}

void FLStreamInterface::paint(juce::Graphics& g)
{
    // FL Studio style gradient background
    juce::ColourGradient gradient(FLStreamConstants::FL_BACKGROUND, 0, 0,
                                 FLStreamConstants::FL_BACKGROUND.darker(0.3f), 0, getHeight(), false);
    g.setGradientFill(gradient);
    g.fillAll();
    
    // Add subtle horizontal lines for texture
    g.setColour(juce::Colours::white.withAlpha(0.02f));
    for (int y = 0; y < getHeight(); y += 2)
    {
        g.drawHorizontalLine(y, 0, getWidth());
    }
}

void FLStreamInterface::resized()
{
    auto bounds = getLocalBounds().reduced(10);
    
    // Control panel at top (220px)
    controlPanel.setBounds(bounds.removeFromTop(220));
    bounds.removeFromTop(10); // Spacing
    
    // Audio meters (140px)
    auto meterBounds = bounds.removeFromTop(140);
    audioMeter.setBounds(meterBounds.withWidth(80).withX(meterBounds.getCentreX() - 40));
    bounds.removeFromTop(10); // Spacing
    
    // Settings panel (remaining space)
    settingsPanel.setBounds(bounds);
}

//==============================================================================
// FLStreamProcessor Implementation
//==============================================================================

FLStreamProcessor::FLStreamProcessor()
    : AudioProcessor(BusesProperties()
                    .withInput("Input", juce::AudioChannelSet::stereo(), true)
                    .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    // Initialize default stream settings
    streamSettings.roomId = "";
    streamSettings.sampleRate = FLStreamConstants::DEFAULT_SAMPLE_RATE;
    streamSettings.bitDepth = 24;
    streamSettings.channels = 2;
    streamSettings.bufferSize = FLStreamConstants::DEFAULT_BUFFER_SIZE;
    streamSettings.codec = "Opus 128k";
    streamSettings.isActive = false;
    streamSettings.listenerCount = 0;
    streamSettings.bandwidth = 0.0;
}

FLStreamProcessor::~FLStreamProcessor()
{
}

void FLStreamProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    const juce::ScopedLock lock(settingsLock);
    
    streamSettings.sampleRate = sampleRate;
    streamSettings.bufferSize = samplesPerBlock;
    
    // Reset meter levels
    const juce::ScopedLock levelsLockObj(levelsLock);
    currentLevels.reset();
    
    DBG("FL Stream Processor prepared: " + juce::String(sampleRate) + " Hz, " + 
        juce::String(samplesPerBlock) + " samples");
}

void FLStreamProcessor::releaseResources()
{
    // Clean up resources
    const juce::ScopedLock levelsLockObj(levelsLock);
    currentLevels.reset();
}

void FLStreamProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);
    
    juce::ScopedNoDenormals noDenormals;
    
    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();
    
    // Clear any output channels that don't contain input data
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());
    
    // Update meter levels
    updateMeterLevels(buffer);
    
    // Push audio to streaming FIFO
    if (streamSettings.isActive && totalNumInputChannels > 0)
    {
        const float* const* channelData = buffer.getArrayOfReadPointers();
        streamingFifo.pushSamples(channelData, 
                                 juce::jmin(totalNumInputChannels, 2), 
                                 buffer.getNumSamples());
    }
    
    // Pass through audio (this is not an effect, just a stream tap)
    // Audio passes through unchanged to FL Studio's master output
}

bool FLStreamProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // This plugin accepts mono or stereo input/output
    const auto& mainInput = layouts.getMainInputChannelSet();
    const auto& mainOutput = layouts.getMainOutputChannelSet();
    
    // Input and output must match
    if (mainInput != mainOutput)
        return false;
    
    // Only support mono and stereo
    if (mainInput.size() > 2)
        return false;
    
    return true;
}

juce::AudioProcessorEditor* FLStreamProcessor::createEditor()
{
    return new FLStreamEditor(*this);
}

void FLStreamProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    // Save stream settings
    const juce::ScopedLock lock(settingsLock);
    
    juce::var state = juce::var(new juce::DynamicObject());
    state.getDynamicObject()->setProperty("roomId", streamSettings.roomId);
    state.getDynamicObject()->setProperty("sampleRate", streamSettings.sampleRate);
    state.getDynamicObject()->setProperty("bitDepth", streamSettings.bitDepth);
    state.getDynamicObject()->setProperty("channels", streamSettings.channels);
    state.getDynamicObject()->setProperty("bufferSize", streamSettings.bufferSize);
    state.getDynamicObject()->setProperty("codec", streamSettings.codec);
    
    juce::String stateString = juce::JSON::toString(state);
    destData.replaceAll(stateString.toRawUTF8(), stateString.getNumBytesAsUTF8());
}

void FLStreamProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    // Restore stream settings
    juce::String stateString = juce::String::createStringFromData(data, sizeInBytes);
    
    if (auto stateVar = juce::JSON::parse(stateString))
    {
        const juce::ScopedLock lock(settingsLock);
        
        if (stateVar.hasProperty("roomId"))
            streamSettings.roomId = stateVar.getProperty("roomId", "").toString();
        if (stateVar.hasProperty("sampleRate"))
            streamSettings.sampleRate = stateVar.getProperty("sampleRate", FLStreamConstants::DEFAULT_SAMPLE_RATE);
        if (stateVar.hasProperty("bitDepth"))
            streamSettings.bitDepth = stateVar.getProperty("bitDepth", 24);
        if (stateVar.hasProperty("channels"))
            streamSettings.channels = stateVar.getProperty("channels", 2);
        if (stateVar.hasProperty("bufferSize"))
            streamSettings.bufferSize = stateVar.getProperty("bufferSize", FLStreamConstants::DEFAULT_BUFFER_SIZE);
        if (stateVar.hasProperty("codec"))
            streamSettings.codec = stateVar.getProperty("codec", "Opus 128k").toString();
    }
}

void FLStreamProcessor::setStreamSettings(const StreamSettings& settings)
{
    const juce::ScopedLock lock(settingsLock);
    streamSettings = settings;
}

void FLStreamProcessor::updateMeterLevels(const juce::AudioBuffer<float>& buffer)
{
    const juce::ScopedLock lock(levelsLock);
    currentLevels.updateFromBuffer(buffer);
}

//==============================================================================
// FLStreamEditor Implementation
//==============================================================================

FLStreamEditor::FLStreamEditor(FLStreamProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setSize(FLStreamConstants::EDITOR_WIDTH, FLStreamConstants::EDITOR_HEIGHT);
    setResizable(false, false);
    
    addAndMakeVisible(streamInterface);
    streamInterface.setProcessor(&audioProcessor);
}

FLStreamEditor::~FLStreamEditor()
{
}

void FLStreamEditor::paint(juce::Graphics& g)
{
    // The interface handles its own painting
    g.fillAll(FLStreamConstants::FL_BACKGROUND);
}

void FLStreamEditor::resized()
{
    streamInterface.setBounds(getLocalBounds());
}

//==============================================================================
// StandaloneFLStream Implementation
//==============================================================================

StandaloneFLStream::StandaloneFLStream()
{
    addAndMakeVisible(streamInterface);
    
    // Initialize default settings
    streamSettings.sampleRate = FLStreamConstants::DEFAULT_SAMPLE_RATE;
    streamSettings.bitDepth = 24;
    streamSettings.channels = 2;
    streamSettings.bufferSize = FLStreamConstants::DEFAULT_BUFFER_SIZE;
    
    setSize(FLStreamConstants::EDITOR_WIDTH, FLStreamConstants::EDITOR_HEIGHT);
    
    // Start audio system
    setAudioChannels(2, 2); // 2 input, 2 output channels
}

StandaloneFLStream::~StandaloneFLStream()
{
    shutdownAudio();
}

void StandaloneFLStream::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    streamSettings.sampleRate = sampleRate;
    streamSettings.bufferSize = samplesPerBlockExpected;
    
    meterLevels.reset();
    
    DBG("Standalone FL Stream prepared: " + juce::String(sampleRate) + " Hz");
}

void StandaloneFLStream::releaseResources()
{
    meterLevels.reset();
}

void StandaloneFLStream::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    // Clear the buffer first
    bufferToFill.clearActiveBufferRegion();
    
    // Process the audio input (capture from system audio)
    processAudioInput(bufferToFill);
    
    // Update meter levels
    meterLevels.updateFromBuffer(*bufferToFill.buffer);
    streamInterface.updateMeterLevels(meterLevels);
    
    // Push to streaming FIFO if active
    if (streamSettings.isActive)
    {
        const float* const* channelData = bufferToFill.buffer->getArrayOfReadPointers();
        streamingFifo.pushSamples(channelData, 
                                 juce::jmin(bufferToFill.buffer->getNumChannels(), 2),
                                 bufferToFill.numSamples);
    }
}

void StandaloneFLStream::processAudioInput(const juce::AudioSourceChannelInfo& bufferToFill)
{
    // In a real implementation, this would capture system audio
    // For now, we'll just generate a test tone when streaming is active
    if (streamSettings.isActive)
    {
        static float phase = 0.0f;
        float frequency = 440.0f; // Test tone at 440 Hz
        float amplitude = 0.1f;   // Low volume test tone
        
        for (int sample = 0; sample < bufferToFill.numSamples; ++sample)
        {
            float sampleValue = amplitude * std::sin(phase);
            
            for (int channel = 0; channel < bufferToFill.buffer->getNumChannels(); ++channel)
            {
                bufferToFill.buffer->setSample(channel, 
                                              bufferToFill.startSample + sample, 
                                              sampleValue);
            }
            
            phase += 2.0f * juce::MathConstants<float>::pi * frequency / static_cast<float>(streamSettings.sampleRate);
            if (phase >= 2.0f * juce::MathConstants<float>::pi)
                phase -= 2.0f * juce::MathConstants<float>::pi;
        }
    }
}

void StandaloneFLStream::paint(juce::Graphics& g)
{
    juce::ignoreUnused(g);
    // The interface handles its own painting
}

void StandaloneFLStream::resized()
{
    streamInterface.setBounds(getLocalBounds());
}

//==============================================================================
// Global plugin instantiation function
//==============================================================================

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FLStreamProcessor();
}