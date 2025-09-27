#include "FLStreamEditor.h"

// Colyseus server address - matches your nginx setup
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

    // Address bar and navigation controls are hidden - users don't need to change the URL
    
    // Avoid unused warning 
    (void)processorRef;

    // Initialize UI based on mode
    if (currentUIMode == UIMode::WebView) {
        // Create the browser component for FL Stream Voice Chat
        try {
            webComponent = std::make_unique<FLStreamWebView>();
            addAndMakeVisible(webComponent.get());
            std::cout << "FL Stream: Voice Chat WebView initialized" << std::endl;
        } catch (...) {
            std::cout << "FL Stream: WebView initialization failed, falling back to native UI" << std::endl;
            currentUIMode = UIMode::Native;
        }
    }
    
    if (currentUIMode == UIMode::Native) {
        // Create enhanced FL Stream native UI with web-like styling
        talkButton = std::make_unique<TextButton>("Push to Talk");
        talkButton->setButtonText("🎤 Push to Talk");
        
        // Enhanced button styling to match web interface
        talkButton->setColour(TextButton::buttonColourId, Colour(0xff4a90e2));
        talkButton->setColour(TextButton::buttonOnColourId, Colour(0xff27ae60)); // Green when active
        talkButton->setColour(TextButton::textColourOffId, Colours::white);
        talkButton->setColour(TextButton::textColourOnId, Colours::white);
        talkButton->setSize(200, 50);
        
        // Add mouse interaction callbacks for visual feedback
        talkButton->onStateChange = [this]() {
            if (talkButton->isDown()) {
                talkButton->setButtonText("🔴 Talking...");
                talkButton->setColour(TextButton::buttonColourId, Colour(0xffe74c3c)); // Red when pushed
                startTalking();
            } else {
                talkButton->setButtonText("🎤 Push to Talk");
                talkButton->setColour(TextButton::buttonColourId, Colour(0xff4a90e2)); // Blue default
                stopTalking();
            }
        };
        
        addAndMakeVisible(talkButton.get());
        
        // Enhanced status label with better typography
        statusLabel = std::make_unique<Label>("Status", "🎵 FLStream Voice Chat\nThis example uses messages to exchange raw binary audio data.");
        statusLabel->setJustificationType(Justification::centred);
        statusLabel->setColour(Label::textColourId, Colours::white);
        statusLabel->setColour(Label::backgroundColourId, Colour(0xff2c2c2c));
        statusLabel->setFont(Font(FontOptions(14.0f))); // Slightly larger font
        addAndMakeVisible(statusLabel.get());
        
        // Players section header
        playersLabel = std::make_unique<Label>("PlayersHeader", "room.state.players:");
        playersLabel->setJustificationType(Justification::centredLeft);
        playersLabel->setColour(Label::textColourId, Colour(0xffff69b4)); // Pink like web interface
        playersLabel->setFont(Font(FontOptions(13.0f)));
        addAndMakeVisible(playersLabel.get());
        
        // Player list container
        playerListContainer = std::make_unique<Component>();
        addAndMakeVisible(playerListContainer.get());
        
        // Initialize player list
        updatePlayerList();
        
        // Connection status indicator
        connectionStatusLabel = std::make_unique<Label>("ConnectionStatus", "● Connecting...");
        connectionStatusLabel->setJustificationType(Justification::centred);
        connectionStatusLabel->setColour(Label::textColourId, Colour(0xfff39c12)); // Orange for connecting
        connectionStatusLabel->setFont(Font(FontOptions(12.0f).withStyle("Bold")));
        addAndMakeVisible(connectionStatusLabel.get());
        
        std::cout << "FL Stream: Enhanced native UI initialized with web-like styling" << std::endl;
        
        // Start periodic connection status updates
        startTimer(1000); // Update every second
    }

    // Store FL Stream Voice Chat HTML content
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
        <h1>🎵 FL Stream Voice Chat</h1>
        <p class="subtitle">Real-time voice collaboration for music production</p>
        
        <button id="btn-talk" 
                onmousedown="startTalking()" 
                onmouseup="stopTalking()"
                onmouseleave="stopTalking()">
            🎤 Push to Talk
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
            talkBtn.textContent = '🔴 Talking...';
            
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
            talkBtn.textContent = '🎤 Push to Talk';
            
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
    g.fillAll(Colour(0xff1a1a1a)); // Dark background for FL Stream
}

void FLStreamEditor::resized()
{
    auto area = getLocalBounds();
    
    if (currentUIMode == UIMode::WebView && webComponent) {
        // WebView fills entire area
        webComponent->setBounds(area);
    }
    else if (currentUIMode == UIMode::Native) {
        // Enhanced native UI layout with connection status and player list
        area.reduce(20, 20);
        
        // Connection status at top
        if (connectionStatusLabel) {
            connectionStatusLabel->setBounds(area.removeFromTop(30));
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

// Timer functionality removed for general browser

//==============================================================================
std::optional<WebBrowserComponent::Resource> FLStreamEditor::getResource(const String& url)
{
    const auto urlToRetrive = url == "/" ? String{"index.html"} 
                                         : url.fromFirstOccurrenceOf("/", false, false);

    // Try to load from embedded assets first
    if (auto* archive = getFLStreamWebAssets())
    {
        if (auto* entry = archive->getEntry(urlToRetrive))
        {
            auto stream = rawToUniquePtr(archive->createStreamForEntry(*entry));
            auto v = streamToVector(*stream);
            auto mime = getMimeForExtension(getExtension(entry->filename).toLowerCase());
            return WebBrowserComponent::Resource{std::move(v), std::move(mime)};
        }
    }

    // Return FL Stream Voice Chat interface for local URLs
    if (urlToRetrive == "index.html" || urlToRetrive == "" || url == "/" || url == "flstream://home")
    {
        std::cout << "FL Stream: Serving FL Stream Voice Chat interface" << std::endl;
        MemoryInputStream stream{flStreamHtmlContent.getCharPointer(), flStreamHtmlContent.getNumBytesAsUTF8(), false};
        return WebBrowserComponent::Resource{streamToVector(stream), String{"text/html"}};
    }
    
    // Fallback to simpler HTML interface if above doesn't work
    if (urlToRetrive == "fallback.html")
    {
        const String fallbackHtml = R"HTMLEND(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>FL Stream Voice Chat</title>
    <style>
        body {
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
            background: #f5f5f5;
            color: #333;
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
            color: #666;
        }
        .subtitle {
            font-size: 1em;
            color: #999;
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
        }
        #btn-talk:active, #btn-talk.talking {
            background: #27ae60;
            transform: scale(1.4);
        }
        #btn-talk.pushing {
            background: #f39c12;
        }
        #btn-talk:disabled {
            background: #ccc;
            cursor: not-allowed;
            transform: none;
        }
        .players {
            margin-top: 40px;
            text-align: left;
        }
        .players h3 {
            color: #c44569;
            margin-bottom: 15px;
            font-size: 1.2em;
        }
        .player-list {
            list-style: none;
            padding: 0;
            margin: 0;
            background: white;
            border-radius: 8px;
            overflow: hidden;
            box-shadow: 0 2px 10px rgba(0,0,0,0.1);
        }
        .player-list li {
            padding: 12px 15px;
            border-bottom: 1px solid #f0f0f0;
            transition: background 0.2s;
        }
        .player-list li:last-child {
            border-bottom: none;
        }
        .player-list li.current-user {
            font-weight: bold;
        }
        .player-list li.pushing {
            background: #fff3cd;
        }
        .player-list li.talking {
            background: #d4edda;
        }
        .connection-status {
            position: absolute;
            top: 20px;
            right: 20px;
            font-size: 0.9em;
            color: #999;
        }
        .status-dot {
            display: inline-block;
            width: 8px;
            height: 8px;
            border-radius: 50%;
            background: #e74c3c;
            margin-right: 5px;
        }
        .status-dot.connected {
            background: #27ae60;
        }
    </style>
</head>
<body>
    <div class="connection-status">
        <span class="status-dot" id="statusDot"></span>
        <span id="statusText">Disconnected</span>
    </div>

    <div class="container">
        <h1>Voice Chat</h1>
        <p class="subtitle">This example uses messages to exchange raw binary audio data.</p>
        
        <button id="btn-talk" 
                onpointerdown="startTalking()" 
                onpointerup="stopTalking()"
                onmousedown="startTalking()" 
                onmouseup="stopTalking()"
                ontouchstart="startTalking()" 
                ontouchend="stopTalking()"
                disabled>
            🎤 Push to Talk
        </button>
        
        <div class="players">
            <h3>room.state.players:</h3>
            <ul class="player-list" id="playerList">
                <li>No players connected</li>
            </ul>
        </div>
    </div>

    <script>
        let isConnected = false;
        let players = [];
        
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
                            statusText.textContent = 'Connected';
                        } else {
                            statusDot.className = 'status-dot';
                            statusText.textContent = 'Disconnected';
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
                playerList.innerHTML = '<li>No players connected</li>';
            } else {
                let html = '';
                for (let i = 0; i < userCount; i++) {
                    const isCurrentUser = i === 0; // Assume first player is current user
                    html += `<li class="${isCurrentUser ? 'current-user' : ''}">Player ${String.fromCharCode(65 + i)}${isCurrentUser ? ' (You)' : ''}</li>`;
                }
                playerList.innerHTML = html;
            }
        }
        
        // Push-to-talk functionality
        function startTalking() {
            if (!isConnected) {
                console.log('Cannot talk - not connected to room');
                return;
            }
            
            console.log('Started talking (push-to-talk)');
            
            // Update button appearance
            const talkBtn = document.getElementById('btn-talk');
            talkBtn.classList.add('pushing');
            talkBtn.textContent = '🔴 Talking...';
            
            // Set the talking parameter to true
            if (window.talkButton) {
                window.talkButton.setValue(1.0);
            }
        }
        
        function stopTalking() {
            console.log('Stopped talking (push-to-talk released)');
            
            // Update button appearance
            const talkBtn = document.getElementById('btn-talk');
            talkBtn.classList.remove('pushing', 'talking');
            talkBtn.textContent = '🎤 Push to Talk';
            
            // Set the talking parameter to false
            if (window.talkButton) {
                window.talkButton.setValue(0.0);
            }
        }
        
        // Auto-join room on load (simplifies UI)
        function autoJoinRoom() {
            if (window.joinRoom) {
                window.joinRoom('my_room', 'https://voice.latticeworks-ai.com', (result) => {
                    console.log('Auto-join result:', result);
                    updateStatus();
                });
            }
        }
        
        // Prevent context menu on talk button
        document.addEventListener('DOMContentLoaded', function() {
            const talkBtn = document.getElementById('btn-talk');
            if (talkBtn) {
                talkBtn.addEventListener('contextmenu', function(e) {
                    e.preventDefault();
                });
            }
            
            // Auto-join after DOM loads
            setTimeout(autoJoinRoom, 500);
        });
        
        // Listen for status updates from plugin
        if (window.addEventListener) {
            window.addEventListener('statusUpdate', updateStatus);
        }
        
        // Initial status check and periodic updates
        updateStatus();
        setInterval(updateStatus, 1000);
    </script>
</body>
</html>
        )HTMLEND";
        
        MemoryInputStream stream{fallbackHtml.getCharPointer(), fallbackHtml.getNumBytesAsUTF8(), false};
        return WebBrowserComponent::Resource{streamToVector(stream), String{"text/html"}};
    }


    return std::nullopt;
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
        connectionStatusLabel->setText("● Connected", dontSendNotification);
        connectionStatusLabel->setColour(Label::textColourId, Colour(0xff27ae60)); // Green
    } else if (isConnecting) {
        connectionStatusLabel->setText("● Connecting...", dontSendNotification);
        connectionStatusLabel->setColour(Label::textColourId, Colour(0xfff39c12)); // Orange
    } else {
        connectionStatusLabel->setText("● Disconnected", dontSendNotification);
        connectionStatusLabel->setColour(Label::textColourId, Colour(0xffe74c3c)); // Red
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
    
    // Create player labels (simulating the data from the image)
    std::vector<std::pair<String, bool>> players = {
        {"Player AxIzvU3u5", false},  // Other player (not talking)
        {"Player o7AfStBze (You)", isSelfTalking}  // Self with talking state
    };
    
    for (const auto& player : players) {
        auto playerLabel = std::make_unique<Label>("Player", player.first);
        playerLabel->setJustificationType(Justification::centredLeft);
        playerLabel->setFont(Font(FontOptions(12.0f)));
        
        // Apply highlighting like web interface
        if (player.second) {
            // Yellow background when talking (like web interface)
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
}