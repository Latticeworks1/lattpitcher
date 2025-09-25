#include "FLStreamEditor.h"

// Colyseus server address - matches your nginx setup
#if JUCE_ANDROID
const String localColouseusServerAddress = "https://voice.latticeworks-ai.com/";
#else
const String localColouseusServerAddress = "https://voice.latticeworks-ai.com/";
#endif

//==============================================================================
bool FLStreamWebView::pageAboutToLoad(const String& newURL)
{
    // Only allow our Colyseus server and local resource URLs
    return newURL.startsWith(localColouseusServerAddress) || 
           newURL == getResourceProviderRoot();
}

//==============================================================================
FLStreamEditor::FLStreamEditor(FLStreamProcessor& p)
    : AudioProcessorEditor(p), 
      processorRef(p),
      roomVolumeAttachment(*processorRef.parameters.getParameter(PARAM_IS_CONNECTED),
                          roomVolumeRelay,
                          processorRef.parameters.undoManager),
      muteAttachment(*processorRef.parameters.getParameter(PARAM_IS_CONNECTED),
                    muteToggleRelay,
                    processorRef.parameters.undoManager),
      talkButtonAttachment(*processorRef.parameters.getParameter(PARAM_IS_TALKING),
                          talkButtonRelay,
                          processorRef.parameters.undoManager)
{
    addAndMakeVisible(webComponent);

    // Load the room management interface
    webComponent.goToURL(WebBrowserComponent::getResourceProviderRoot());

    setSize(800, 600);
    startTimerHz(30); // 30fps updates for real-time room status
}

//==============================================================================
void FLStreamEditor::paint(Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(ResizableWindow::backgroundColourId));
}

void FLStreamEditor::resized()
{
    webComponent.setBounds(getLocalBounds());
}

//==============================================================================
void FLStreamEditor::timerCallback()
{
    static constexpr size_t numFramesBuffered = 10;

    SpinLock::ScopedLockType lock{processorRef.statusLock};

    Array<var> frame;
    
    // Gather real-time status data
    frame.add(processorRef.getCurrentRoomName());
    frame.add(processorRef.getServerAddress());
    frame.add(processorRef.isRoomConnected());
    frame.add(processorRef.isRoomConnected()); // Connection status
    frame.add(processorRef.connectedUsers.load());
    frame.add(processorRef.audioLevel.load());

    statusFrames.push_back(std::move(frame));

    while (statusFrames.size() > numFramesBuffered)
        statusFrames.pop_front();

    static int64 callbackCounter = 0;

    // Send status updates to web interface every few frames
    if (statusFrames.size() == numFramesBuffered && callbackCounter++ % 5 == 0)
    {
        webComponent.emitEventIfBrowserIsVisible("statusUpdate", var{});
    }
}

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

    // Fallback to embedded HTML interface
    if (urlToRetrive == "index.html")
    {
        const String fallbackHtml = R"HTMLEND(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>FL Stream - Room Management</title>
    <style>
        body {
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
            background: linear-gradient(135deg, #393f47 0%, #4a5058 100%);
            color: #ffffff;
            margin: 0;
            padding: 20px;
            min-height: 100vh;
        }
        .container {
            max-width: 600px;
            margin: 0 auto;
            background: rgba(74, 80, 88, 0.8);
            border-radius: 12px;
            padding: 30px;
            box-shadow: 0 8px 32px rgba(0, 0, 0, 0.3);
        }
        h1 {
            color: #5fb3d4;
            text-align: center;
            margin-bottom: 30px;
            font-size: 2.2em;
        }
        .status-section {
            background: rgba(95, 179, 212, 0.1);
            border: 1px solid #5fb3d4;
            border-radius: 8px;
            padding: 20px;
            margin-bottom: 20px;
        }
        .room-section {
            background: rgba(124, 181, 24, 0.1);
            border: 1px solid #7cb518;
            border-radius: 8px;
            padding: 20px;
            margin-bottom: 20px;
        }
        .input-group {
            margin-bottom: 15px;
        }
        label {
            display: block;
            margin-bottom: 5px;
            color: #b8bcc2;
            font-weight: 500;
        }
        input, select, button {
            width: 100%;
            padding: 12px;
            border: 1px solid #5a5a5a;
            border-radius: 6px;
            background: #2c2c2c;
            color: #ffffff;
            font-size: 14px;
            box-sizing: border-box;
        }
        button {
            background: linear-gradient(135deg, #5fb3d4 0%, #4a9bc4 100%);
            border: none;
            cursor: pointer;
            font-weight: bold;
            text-transform: uppercase;
            letter-spacing: 0.5px;
            transition: all 0.3s ease;
        }
        button:hover {
            background: linear-gradient(135deg, #4a9bc4 0%, #3d8ab4 100%);
            transform: translateY(-1px);
        }
        button:disabled {
            background: #666;
            cursor: not-allowed;
            transform: none;
        }
        .status-indicator {
            display: inline-block;
            width: 12px;
            height: 12px;
            border-radius: 50%;
            margin-right: 8px;
        }
        .connected { background: #7cb518; }
        .disconnected { background: #e74c3c; }
        .stats {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
            gap: 15px;
            margin-top: 20px;
        }
        .stat-item {
            text-align: center;
            padding: 15px;
            background: rgba(0, 0, 0, 0.2);
            border-radius: 6px;
        }
        .stat-value {
            font-size: 1.8em;
            font-weight: bold;
            color: #5fb3d4;
        }
        .stat-label {
            font-size: 0.9em;
            color: #b8bcc2;
            margin-top: 5px;
        }
        .status-detail {
            font-size: 0.9em;
            margin: 8px 0;
            padding: 5px;
            background: rgba(0, 0, 0, 0.2);
            border-radius: 4px;
        }
        .connecting {
            background: #f39c12;
            animation: pulse 1.5s infinite;
        }
        @keyframes pulse {
            0% { opacity: 0.6; }
            50% { opacity: 1; }
            100% { opacity: 0.6; }
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>FL Stream Plugin</h1>
        
        <div class="status-section">
            <h3>Connection Status</h3>
            <div id="connectionStatus">
                <span class="status-indicator disconnected"></span>
                <span id="statusText">Disconnected</span>
            </div>
            
            <!-- Detailed status information -->
            <div id="detailedStatus" style="margin-top: 15px;">
                <div class="status-detail">
                    <strong>Status:</strong> <span id="connectionStatusDetail">Waiting...</span>
                </div>
                <div class="status-detail" id="lastLogContainer" style="display: none;">
                    <strong>Last Activity:</strong> <span id="lastLogMessage">-</span>
                </div>
                <div class="status-detail" id="errorContainer" style="display: none; color: #e74c3c;">
                    <strong>Error:</strong> <span id="errorMessage">-</span>
                </div>
            </div>
            
            <div class="stats">
                <div class="stat-item">
                    <div class="stat-value" id="connectedUsers">0</div>
                    <div class="stat-label">Users</div>
                </div>
                <div class="stat-item">
                    <div class="stat-value" id="audioLevel">0%</div>
                    <div class="stat-label">Audio Level</div>
                </div>
            </div>
        </div>

        <div class="room-section">
            <h3>Colyseus Room</h3>
            
            <div class="input-group">
                <label for="serverAddress">Server URL</label>
                <input type="text" id="serverAddress" value="https://voice.latticeworks-ai.com" placeholder="https://your-colyseus-server.com">
            </div>
            
            <div class="input-group">
                <label for="roomName">Room Name</label>
                <input type="text" id="roomName" placeholder="my-room" maxlength="50">
            </div>
            
            <button id="joinBtn" onclick="joinRoom()">Join Room</button>
            <button id="leaveBtn" onclick="leaveRoom()" style="margin-top: 10px; background: #e74c3c;" disabled>Leave Room</button>
            
            <!-- Push-to-Talk Button -->
            <div style="margin-top: 15px; padding: 10px; background: rgba(0,0,0,0.3); border-radius: 8px;">
                <h4 style="margin: 0 0 10px 0; color: #5fb3d4;">Push to Talk</h4>
                <button id="talkButton" 
                        onpointerdown="startTalking()" 
                        onpointerup="stopTalking()" 
                        style="padding: 15px 30px; font-size: 1.1em; background: #27ae60; border: none; border-radius: 6px; color: white; cursor: pointer; width: 100%; user-select: none;"
                        disabled>
                    Hold to Talk
                </button>
                <p style="font-size: 0.9em; color: #b8bcc2; margin: 8px 0 0 0; text-align: center;">Hold down the button while speaking</p>
            </div>
        </div>
    </div>

    <script>
        let isConnected = false;
        
        function updateStatus() {
            if (window.getStatus) {
                window.getStatus((status) => {
                    if (typeof status === 'object') {
                        document.getElementById('roomName').value = status.roomName || '';
                        document.getElementById('serverAddress').value = status.serverAddress || 'https://voice.latticeworks-ai.com';
                        
                        isConnected = status.isConnected;
                        const isConnecting = status.isConnecting;
                        
                        updateConnectionUI(isConnecting);
                        updateDetailedStatus(status);
                        
                        document.getElementById('connectedUsers').textContent = status.connectedUsers || 0;
                        document.getElementById('audioLevel').textContent = Math.round((status.audioLevel || 0) * 100) + '%';
                    }
                });
            }
        }
        
        function updateDetailedStatus(status) {
            // Update detailed status text
            document.getElementById('connectionStatusDetail').textContent = status.connectionStatus || 'Unknown';
            
            // Handle log messages
            const lastLog = status.lastLog;
            const lastLogContainer = document.getElementById('lastLogContainer');
            const lastLogElement = document.getElementById('lastLogMessage');
            
            if (lastLog && lastLog.trim() !== '') {
                lastLogContainer.style.display = 'block';
                lastLogElement.textContent = lastLog;
            } else {
                lastLogContainer.style.display = 'none';
            }
            
            // Handle error messages
            const lastError = status.lastError;
            const errorContainer = document.getElementById('errorContainer');
            const errorElement = document.getElementById('errorMessage');
            
            if (lastError && lastError.trim() !== '') {
                errorContainer.style.display = 'block';
                errorElement.textContent = lastError;
            } else {
                errorContainer.style.display = 'none';
            }
        }
        
        function updateConnectionUI(isConnecting) {
            const statusEl = document.getElementById('connectionStatus');
            const statusTextEl = document.getElementById('statusText');
            const statusIndicator = statusEl.querySelector('.status-indicator');
            const joinBtn = document.getElementById('joinBtn');
            const leaveBtn = document.getElementById('leaveBtn');
            const talkBtn = document.getElementById('talkButton');
            
            // Remove all status classes
            statusIndicator.className = 'status-indicator';
            
            if (isConnected) {
                statusIndicator.classList.add('connected');
                statusTextEl.textContent = 'Connected to ' + (document.getElementById('roomName').value || 'room');
                joinBtn.disabled = true;
                leaveBtn.disabled = false;
                talkBtn.disabled = false;  // Enable talk button when connected
            } else if (isConnecting) {
                statusIndicator.classList.add('connecting');
                statusTextEl.textContent = 'Connecting...';
                joinBtn.disabled = true;
                leaveBtn.disabled = true;
                talkBtn.disabled = true;  // Disable talk button while connecting
            } else {
                statusIndicator.classList.add('disconnected');
                statusTextEl.textContent = 'Disconnected';
                joinBtn.disabled = false;
                leaveBtn.disabled = true;
                talkBtn.disabled = true;  // Disable talk button when disconnected
            }
        }
        
        function joinRoom() {
            const roomName = document.getElementById('roomName').value.trim();
            const serverAddress = document.getElementById('serverAddress').value.trim();
            
            if (!roomName) {
                // Show error in detailed status instead of alert
                document.getElementById('connectionStatusDetail').textContent = 'Please enter a room name';
                document.getElementById('errorContainer').style.display = 'block';
                document.getElementById('errorMessage').textContent = 'Room name is required';
                return;
            }
            
            if (!serverAddress) {
                document.getElementById('connectionStatusDetail').textContent = 'Please enter a server address';
                document.getElementById('errorContainer').style.display = 'block';
                document.getElementById('errorMessage').textContent = 'Server address is required';
                return;
            }
            
            // Clear previous errors
            document.getElementById('errorContainer').style.display = 'none';
            
            // Show connecting state immediately
            updateConnectionUI(true);
            document.getElementById('connectionStatusDetail').textContent = 'Initiating connection...';
            
            if (window.joinRoom) {
                window.joinRoom(roomName, serverAddress, (result) => {
                    console.log('Join result:', result);
                    // Update status immediately and then again after delay
                    updateStatus();
                    setTimeout(updateStatus, 500);
                });
            } else {
                document.getElementById('errorContainer').style.display = 'block';
                document.getElementById('errorMessage').textContent = 'Native join function not available';
                updateConnectionUI(false);
            }
        }
        
        function leaveRoom() {
            if (window.leaveRoom) {
                window.leaveRoom((result) => {
                    console.log('Leave result:', result);
                    setTimeout(updateStatus, 500);
                });
            }
        }
        
        
        // Listen for status updates from plugin
        if (window.addEventListener) {
            window.addEventListener('statusUpdate', updateStatus);
        }
        
        // Push-to-talk functionality
        function startTalking() {
            if (!isConnected) {
                console.log('Cannot talk - not connected to room');
                return;
            }
            
            console.log('Started talking (push-to-talk)');
            
            // Update button appearance
            const talkBtn = document.getElementById('talkButton');
            talkBtn.textContent = 'Talking...';
            talkBtn.style.background = '#e74c3c';  // Red when talking
            
            // Set the talking parameter to true
            if (window.talkButton) {
                window.talkButton.setValue(1.0);  // Enable talking
            }
        }
        
        function stopTalking() {
            console.log('Stopped talking (push-to-talk released)');
            
            // Update button appearance
            const talkBtn = document.getElementById('talkButton');
            talkBtn.textContent = 'Hold to Talk';
            talkBtn.style.background = '#27ae60';  // Green when not talking
            
            // Set the talking parameter to false
            if (window.talkButton) {
                window.talkButton.setValue(0.0);  // Disable talking
            }
        }
        
        // Prevent context menu on talk button to avoid interference with push-to-talk
        document.addEventListener('DOMContentLoaded', function() {
            const talkBtn = document.getElementById('talkButton');
            if (talkBtn) {
                talkBtn.addEventListener('contextmenu', function(e) {
                    e.preventDefault();
                });
            }
        });
        
        // Initial status check
        updateStatus();
        setInterval(updateStatus, 1000);
        
        // Parameter sync - room enabled when connected
        // No UI controls needed for this
    </script>
</body>
</html>
        )HTMLEND";
        
        MemoryInputStream stream{fallbackHtml.getCharPointer(), fallbackHtml.getNumBytesAsUTF8(), false};
        return WebBrowserComponent::Resource{streamToVector(stream), String{"text/html"}};
    }

    // Status data API endpoint
    if (urlToRetrive == "status.json")
    {
        Array<var> frames;
        for (const auto& frame : statusFrames)
            frames.add(frame);

        DynamicObject::Ptr d(new DynamicObject());
        d->setProperty("timeResolutionMs", getTimerInterval());
        d->setProperty("frames", std::move(frames));

        const auto s = JSON::toString(d.get());
        MemoryInputStream stream{s.getCharPointer(), s.getNumBytesAsUTF8(), false};
        return WebBrowserComponent::Resource{streamToVector(stream), String{"application/json"}};
    }

    return std::nullopt;
}