#include "FLStreamEditor.h"

// Production Colyseus server endpoint for voice.latticeworks-ai.com
#if JUCE_ANDROID
const String localColouseusServerAddress = "https://voice.latticeworks-ai.com/";
#else
const String localColouseusServerAddress = "https://voice.latticeworks-ai.com/";
#endif

// Implementation moved to header file

//==============================================================================
FLStreamEditor::FLStreamEditor(FLStreamProcessor& p)
    : AudioProcessorEditor(p), 
      processorRef(p)
{
    setOpaque(true);

    // WebBrowserComponent navigation UI disabled for fixed voice.latticeworks-ai.com endpoint
    
    // Configure WebView for VST/AU hosts, native controls for standalone application
#if JUCE_STANDALONE_APPLICATION
    currentUIMode = UIMode::Native;
    std::cout << "FL Stream: Standalone mode detected - using native UI" << std::endl;
#else
    currentUIMode = UIMode::WebView;
    std::cout << "FL Stream: VST/AU mode detected - using WebView interface" << std::endl;
#endif
    
    std::cout << "FL Stream: DEBUG - After mode detection, proceeding to UI creation" << std::endl;
    
    // Suppress unused parameter warning for processorRef
    (void)processorRef;

    // Use WebView for reliable connection
    currentUIMode = UIMode::WebView;
    
    // Create WebView for all cases to ensure reliable connection
    if (currentUIMode == UIMode::WebView) {
        // Instantiate FLStreamWebView for Colyseus interface rendering
        try {
            webComponent = std::make_unique<FLStreamWebView>();
            addAndMakeVisible(webComponent.get());
            std::cout << "FL Stream: Voice Chat WebView initialized" << std::endl;
        } catch (...) {
            std::cout << "FL Stream: WebView initialization failed" << std::endl;
            // Don't fall back to native - WebView is required for reliable connection
        }
    }
    
    std::cout << "FL Stream: DEBUG - currentUIMode = " << (int)currentUIMode << std::endl;
    if (currentUIMode == UIMode::Native && !webComponent) {
        std::cout << "FL Stream: DEBUG - Entering native UI block" << std::endl;
        // Create JUCE components for push-to-talk and connection status display
        talkButton = std::make_unique<TextButton>("Push to Talk");
        talkButton->setButtonText("[MIC] Push to Talk");
        
        // Set TextButton colors for push-to-talk state indication
        talkButton->setColour(TextButton::buttonColourId, Colour(0xff4a90e2));
        talkButton->setColour(TextButton::buttonOnColourId, Colour(0xff27ae60)); // Green when active
        talkButton->setColour(TextButton::textColourOffId, Colours::white);
        talkButton->setColour(TextButton::textColourOnId, Colours::white);
        talkButton->setSize(200, 50);
        
        // Configure TextButton onStateChange for push-to-talk parameter control
        talkButton->onStateChange = [this]() {
            if (talkButton->isDown()) {
                talkButton->setButtonText("[REC] Talking...");
                talkButton->setColour(TextButton::buttonColourId, Colour(0xffe74c3c)); // Red when pushed
                startTalking();
            } else {
                talkButton->setButtonText("[MIC] Push to Talk");
                talkButton->setColour(TextButton::buttonColourId, Colour(0xff4a90e2)); // Blue default
                stopTalking();
            }
        };
        
        addAndMakeVisible(talkButton.get());
        
        // Label component for Colyseus connection status and voice protocol information
        statusLabel = std::make_unique<Label>("Status", "FL Stream Voice Chat\nReal-time collaborative voice communication over network");
        statusLabel->setJustificationType(Justification::centred);
        statusLabel->setColour(Label::textColourId, Colours::white);
        statusLabel->setColour(Label::backgroundColourId, Colour(0xff2c2c2c));
        statusLabel->setFont(Font(FontOptions(14.0f))); // Slightly larger font
        addAndMakeVisible(statusLabel.get());
        
        // Label displaying "room.state.players:" to match web interface
        playersLabel = std::make_unique<Label>("PlayersHeader", "room.state.players:");
        playersLabel->setJustificationType(Justification::centredLeft);
        playersLabel->setColour(Label::textColourId, Colour(0xffff69b4)); // Color 0xFFFF69B4 for web interface consistency
        playersLabel->setFont(Font(FontOptions(13.0f)));
        addAndMakeVisible(playersLabel.get());
        
        // Component container for dynamic player Label instances
        playerListContainer = std::make_unique<Component>();
        addAndMakeVisible(playerListContainer.get());
        
        // Generate initial player Label components with sample data
        updatePlayerList();
        
        // Label for real-time Colyseus WebSocket connection state
        connectionStatusLabel = std::make_unique<Label>("ConnectionStatus", "[*] Connecting...");
        connectionStatusLabel->setJustificationType(Justification::centred);
        connectionStatusLabel->setColour(Label::textColourId, Colour(0xfff39c12)); // Orange for connecting
        connectionStatusLabel->setFont(Font(FontOptions(12.0f).withStyle("Bold")));
        addAndMakeVisible(connectionStatusLabel.get());
        
        // Label for current room display
        roomDisplayLabel = std::make_unique<Label>("RoomDisplay", "Room: my_room");
        roomDisplayLabel->setJustificationType(Justification::centred);
        roomDisplayLabel->setColour(Label::textColourId, Colour(0xff64b5f6)); // Light blue for room info
        roomDisplayLabel->setFont(Font(FontOptions(13.0f).withStyle("Bold")));
        addAndMakeVisible(roomDisplayLabel.get());
        
        std::cout << "FL Stream: Native UI initialized with push-to-talk controls" << std::endl;
        std::cout << "FL Stream: DEBUG - About to execute auto-join block" << std::endl;
        
        // Configure Timer for 1000ms Colyseus connection status polling
        startTimer(1000); // Update every second
        
        // WebView handles connection - no auto-join needed for native UI fallback
    }

    // Initialize flStreamHtmlContent with embedded Colyseus interface HTML
    flStreamHtmlContent = R"HTMLEND(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>FL Stream Voice Chat</title>
    <style>
        body {
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
            background: #2c2c2c;
            color: #fff;
            margin: 0;
            padding: 20px;
            min-height: 100vh;
            display: flex;
            flex-direction: column;
            align-items: center;
            justify-content: center;
        }
        .container {
            text-align: center;
            max-width: 400px;
        }
        h1 {
            font-size: 1.5em;
            margin-bottom: 10px;
            color: #fff;
        }
        .subtitle {
            font-size: 1em;
            color: #aaa;
            margin-bottom: 30px;
        }
        #btn-talk {
            background: #4a90e2;
            color: white;
            border: none;
            border-radius: 25px;
            padding: 15px 30px;
            font-size: 1.1em;
            font-weight: 500;
            cursor: pointer;
            transition: all 0.2s ease;
            user-select: none;
            outline: none;
            min-width: 200px;
        }
        #btn-talk:active, #btn-talk.talking {
            background: #27ae60;
            transform: scale(1.1);
        }
        #btn-talk.pushing {
            background: #e74c3c;
            transform: scale(1.1);
        }
        #btn-talk:disabled {
            background: #555;
            cursor: not-allowed;
            transform: none;
        }
        .status {
            margin-top: 30px;
            padding: 15px;
            background: rgba(255,255,255,0.1);
            border-radius: 10px;
        }
        .status-dot {
            display: inline-block;
            width: 10px;
            height: 10px;
            border-radius: 50%;
            background: #e74c3c;
            margin-right: 8px;
        }
        .status-dot.connected {
            background: #27ae60;
        }
        .players {
            margin-top: 20px;
            text-align: left;
        }
        .players h3 {
            color: #4a90e2;
            margin-bottom: 10px;
            font-size: 1em;
        }
        .player-list {
            list-style: none;
            padding: 0;
            margin: 0;
            background: rgba(255,255,255,0.05);
            border-radius: 8px;
            overflow: hidden;
        }
        .player-list li {
            padding: 8px 12px;
            border-bottom: 1px solid rgba(255,255,255,0.1);
            font-size: 0.9em;
        }
        .player-list li:last-child {
            border-bottom: none;
        }
        .player-list li.current-user {
            font-weight: bold;
            color: #4a90e2;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>FL Stream Voice Chat</h1>
        <p class="subtitle">Real-time voice collaboration for music production</p>
        
        <button id="btn-talk" 
                onmousedown="startTalking()" 
                onmouseup="stopTalking()"
                onmouseleave="stopTalking()">
            [MIC] Push to Talk
        </button>
        
        <div class="status">
            <span class="status-dot" id="statusDot"></span>
            <span id="statusText">Connecting...</span>
        </div>
        
        <div class="players">
            <h3>Connected Users:</h3>
            <ul class="player-list" id="playerList">
                <li>Connecting to room...</li>
            </ul>
        </div>
    </div>

    <script>
        let isConnected = false;
        let isTalking = false;
        
        function startTalking() {
            if (!isConnected) return;
            
            console.log('Started talking');
            isTalking = true;
            
            const talkBtn = document.getElementById('btn-talk');
            talkBtn.classList.add('pushing');
            talkBtn.textContent = '[REC] Talking...';
            
            // Set the talking parameter to true
            if (window.talkButton) {
                window.talkButton.setValue(1.0);
            }
        }
        
        function stopTalking() {
            if (!isTalking) return;
            
            console.log('Stopped talking');
            isTalking = false;
            
            const talkBtn = document.getElementById('btn-talk');
            talkBtn.classList.remove('pushing');
            talkBtn.textContent = '[MIC] Push to Talk';
            
            // Set the talking parameter to false
            if (window.talkButton) {
                window.talkButton.setValue(0.0);
            }
        }
        
        function updateStatus() {
            if (window.getStatus) {
                window.getStatus((status) => {
                    if (typeof status === 'object') {
                        isConnected = status.isConnected;
                        
                        // Update connection status
                        const statusDot = document.getElementById('statusDot');
                        const statusText = document.getElementById('statusText');
                        
                        if (isConnected) {
                            statusDot.className = 'status-dot connected';
                            statusText.textContent = 'Connected to ' + (status.roomName || 'room');
                        } else {
                            statusDot.className = 'status-dot';
                            statusText.textContent = status.connectionStatus || 'Disconnected';
                        }
                        
                        // Update talk button state
                        const talkBtn = document.getElementById('btn-talk');
                        talkBtn.disabled = !isConnected;
                        
                        // Update players list
                        updatePlayersList(status.connectedUsers || 0);
                    }
                });
            }
        }
        
        function updatePlayersList(userCount) {
            const playerList = document.getElementById('playerList');
            
            if (userCount === 0) {
                playerList.innerHTML = '<li>No other users connected</li>';
            } else {
                let html = '<li class="current-user">You</li>';
                for (let i = 1; i < userCount; i++) {
                    html += `<li>User ${i + 1}</li>`;
                }
                playerList.innerHTML = html;
            }
        }
        
        // Auto-join room on load
        setTimeout(() => {
            if (window.joinRoom) {
                window.joinRoom('my_room', 'https://voice.latticeworks-ai.com', (result) => {
                    console.log('Auto-join result:', result);
                });
            }
        }, 1000);
        
        // Prevent context menu and selection
        document.addEventListener('contextmenu', e => e.preventDefault());
        document.addEventListener('selectstart', e => e.preventDefault());
        
        // Update status regularly
        setInterval(updateStatus, 1000);
        updateStatus();
    </script>
</body>
</html>
    )HTMLEND";
    
    setSize(1000, 700);
    
    // Only initialize WebView components if in WebView mode
    if (currentUIMode == UIMode::WebView && webComponent) {
        // CRITICAL: Force layout before loading URL
        resized();
        
        std::cout << "FL Stream: Browser ready - " << webComponent->getBounds().toString() << std::endl;
        
        // Load FL Stream Voice Chat asynchronously to prevent hanging
        juce::Timer::callAfterDelay(2000, [this]() {
            if (webComponent) {
                std::cout << "FL Stream: Starting delayed page load..." << std::endl;
                try {
                    loadFLStreamHome();
                } catch (...) {
                    std::cout << "FL Stream: Page load failed, browser ready for manual navigation" << std::endl;
                }
            }
        });
    } else {
        // Native UI mode - just resize
        resized();
    }
}

//==============================================================================
void FLStreamEditor::paint(Graphics& g)
{
    g.fillAll(Colour(0xff1a1a1a)); // Background color 0xFF1A1A1A for plugin interface
}

void FLStreamEditor::resized()
{
    auto area = getLocalBounds();
    
    if (currentUIMode == UIMode::WebView && webComponent) {
        // WebView fills entire area
        webComponent->setBounds(area);
    }
    else if (currentUIMode == UIMode::Native) {
        // Arrange UI components: status at top, button center, player list below
        area.reduce(20, 20);
        
        // Connection status at top
        if (connectionStatusLabel) {
            connectionStatusLabel->setBounds(area.removeFromTop(30));
            area.removeFromTop(5);
        }
        
        // Room display below connection status
        if (roomDisplayLabel) {
            roomDisplayLabel->setBounds(area.removeFromTop(25));
            area.removeFromTop(10);
        }
        
        // Main status label
        if (statusLabel) {
            statusLabel->setBounds(area.removeFromTop(80));
            area.removeFromTop(15);
        }
        
        // Talk button centered
        if (talkButton) {
            auto buttonArea = area.removeFromTop(60);
            talkButton->setBounds(buttonArea.reduced(buttonArea.getWidth() / 4, 0));
            area.removeFromTop(15);
        }
        
        // Players section header
        if (playersLabel) {
            playersLabel->setBounds(area.removeFromTop(25));
            area.removeFromTop(5);
        }
        
        // Player list container
        if (playerListContainer) {
            playerListContainer->setBounds(area.removeFromTop(120));
            
            // Layout player labels within container
            auto playerArea = playerListContainer->getLocalBounds();
            for (auto& playerLabel : playerLabels) {
                if (playerLabel) {
                    playerLabel->setBounds(playerArea.removeFromTop(30));
                }
            }
        }
    }
}



//==============================================================================
void FLStreamEditor::loadFLStreamHome()
{
    if (webComponent)
    {
        // Load FL Stream Voice Chat interface directly
        webComponent->goToURL("https://voice.latticeworks-ai.com");
        std::cout << "FL Stream: Loading FL Stream Voice Chat interface" << std::endl;
    }
}

//==============================================================================
// Native UI Interactive Methods
void FLStreamEditor::startTalking()
{
    std::cout << "FL Stream: Started talking (push-to-talk)" << std::endl;
    
    isSelfTalking = true;
    updatePlayerList(); // Update visual state immediately
    
    // Update processor talking parameter
    auto* talkingParam = processorRef.parameters.getParameter("isTalking");
    if (talkingParam) {
        talkingParam->setValueNotifyingHost(1.0f);
    }
}

void FLStreamEditor::stopTalking()
{
    std::cout << "FL Stream: Stopped talking (push-to-talk released)" << std::endl;
    
    isSelfTalking = false;
    updatePlayerList(); // Update visual state immediately
    
    // Update processor talking parameter
    auto* talkingParam = processorRef.parameters.getParameter("isTalking");
    if (talkingParam) {
        talkingParam->setValueNotifyingHost(0.0f);
    }
}

void FLStreamEditor::updateConnectionStatus()
{
    if (!connectionStatusLabel) return;
    
    bool isConnected = processorRef.isRoomConnected();
    bool isConnecting = processorRef.isConnecting();
    
    if (isConnected) {
        connectionStatusLabel->setText("[OK] Connected", dontSendNotification);
        connectionStatusLabel->setColour(Label::textColourId, Colour(0xff27ae60)); // Green
    } else if (isConnecting) {
        connectionStatusLabel->setText("[*] Connecting...", dontSendNotification);
        connectionStatusLabel->setColour(Label::textColourId, Colour(0xfff39c12)); // Orange
    } else {
        connectionStatusLabel->setText("[X] Disconnected", dontSendNotification);
        connectionStatusLabel->setColour(Label::textColourId, Colour(0xffe74c3c)); // Red
    }
}

void FLStreamEditor::updateRoomDisplay()
{
    if (!roomDisplayLabel) return;
    
    String currentRoom = processorRef.getCurrentRoomName();
    if (currentRoom.isEmpty()) {
        roomDisplayLabel->setText("Room: Not connected", dontSendNotification);
        roomDisplayLabel->setColour(Label::textColourId, Colour(0xff999999)); // Gray when not connected
    } else {
        roomDisplayLabel->setText("Room: " + currentRoom, dontSendNotification);
        roomDisplayLabel->setColour(Label::textColourId, Colour(0xff64b5f6)); // Light blue when connected
    }
}

void FLStreamEditor::updatePlayerList()
{
    if (!playerListContainer) return;
    
    // Clear existing player labels
    for (auto& label : playerLabels) {
        if (label) {
            playerListContainer->removeChildComponent(label.get());
        }
    }
    playerLabels.clear();
    
    // Get real player data from Colyseus room state
    std::vector<std::pair<String, bool>> players;
    
    if (processorRef.isRoomConnected()) {
        // Add self as connected player
        String selfId = processorRef.roomClient->getCurrentPlayerId();
        if (selfId.isNotEmpty()) {
            players.push_back({selfId + " (You)", isSelfTalking});
        }
        
        // Add other connected players from room state
        StringArray connectedUsers = processorRef.roomClient->getConnectedUserList();
        for (const String& userId : connectedUsers) {
            if (userId != selfId) {
                players.push_back({userId, false}); // Other players not talking for now
            }
        }
    }
    
    // Show disconnected state when no connection
    if (players.empty()) {
        players.push_back({"Not connected to room", false});
    }
    
    for (const auto& player : players) {
        auto playerLabel = std::make_unique<Label>("Player", player.first);
        playerLabel->setJustificationType(Justification::centredLeft);
        playerLabel->setFont(Font(FontOptions(12.0f)));
        
        // Apply visual highlighting for active players
        if (player.second) {
            // Background color 0xFFFFD700 when player is talking
            playerLabel->setColour(Label::backgroundColourId, Colour(0xffffff00));
            playerLabel->setColour(Label::textColourId, Colour(0xff000000)); // Black text on yellow
        } else {
            // Default background
            playerLabel->setColour(Label::backgroundColourId, Colour(0xff3a3a3a));
            playerLabel->setColour(Label::textColourId, Colours::white);
        }
        
        playerListContainer->addAndMakeVisible(playerLabel.get());
        playerLabels.push_back(std::move(playerLabel));
    }
    
    // Trigger relayout
    resized();
}

void FLStreamEditor::timerCallback()
{
    // Update connection status periodically
    updateConnectionStatus();
    updateRoomDisplay();
}