#include "FLStreamProcessor.h"
#include "FLStreamEditor.h"

//==============================================================================
FLStreamProcessor::FLStreamProcessor()
    : AudioProcessor(BusesProperties()
                    #if ! JucePlugin_IsMidiEffect
                     #if ! JucePlugin_IsSynth
                      .withInput("Input", AudioChannelSet::stereo(), true)
                     #else
                      // Synthesizer plugin accepts external audio bus for push-to-talk voice processing
                      .withInput("External Input", AudioChannelSet::stereo(), false)
                     #endif
                      .withOutput("Output", AudioChannelSet::stereo(), true)
                    #endif
                      ),
      parameters(*this, nullptr, "STATE", createParameterLayout()),
      roomClient(std::make_unique<ColyseusRoomClient>())
{
    // Configure Colyseus WebSocket event handlers for room state synchronization
    roomClient->onRoomJoined = [this](const String& roomName) {
        const ScopedLock lock(roomStateMutex);
        currentRoomName = roomName;
        connectedUsers.store(1);
        connectionState.store(ConnectionState::Connected);
        
        const ScopedLock statusGuard(statusMutex);
        connectionStatusText = "Connected to room: " + roomName;
        lastLogMessage = "Successfully joined room " + roomName;
    };
    
    roomClient->onRoomLeft = [this](const String& roomName) {
        const ScopedLock lock(roomStateMutex);
        currentRoomName = "";
        connectedUsers.store(0);
        connectionState.store(ConnectionState::Disconnected);
        
        const ScopedLock statusGuard(statusMutex);
        connectionStatusText = "Disconnected from room";
        lastLogMessage = "Left room " + roomName;
    };
    
    roomClient->onUserJoined = [this](const String& userId, const String& userName) {
        connectedUsers.store(roomClient->getConnectedUserCount());
        const ScopedLock statusGuard(statusMutex);
        lastLogMessage = "User joined: " + userName + " (" + userId + ")";
    };
    
    roomClient->onUserLeft = [this](const String& userId) {
        connectedUsers.store(roomClient->getConnectedUserCount());
        const ScopedLock statusGuard(statusMutex);
        lastLogMessage = "User left: " + userId;
    };
    
    // Add error and log message callbacks
    roomClient->onError = [this](const String& errorMessage) {
        connectionState.store(ConnectionState::Error);
        const ScopedLock statusGuard(statusMutex);
        lastErrorMessage = errorMessage;
        connectionStatusText = "Error: " + errorMessage;
        lastLogMessage = "ERROR: " + errorMessage;
    };
    
    roomClient->onLogMessage = [this](const String& logMessage) {
        const ScopedLock statusGuard(statusMutex);
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
    
    // Audio receive handled via getNextAudioMessage() polling
    // No callback needed for audio data retrieval
    
    // Network operations execute on background thread to maintain real-time audio performance
}

//==============================================================================
void FLStreamProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(sampleRate, samplesPerBlock);
    
    const auto channels = std::max(getTotalNumInputChannels(), getTotalNumOutputChannels());
    
    if (channels == 0)
        return;
    
    // Sample rate and channel configuration applied for real-time processing
}

bool FLStreamProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // Main output must be mono or stereo
    if (layouts.getMainOutputChannelSet() != AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != AudioChannelSet::stereo())
        return false;

#if JucePlugin_IsSynth
    // JucePlugin_IsSynth: supports arbitrary input channel counts for voice transmission
    // Generator plugin output channels determined by plugin configuration, not input bus
    return true;
#else
    // Effect plugin enforces identical input/output channel count per JUCE specification
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;

    return true;
#endif
}

void FLStreamProcessor::processBlock(AudioBuffer<float>& buffer, MidiBuffer& midiMessages)
{
    ScopedNoDenormals noDenormals;
    ignoreUnused(midiMessages);

    const auto totalNumInputChannels = getTotalNumInputChannels();
    const auto totalNumOutputChannels = getTotalNumOutputChannels();
    juce::ignoreUnused(totalNumInputChannels, totalNumOutputChannels);

#if JucePlugin_IsSynth
    // JucePlugin_IsSynth: process main output bus and external input bus if present
    auto mainOutput = getBusBuffer(buffer, false, 0);  // Main output bus
    
    // Zero output buffer for synthesizer plugin baseline
    mainOutput.clear();
    
    // Access external input bus 0 when input channels configured
    AudioBuffer<float> externalInput;
    if (getTotalNumInputChannels() > 0) {
        auto inputBuffer = getBusBuffer(buffer, true, 0);  // External input bus
        if (inputBuffer.getNumChannels() > 0) {
            // Transfer external input samples to local AudioBuffer for voice transmission
            externalInput = inputBuffer;
        }
    }
    
    // Execute Colyseus voice mixing and external input processing in synthesizer mode
    if (roomClient->isConnected())
    {
        processRoomCollaboration(mainOutput, &externalInput);
    }
    
    // Calculate RMS level for WebView status display
    updateAudioLevel(mainOutput);
#else
    // Effect plugin: process input channels directly to output channels
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    // Execute Colyseus voice protocol when WebSocket connection active
    if (roomClient->isConnected())
    {
        processRoomCollaboration(buffer);
    }
    // Audio passthrough when no active Colyseus connection
    
    // Calculate RMS level for WebView status display
    updateAudioLevel(buffer);
#endif
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
        
        // Send Colyseus push message via WebSocket protocol
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
        // Convert JUCE AudioBuffer to std::vector<float> for Colyseus WebSocket transmission
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
        
        // Send audio data via Colyseus WebSocket
        roomClient->sendAudioData(audioData);
    }
    
    // Mix incoming audio from other clients with proper buffering
    AudioMessage incomingMessage;
    while (roomClient->getNextAudioMessage(incomingMessage)) {
        int bufferSamples = buffer.getNumSamples();
        int channelCount = buffer.getNumChannels();
        
        // Process audio in channel-interleaved format
        int audioSamples = static_cast<int>(incomingMessage.audioData.size()) / channelCount;
        int samplesToMix = jmin(audioSamples, bufferSamples);
        
        for (int channel = 0; channel < channelCount; ++channel) {
            auto* channelData = buffer.getWritePointer(channel);
            
            for (int sample = 0; sample < samplesToMix; ++sample) {
                if (sample * channelCount + channel < static_cast<int>(incomingMessage.audioData.size())) {
                    channelData[sample] += incomingMessage.audioData[sample * channelCount + channel] * 0.5f;
                }
            }
        }
    }
    
    // Calculate RMS level for WebView status display
    updateAudioLevel(buffer);
}

void FLStreamProcessor::processRoomCollaboration(AudioBuffer<float>& outputBuffer, AudioBuffer<float>* externalInput)
{
    // Check if user is currently talking (push-to-talk)
    auto* talkingParam = parameters.getParameter(PARAM_IS_TALKING);
    bool currentlyTalking = talkingParam->getValue() > 0.5f;
    
    // Handle push-to-talk state changes
    if (currentlyTalking && !isTalking.load()) {
        // Just started talking - send "push" message
        isTalking = true;
        isPushing = true;
        
        // Send Colyseus push message via WebSocket protocol
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
    
    // Mix external input into output when channels configured and push-to-talk active
    if (externalInput && externalInput->getNumChannels() > 0 && isTalking.load()) {
        int samplesToProcess = jmin(outputBuffer.getNumSamples(), externalInput->getNumSamples());
        
        // Copy external input samples to output buffer for voice transmission
        for (int channel = 0; channel < jmin(outputBuffer.getNumChannels(), externalInput->getNumChannels()); ++channel) {
            outputBuffer.copyFrom(channel, 0, *externalInput, channel, 0, samplesToProcess);
        }
        
        // Send the external input audio data when talking
        std::vector<float> audioData;
        int numSamples = samplesToProcess;
        int numChannels = externalInput->getNumChannels();
        
        audioData.resize(numSamples * numChannels);
        
        // Interleave channels from external input
        for (int sample = 0; sample < numSamples; ++sample) {
            for (int channel = 0; channel < numChannels; ++channel) {
                audioData[sample * numChannels + channel] = externalInput->getSample(channel, sample);
            }
        }
        
        // Send audio data via Colyseus WebSocket protocol
        roomClient->sendAudioData(audioData);
    }
    
    // Mix incoming audio from other clients into output with proper buffering
    AudioMessage incomingMessage;
    while (roomClient->getNextAudioMessage(incomingMessage)) {
        int bufferSamples = outputBuffer.getNumSamples();
        int channelCount = outputBuffer.getNumChannels();
        
        // Process audio in channel-interleaved format
        int audioSamples = static_cast<int>(incomingMessage.audioData.size()) / channelCount;
        int samplesToMix = jmin(audioSamples, bufferSamples);
        
        for (int channel = 0; channel < channelCount; ++channel) {
            auto* channelData = outputBuffer.getWritePointer(channel);
            
            for (int sample = 0; sample < samplesToMix; ++sample) {
                if (sample * channelCount + channel < static_cast<int>(incomingMessage.audioData.size())) {
                    channelData[sample] += incomingMessage.audioData[sample * channelCount + channel] * 0.5f;
                }
            }
        }
    }
    
    // Calculate RMS level for WebView status display
    updateAudioLevel(outputBuffer);
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
            const ScopedLock statusGuard(statusMutex);
            connectionStatusText = "Connecting to " + roomName + "...";
            lastLogMessage = "Initiating connection to room: " + roomName;
            lastErrorMessage = ""; // Clear previous errors
        }
        
        return roomClient->joinRoom(roomName, serverUrl);
    }
    else
    {
        connectionState.store(ConnectionState::Error);
        const ScopedLock statusGuard(statusMutex);
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
    const ScopedLock statusGuard(statusMutex);
    return connectionStatusText;
}

String FLStreamProcessor::getLastErrorMessage() const
{
    const ScopedLock statusGuard(statusMutex);
    return lastErrorMessage;
}

String FLStreamProcessor::getLastLogMessage() const
{
    const ScopedLock statusGuard(statusMutex);
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