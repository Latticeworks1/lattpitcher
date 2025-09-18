#include "FLCollabAudioProcessor.h"
#include "FLCollabEditor.h"

// CloudAPI Implementation  
CloudAPI::CloudAPI()
{
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 3L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 1L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L); // Verify HTTPS certificates
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "FL-Collab/1.0");
        
        // Test connection to Puter worker
        auto response = makeRequest(baseUrl + "/health");
        connected = (response.responseCode == 200);
    }
}

CloudAPI::~CloudAPI()
{
    if (curl) {
        curl_easy_cleanup(curl);
    }
    curl_global_cleanup();
}

bool CloudAPI::setKV(const juce::String& key, const juce::String& value)
{
    if (!curl || !connected) return false;
    
    try {
        juce::var payload = juce::var(new juce::DynamicObject());
        payload.getDynamicObject()->setProperty("key", key);
        payload.getDynamicObject()->setProperty("value", value);
        
        juce::String jsonData = juce::JSON::toString(payload);
        auto response = makeRequest(baseUrl + "/set", jsonData);
        
        if (response.responseCode == 200) {
            auto result = juce::JSON::parse(response.data);
            if (result.isObject() && result.hasProperty("success")) {
                return static_cast<bool>(result.getProperty("success", false));
            }
        }
        
        return false;
        
    } catch (...) {
        connected = false;
        return false;
    }
}

juce::String CloudAPI::getKV(const juce::String& key)
{
    if (!curl || !connected) return {};
    
    try {
        juce::var payload = juce::var(new juce::DynamicObject());
        payload.getDynamicObject()->setProperty("key", key);
        
        juce::String jsonData = juce::JSON::toString(payload);
        auto response = makeRequest(baseUrl + "/get", jsonData);
        
        if (response.responseCode == 200) {
            auto result = juce::JSON::parse(response.data);
            if (result.isObject() && result.hasProperty("value")) {
                return result.getProperty("value", "").toString();
            }
        }
        
        return {};
        
    } catch (...) {
        connected = false;
        return {};
    }
}

CloudAPI::HTTPResponse CloudAPI::makeRequest(const juce::String& url, const juce::String& postData)
{
    HTTPResponse response;
    if (!curl) return response;
    
    curl_easy_setopt(curl, CURLOPT_URL, url.toRawUTF8());
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    
    if (!postData.isEmpty()) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.toRawUTF8());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, postData.length());
    } else {
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    }
    
    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.responseCode);
    }
    
    curl_slist_free_all(headers);
    return response;
}

size_t CloudAPI::writeCallback(void* contents, size_t size, size_t nmemb, CloudAPI::HTTPResponse* response)
{
    size_t totalSize = size * nmemb;
    juce::String chunk(static_cast<const char*>(contents), totalSize);
    response->data += chunk;
    return totalSize;
}

// FLCollabAudioProcessor Implementation
FLCollabAudioProcessor::FLCollabAudioProcessor()
    : AudioProcessor(BusesProperties()
                    .withInput("Input", juce::AudioChannelSet::stereo(), true)
                    .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    cloudAPI = std::make_unique<CloudAPI>();
    
    volumeParam = parameters.getRawParameterValue("volume");
    muteParam = parameters.getRawParameterValue("mute");
    userTypeParam = parameters.getRawParameterValue("userType");
    
    // Set default room code for immediate testing
    currentRoomCode = "fl_studio_collab_room_1";
    
    lastHeartbeat = std::chrono::steady_clock::now();
}

FLCollabAudioProcessor::~FLCollabAudioProcessor()
{
    stopNetworkThread();
}

void FLCollabAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentBufferSize = samplesPerBlock;
    
    if (!currentRoomCode.isEmpty() && cloudAPI && cloudAPI->isConnected()) {
        startNetworkThread();
    }
}

void FLCollabAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    
    if (muteParam->load() > 0.5f || !connected.load() || currentRoomCode.isEmpty()) {
        buffer.clear();
        return;
    }
    
    if (isProducerMode.load()) {
        // Producer: Send FL output, receive vocalist mic
        sendAudioData(buffer);
        
        // Mix received vocal with current buffer
        juce::AudioBuffer<float> receivedBuffer(buffer.getNumChannels(), buffer.getNumSamples());
        if (receiveAudioData(receivedBuffer)) {
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel) {
                buffer.addFrom(channel, 0, receivedBuffer, channel, 0, buffer.getNumSamples(), volumeParam->load());
            }
        }
    } else {
        // Vocalist: Send mic input, receive producer output
        sendAudioData(buffer);
        
        // Replace buffer with producer's output
        if (!receiveAudioData(buffer)) {
            buffer.clear();
        } else {
            buffer.applyGain(volumeParam->load());
        }
    }
}

void FLCollabAudioProcessor::releaseResources()
{
    stopNetworkThread();
}

void FLCollabAudioProcessor::sendAudioData(const juce::AudioBuffer<float>& buffer)
{
    if (!connected.load() || buffer.getNumSamples() == 0) return;
    
    std::vector<float> audioData;
    audioData.reserve(buffer.getNumSamples() * 2);
    
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample) {
        audioData.push_back(buffer.getSample(0, sample));
        audioData.push_back(buffer.getNumChannels() > 1 ? 
                           buffer.getSample(1, sample) : 
                           buffer.getSample(0, sample));
    }
    
    std::lock_guard<std::mutex> lock(sendMutex);
    sendQueue.push(std::move(audioData));
    
    while (sendQueue.size() > 5) {
        sendQueue.pop();
    }
}

bool FLCollabAudioProcessor::receiveAudioData(juce::AudioBuffer<float>& buffer)
{
    std::lock_guard<std::mutex> lock(receiveMutex);
    
    if (receiveQueue.empty()) {
        return false;
    }
    
    auto audioData = receiveQueue.front();
    receiveQueue.pop();
    
    int samplesToCopy = std::min(buffer.getNumSamples(), static_cast<int>(audioData.size() / 2));
    
    for (int sample = 0; sample < samplesToCopy; ++sample) {
        buffer.setSample(0, sample, audioData[sample * 2]);
        if (buffer.getNumChannels() > 1) {
            buffer.setSample(1, sample, audioData[sample * 2 + 1]);
        }
    }
    
    return true;
}

void FLCollabAudioProcessor::setRoomCode(const juce::String& code)
{
    if (currentRoomCode != code) {
        stopNetworkThread();
        currentRoomCode = code;
        
        if (!code.isEmpty() && cloudAPI && cloudAPI->isConnected()) {
            startNetworkThread();
        }
    }
}

void FLCollabAudioProcessor::setUserType(bool isProducer)
{
    isProducerMode.store(isProducer);
}

void FLCollabAudioProcessor::setServerAddress(const juce::String& address, int port)
{
    if (cloudAPI) {
        cloudAPI->updateServerAddress(address, port);
    }
}

void FLCollabAudioProcessor::startNetworkThread()
{
    if (networkRunning.load()) return;
    
    networkRunning.store(true);
    networkThread = std::make_unique<std::thread>(&FLCollabAudioProcessor::networkLoop, this);
}

void FLCollabAudioProcessor::stopNetworkThread()
{
    networkRunning.store(false);
    connected.store(false);
    
    if (networkThread && networkThread->joinable()) {
        networkThread->join();
    }
}

void FLCollabAudioProcessor::networkLoop()
{
    while (networkRunning.load()) {
        try {
            bool anyActivity = false;
            
            // Send queued audio
            {
                std::lock_guard<std::mutex> lock(sendMutex);
                while (!sendQueue.empty()) {
                    auto audioData = sendQueue.front();
                    sendQueue.pop();
                    
                    if (sendToPuter(audioData)) {
                        connected.store(true);
                        anyActivity = true;
                    }
                }
            }
            
            // Receive audio
            auto receivedData = receiveFromPuter();
            if (!receivedData.empty()) {
                std::lock_guard<std::mutex> lock(receiveMutex);
                receiveQueue.push(std::move(receivedData));
                
                while (receiveQueue.size() > 3) {
                    receiveQueue.pop();
                }
                
                connected.store(true);
                anyActivity = true;
            }
            
            // Update heartbeat every 2 seconds
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastHeartbeat).count() > 2000) {
                updateHeartbeat();
                lastHeartbeat = now;
            }
            
            if (!anyActivity) {
                connected.store(false);
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(20)); // 50fps
            
        } catch (...) {
            connected.store(false);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

bool FLCollabAudioProcessor::sendToPuter(const std::vector<float>& audioData)
{
    if (!cloudAPI || currentRoomCode.isEmpty()) return false;
    
    try {
        juce::var packet = juce::var(new juce::DynamicObject());
        packet.getDynamicObject()->setProperty("timestamp", juce::Time::currentTimeMillis());
        packet.getDynamicObject()->setProperty("sampleRate", currentSampleRate);
        packet.getDynamicObject()->setProperty("channels", 2);
        packet.getDynamicObject()->setProperty("samples", static_cast<int>(audioData.size() / 2));
        
        // Convert to 16-bit integers for efficiency
        juce::Array<juce::var> intData;
        for (float sample : audioData) {
            intData.add(static_cast<int>(sample * 32767.0f));
        }
        packet.getDynamicObject()->setProperty("data", intData);
        
        juce::String jsonData = juce::JSON::toString(packet);
        
        juce::String key = "flroom_" + currentRoomCode + "_" + 
                          (isProducerMode.load() ? "producer" : "vocalist") + "_audio";
        
        return cloudAPI->setKV(key, jsonData);
        
    } catch (...) {
        return false;
    }
}

std::vector<float> FLCollabAudioProcessor::receiveFromPuter()
{
    if (!cloudAPI || currentRoomCode.isEmpty()) return {};
    
    try {
        juce::String key = "flroom_" + currentRoomCode + "_" + 
                          (isProducerMode.load() ? "vocalist" : "producer") + "_audio";
        
        juce::String jsonData = cloudAPI->getKV(key);
        if (jsonData.isEmpty()) return {};
        
        auto packet = juce::JSON::parse(jsonData);
        if (!packet.isObject() || !packet.hasProperty("data")) return {};
        
        auto dataArray = packet.getProperty("data", juce::var());
        if (!dataArray.isArray()) return {};
        
        std::vector<float> audioData;
        for (int i = 0; i < dataArray.size(); ++i) {
            int intSample = static_cast<int>(dataArray[i]);
            float floatSample = static_cast<float>(intSample) / 32767.0f;
            audioData.push_back(floatSample);
        }
        
        return audioData;
        
    } catch (...) {
        return {};
    }
}

void FLCollabAudioProcessor::updateHeartbeat()
{
    if (!cloudAPI || currentRoomCode.isEmpty()) return;
    
    try {
        juce::String roomKey = "flroom_" + currentRoomCode;
        juce::String roomData = cloudAPI->getKV(roomKey);
        
        juce::var room;
        if (!roomData.isEmpty()) {
            room = juce::JSON::parse(roomData);
        } else {
            room = juce::var(new juce::DynamicObject());
            room.getDynamicObject()->setProperty("created", juce::Time::currentTimeMillis());
        }
        
        if (!room.isObject()) {
            room = juce::var(new juce::DynamicObject());
        }
        
        juce::String userType = isProducerMode.load() ? "producer" : "vocalist";
        juce::var userInfo = juce::var(new juce::DynamicObject());
        userInfo.getDynamicObject()->setProperty("connected", true);
        userInfo.getDynamicObject()->setProperty("lastSeen", juce::Time::currentTimeMillis());
        userInfo.getDynamicObject()->setProperty("sampleRate", currentSampleRate);
        userInfo.getDynamicObject()->setProperty("bufferSize", currentBufferSize);
        
        room.getDynamicObject()->setProperty(userType, userInfo);
        
        juce::String updatedRoomData = juce::JSON::toString(room);
        cloudAPI->setKV(roomKey, updatedRoomData);
        
    } catch (...) {
        // Ignore heartbeat errors
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout FLCollabAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    
    layout.add(std::make_unique<juce::AudioParameterFloat>("volume",
                                                          "Volume",
                                                          juce::NormalisableRange<float>(0.0f, 2.0f),
                                                          1.0f));
    
    layout.add(std::make_unique<juce::AudioParameterBool>("mute",
                                                         "Mute",
                                                         false));
    
    layout.add(std::make_unique<juce::AudioParameterChoice>("userType",
                                                           "User Type",
                                                           juce::StringArray{"Producer", "Vocalist"},
                                                           0));
    
    return layout;
}

void FLCollabAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty("roomCode", currentRoomCode, nullptr);
    state.setProperty("userType", isProducerMode.load(), nullptr);
    
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void FLCollabAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    
    if (xmlState.get() != nullptr) {
        if (xmlState->hasTagName(parameters.state.getType())) {
            auto state = juce::ValueTree::fromXml(*xmlState);
            parameters.replaceState(state);
            
            if (state.hasProperty("roomCode")) {
                setRoomCode(state.getProperty("roomCode", ""));
            }
            
            if (state.hasProperty("userType")) {
                setUserType(state.getProperty("userType", true));
            }
        }
    }
}

juce::AudioProcessorEditor* FLCollabAudioProcessor::createEditor()
{
    return new FLCollabEditor(*this);
}

// Plugin factory
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FLCollabAudioProcessor();
}