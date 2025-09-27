#include "FLStreamProcessor.h"
#include "FLStreamEditor.h"

//==============================================================================
FLStreamProcessor::FLStreamProcessor()
    : AudioProcessor(BusesProperties()
                    #if ! JucePlugin_IsMidiEffect
                     #if ! JucePlugin_IsSynth
                      .withInput("Input", AudioChannelSet::stereo(), true)
                     #endif
                      .withOutput("Output", AudioChannelSet::stereo(), true)
                    #endif
                      ),
      parameters(*this, nullptr, "STATE", createParameterLayout()),
      roomClient(std::make_unique<ColyseusRoomClient>())
{
    // Set up Colyseus room client callbacks with enhanced status tracking
    roomClient->onRoomJoined = [this](const String& roomName) {
        const ScopedLock lock(roomStateMutex);
        currentRoomName = roomName;
        connectedUsers.store(1);
        connectionState.store(ConnectionState::Connected);
        
        const ScopedLock statusLock(statusMutex);
        connectionStatusText = "Connected to room: " + roomName;
        lastLogMessage = "Successfully joined room " + roomName;
    };
    
    roomClient->onRoomLeft = [this](const String& roomName) {
        const ScopedLock lock(roomStateMutex);
        currentRoomName = "";
        connectedUsers.store(0);
        connectionState.store(ConnectionState::Disconnected);
        
        const ScopedLock statusLock(statusMutex);
        connectionStatusText = "Disconnected from room";
        lastLogMessage = "Left room " + roomName;
    };
    
    roomClient->onUserJoined = [this](const String& userId, const String& userName) {
        connectedUsers.store(roomClient->getConnectedUserCount());
        const ScopedLock statusLock(statusMutex);
        lastLogMessage = "User joined: " + userName + " (" + userId + ")";
    };
    
    roomClient->onUserLeft = [this](const String& userId) {
        connectedUsers.store(roomClient->getConnectedUserCount());
        const ScopedLock statusLock(statusMutex);
        lastLogMessage = "User left: " + userId;
    };
    
    // Add error and log message callbacks
    roomClient->onError = [this](const String& errorMessage) {
        connectionState.store(ConnectionState::Error);
        const ScopedLock statusLock(statusMutex);
        lastErrorMessage = errorMessage;
        connectionStatusText = "Error: " + errorMessage;
        lastLogMessage = "ERROR: " + errorMessage;
    };
    
    roomClient->onLogMessage = [this](const String& logMessage) {
        const ScopedLock statusLock(statusMutex);
        lastLogMessage = logMessage;
        
        // Update connection status based on log messages
        if (logMessage.contains("Matchmaking"))
        {
            connectionState.store(ConnectionState::Matchmaking);
            connectionStatusText = "Matchmaking...";
        }
        else if (logMessage.contains("Connecting to WebSocket"))
        {
            connectionState.store(ConnectionState::EstablishingWebSocket);
            connectionStatusText = "Establishing WebSocket connection...";
        }
        else if (logMessage.contains("Attempting reconnection"))
        {
            connectionState.store(ConnectionState::Connecting);
            connectionStatusText = "Reconnecting...";
        }
    };
    
    // Audio receive is now handled via getNextAudioMessage() in processRoomCollaboration
    // No callback needed with the proven implementation
    
    // Auto-join will be triggered by WebView interface to avoid GUI conflicts
}

//==============================================================================
void FLStreamProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    const auto channels = std::max(getTotalNumInputChannels(), getTotalNumOutputChannels());
    
    if (channels == 0)
        return;
    
    // Audio processing initialization complete
}

bool FLStreamProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != AudioChannelSet::stereo())
        return false;

    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;

    return true;
}

void FLStreamProcessor::processBlock(AudioBuffer<float>& buffer, MidiBuffer& midiMessages)
{
    ScopedNoDenormals noDenormals;
    ignoreUnused(midiMessages);

    const auto totalNumInputChannels = getTotalNumInputChannels();
    const auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    // Process room collaboration if connected
    if (roomClient->isConnected())
    {
        processRoomCollaboration(buffer);
    }
    // If not connected, audio passes through unchanged
    
    // Update audio level for web interface
    updateAudioLevel(buffer);
}

//==============================================================================
void FLStreamProcessor::processRoomCollaboration(AudioBuffer<float>& buffer)
{
    // Check if user is currently talking (push-to-talk)
    auto* talkingParam = parameters.getParameter(PARAM_IS_TALKING);
    bool currentlyTalking = talkingParam->getValue() > 0.5f;
    
    // Handle push-to-talk state changes
    if (currentlyTalking && !isTalking.load()) {
        // Just started talking - send "push" message
        isTalking = true;
        isPushing = true;
        
        // Send Colyseus "push" message using proven implementation
        roomClient->sendPushToTalk(true);
        
        std::cout << "FL Stream: Started talking - sent push message" << std::endl;
    }
    else if (!currentlyTalking && isTalking.load()) {
        // Just stopped talking
        isTalking = false;
        isPushing = false;
        
        // Send push-to-talk OFF message
        roomClient->sendPushToTalk(false);
        
        std::cout << "FL Stream: Stopped talking" << std::endl;
    }
    
    // Only send audio when actively talking
    if (isTalking.load()) {
        // Convert JUCE AudioBuffer to std::vector<float> for proven implementation
        std::vector<float> audioData;
        int numSamples = buffer.getNumSamples();
        int numChannels = buffer.getNumChannels();
        
        audioData.resize(numSamples * numChannels);
        
        // Interleave channels
        for (int sample = 0; sample < numSamples; ++sample) {
            for (int channel = 0; channel < numChannels; ++channel) {
                audioData[sample * numChannels + channel] = buffer.getSample(channel, sample);
            }
        }
        
        // Send audio using proven implementation
        roomClient->sendAudioData(audioData);
    }
    
    // Mix incoming audio from other clients
    AudioMessage incomingMessage;
    while (roomClient->getNextAudioMessage(incomingMessage)) {
        int samplesToMix = jmin(static_cast<int>(incomingMessage.audioData.size()), buffer.getNumSamples());
        
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel) {
            auto* channelData = buffer.getWritePointer(channel);
            
            for (int sample = 0; sample < samplesToMix; ++sample) {
                channelData[sample] += incomingMessage.audioData[sample] * 0.5f; // Mix at 50% volume
            }
        }
    }
    
    // Always monitor audio level for UI feedback
    updateAudioLevel(buffer);
}

void FLStreamProcessor::updateAudioLevel(const AudioBuffer<float>& buffer)
{
    float rms = 0.0f;
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        rms += buffer.getRMSLevel(channel, 0, buffer.getNumSamples());
    }
    rms /= jmax(1, buffer.getNumChannels());
    
    audioLevel.store(rms);
}

//==============================================================================
void FLStreamProcessor::getStateInformation(MemoryBlock& destData)
{
    auto xml = parameters.copyState().createXml();
    copyXmlToBinary(*xml, destData);
}

void FLStreamProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto xmlState = getXmlFromBinary(data, sizeInBytes);
    if (xmlState.get() != nullptr)
        if (xmlState->hasTagName(parameters.state.getType()))
            parameters.replaceState(ValueTree::fromXml(*xmlState));
}

AudioProcessorEditor* FLStreamProcessor::createEditor()
{
    return new FLStreamEditor(*this);
}

//==============================================================================
// Room management implementation
bool FLStreamProcessor::joinRoom(const String& roomName, const String& serverUrl)
{
    if (roomClient)
    {
        const ScopedLock lock(roomStateMutex);
        this->serverAddress = serverUrl;
        
        // Set initial connecting state
        connectionState.store(ConnectionState::Connecting);
        {
            const ScopedLock statusLock(statusMutex);
            connectionStatusText = "Connecting to " + roomName + "...";
            lastLogMessage = "Initiating connection to room: " + roomName;
            lastErrorMessage = ""; // Clear previous errors
        }
        
        return roomClient->joinRoom(roomName, serverUrl);
    }
    else
    {
        connectionState.store(ConnectionState::Error);
        const ScopedLock statusLock(statusMutex);
        lastErrorMessage = "Room client not initialized";
        connectionStatusText = "Error: Room client not available";
        return false;
    }
}

void FLStreamProcessor::leaveRoom()
{
    if (roomClient)
    {
        roomClient->leaveRoom();
    }
}

bool FLStreamProcessor::isRoomConnected() const
{
    return roomClient ? roomClient->isConnected() : false;
}

//==============================================================================
AudioProcessorValueTreeState::ParameterLayout FLStreamProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<RangedAudioParameter>> params;
    
    // Connection and talk state parameters
    params.push_back(std::make_unique<AudioParameterBool>(
        PARAM_IS_CONNECTED, "Connected", false));
    
    params.push_back(std::make_unique<AudioParameterBool>(
        PARAM_IS_TALKING, "Talking", false));
    
    // Room audio controls
    params.push_back(std::make_unique<AudioParameterFloat>(
        PARAM_ROOM_VOLUME, "Room Volume", 0.0f, 1.0f, 0.8f));
    
    params.push_back(std::make_unique<AudioParameterBool>(
        PARAM_MUTE, "Mute", false));
    
    return { params.begin(), params.end() };
}

//==============================================================================
String FLStreamProcessor::getConnectionStatusText() const
{
    const ScopedLock statusLock(statusMutex);
    return connectionStatusText;
}

String FLStreamProcessor::getLastErrorMessage() const
{
    const ScopedLock statusLock(statusMutex);
    return lastErrorMessage;
}

String FLStreamProcessor::getLastLogMessage() const
{
    const ScopedLock statusLock(statusMutex);
    return lastLogMessage;
}

bool FLStreamProcessor::isConnecting() const
{
    auto state = connectionState.load();
    return state == ConnectionState::Connecting || 
           state == ConnectionState::Matchmaking || 
           state == ConnectionState::EstablishingWebSocket;
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FLStreamProcessor();
}