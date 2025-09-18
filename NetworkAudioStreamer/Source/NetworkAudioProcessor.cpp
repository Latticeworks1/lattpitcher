#include "NetworkAudioProcessor.h"
#include "NetworkAudioEditor.h"

NetworkAudioProcessor::NetworkAudioProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", AudioChannelSet::stereo(), true)
                                     .withOutput("Output", AudioChannelSet::stereo(), true))
    , parameters_(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    networkManager_ = std::make_unique<NetworkManager>();
    audioMonitor_ = std::make_unique<AudioMonitor>();
    
    // Connect monitor to network manager for web interface
    networkManager_->setAudioProcessor(this);
    
    parameters_.addParameterListener("mode", this);
    parameters_.addParameterListener("port", this);
    parameters_.addParameterListener("sessionId", this);
    parameters_.addParameterListener("userId", this);
}

NetworkAudioProcessor::~NetworkAudioProcessor() {
    networkManager_->stop();
}

void NetworkAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    currentSampleRate_ = sampleRate;
    currentBufferSize_ = samplesPerBlock;
    
    networkBuffer_.setSize(2, samplesPerBlock);
    tempBuffer_.resize(samplesPerBlock * 2);
}

void NetworkAudioProcessor::releaseResources() {
    networkManager_->stop();
}

void NetworkAudioProcessor::processBlock(AudioBuffer<float>& buffer, MidiBuffer&) {
    ScopedNoDenormals noDenormals;
    uint64_t startTime = Time::getHighResolutionTicks();
    
    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();
    
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());
    
    if (networkManager_->getMode() == NetworkManager::Mode::Inactive) {
        if (audioMonitor_) {
            uint64_t processingTime = Time::getHighResolutionTicks() - startTime;
            audioMonitor_->recordProcessTime(processingTime / 1000000.0); // Convert to ms
            
            // Only monitor performance, not audio quality when inactive
            // This prevents false positive glitch detection on silent buffers
        }
        return; // Pass-through when inactive
    }
    
    // Check for buffer size issues
    if (buffer.getNumSamples() == 0) {
        if (audioMonitor_) audioMonitor_->recordGlitch("EmptyProcessBlock", "Process block called with empty buffer");
        return;
    }
    
    if (buffer.getNumSamples() != currentBufferSize_) {
        currentBufferSize_ = buffer.getNumSamples(); // Update current buffer size
        if (audioMonitor_) {
            logToGui("INFO", "Buffer size changed to " + String(buffer.getNumSamples()) + 
                     " samples at " + String(currentSampleRate_, 0) + "Hz");
        }
    }
    
    // Log detailed audio info periodically
    static int debugCounter = 0;
    if (++debugCounter % 1000 == 0) { // Every ~20 seconds at 48kHz/512 samples
        if (audioMonitor_) {
            logToGui("DEBUG", "Audio info: " + String(totalNumInputChannels) + " in, " + 
                     String(totalNumOutputChannels) + " out, " + String(buffer.getNumSamples()) + " samples");
        }
    }
    
    // Monitor input audio only when we're actively processing
    if (totalNumInputChannels > 0 && audioMonitor_) {
        audioMonitor_->analyzeAudioBuffer(buffer, true);
    }
    
    // Send local audio to network
    if (totalNumInputChannels > 0) {
        auto packet = std::make_shared<AudioPacket>(
            sessionId_.load(), userId_.load(), static_cast<uint8_t>(totalNumInputChannels)
        );
        
        packet->header.sequence = sequenceNumber_++;
        packet->header.timestamp = static_cast<uint32_t>(Time::getHighResolutionTicks());
        
        // Convert float samples to bytes (simple conversion for demo)
        size_t sampleCount = buffer.getNumSamples() * totalNumInputChannels;
        packet->audioData.resize(sampleCount * sizeof(float));
        
        float* audioPtr = reinterpret_cast<float*>(packet->audioData.data());
        for (int channel = 0; channel < totalNumInputChannels; ++channel) {
            auto* channelData = buffer.getReadPointer(channel);
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample) {
                audioPtr[sample * totalNumInputChannels + channel] = channelData[sample];
            }
        }
        
        packet->setAudioData(packet->audioData);
        
        if (networkManager_->sendPacket(packet)) {
            if (audioMonitor_) audioMonitor_->recordPacketSent();
        } else {
            if (audioMonitor_) audioMonitor_->recordPacketDropped();
        }
    }
    
    // Receive network audio
    auto receivedPacket = networkManager_->receivePacket();
    if (receivedPacket && receivedPacket->isValid() && totalNumOutputChannels > 0) {
        if (audioMonitor_) audioMonitor_->recordPacketReceived();
        
        int numChannels = std::min(static_cast<int>(receivedPacket->header.channels), totalNumOutputChannels);
        int numSamples = receivedPacket->header.dataSize / (sizeof(float) * numChannels);
        numSamples = std::min(numSamples, buffer.getNumSamples());
        
        if (numSamples != buffer.getNumSamples()) {
            if (audioMonitor_) {
                audioMonitor_->recordGlitch("SampleCountMismatch", 
                    "Received " + String(numSamples) + " samples but buffer expects " + String(buffer.getNumSamples()));
            }
        }
        
        const float* audioPtr = reinterpret_cast<const float*>(receivedPacket->audioData.data());
        
        for (int channel = 0; channel < numChannels; ++channel) {
            auto* outputData = buffer.getWritePointer(channel);
            for (int sample = 0; sample < numSamples; ++sample) {
                outputData[sample] = audioPtr[sample * numChannels + channel];
            }
        }
    } else if (receivedPacket && !receivedPacket->isValid()) {
        if (audioMonitor_) audioMonitor_->recordGlitch("InvalidPacket", "Received invalid network packet");
    }
    
    // Monitor output audio
    if (totalNumOutputChannels > 0 && audioMonitor_) {
        audioMonitor_->analyzeAudioBuffer(buffer, false);
    }
    
    // Record processing time
    if (audioMonitor_) {
        uint64_t processingTime = Time::getHighResolutionTicks() - startTime;
        audioMonitor_->recordProcessTime(processingTime / 1000000.0); // Convert to ms
        
        // Monitor CPU usage (rough estimate)
        double cpuUsage = (processingTime / 1000000.0) / (buffer.getNumSamples() / currentSampleRate_);
        audioMonitor_->recordCpuUsage(cpuUsage);
    }
}

bool NetworkAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    if (layouts.getMainOutputChannelSet() != AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != AudioChannelSet::stereo())
        return false;

    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;

    return true;
}

AudioProcessorEditor* NetworkAudioProcessor::createEditor() {
    return new NetworkAudioEditor(*this);
}

void NetworkAudioProcessor::getStateInformation(MemoryBlock& destData) {
    auto state = parameters_.copyState();
    std::unique_ptr<XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void NetworkAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    std::unique_ptr<XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName(parameters_.state.getType())) {
        parameters_.replaceState(ValueTree::fromXml(*xmlState));
    }
}

void NetworkAudioProcessor::parameterChanged(const String& parameterID, float newValue) {
    if (parameterID == "sessionId") {
        sessionId_ = static_cast<uint32_t>(newValue);
    } else if (parameterID == "userId") {
        userId_ = static_cast<uint32_t>(newValue);
    } else if (parameterID == "mode" || parameterID == "port" || parameterID == "address") {
        updateNetworkConnection();
    }
}

void NetworkAudioProcessor::updateNetworkConnection() {
    int mode = static_cast<int>(*parameters_.getRawParameterValue("mode"));
    int port = static_cast<int>(*parameters_.getRawParameterValue("port"));
    String address = "127.0.0.1"; // Default address - will be configurable via GUI
    
    networkManager_->stop();
    logToGui("INFO", "Stopping network connections");
    
    if (mode == 1) { // Server
        logToGui("INFO", "Starting server on port " + String(port));
        if (networkManager_->startServer(port)) {
            logToGui("INFO", "Server started successfully");
            networkManager_->startWebInterface(port + 100); // Web on port + 100
            logToGui("INFO", "Web interface started on port " + String(port + 100));
        } else {
            logToGui("ERROR", "Failed to start server on port " + String(port));
        }
    } else if (mode == 2) { // Client
        logToGui("INFO", "Connecting to server at " + address + ":" + String(port));
        if (networkManager_->startClient(address, port)) {
            logToGui("INFO", "Connected to server successfully");
        } else {
            logToGui("ERROR", "Failed to connect to server");
        }
    } else {
        logToGui("INFO", "Network mode set to inactive");
    }
}

AudioProcessorValueTreeState::ParameterLayout NetworkAudioProcessor::createParameterLayout() {
    std::vector<std::unique_ptr<RangedAudioParameter>> parameters;
    
    parameters.push_back(std::make_unique<AudioParameterChoice>("mode", "Mode",
        StringArray{"Inactive", "Server", "Client"}, 0));
        
    parameters.push_back(std::make_unique<AudioParameterInt>("port", "Port",
        1024, 65535, 9001));
        
    // Address stored as member variable since JUCE doesn't have AudioParameterString in older versions
        
    parameters.push_back(std::make_unique<AudioParameterInt>("sessionId", "Session ID",
        1, 9999, 1));
        
    parameters.push_back(std::make_unique<AudioParameterInt>("userId", "User ID", 
        1, 9999, 1));
    
    return {parameters.begin(), parameters.end()};
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new NetworkAudioProcessor();
}