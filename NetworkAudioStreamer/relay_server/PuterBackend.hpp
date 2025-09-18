#pragma once
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <string>
#include <memory>
#include <atomic>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <mutex>

class PuterBackend {
public:
    PuterBackend();
    ~PuterBackend();
    
    bool initialize();
    void shutdown();
    
    // Room management for FL Studio sessions
    bool createRoom(const std::string& roomCode);
    bool joinRoom(const std::string& roomCode, const std::string& userType);
    bool leaveRoom(const std::string& roomCode, const std::string& userType);
    
    // Audio data exchange
    bool storeAudioData(const std::string& roomCode, const std::string& userType, 
                       const std::vector<float>& audioData, double sampleRate, int channels);
    std::vector<float> retrieveAudioData(const std::string& roomCode, const std::string& userType);
    
    // Heartbeat and connection status
    bool updateHeartbeat(const std::string& roomCode, const std::string& userType);
    bool isRoomActive(const std::string& roomCode);
    
    bool isConnected() const { return connected_.load(); }

private:
    struct HTTPResponse {
        std::string data;
        long responseCode = 0;
    };
    
    CURL* curl_;
    std::atomic<bool> connected_{false};
    std::string baseUrl_ = "https://api.puter.com/kv";
    
    // Rate limiting and caching
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> lastAccess_;
    std::mutex accessMutex_;
    
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, HTTPResponse* response);
    HTTPResponse makeRequest(const std::string& url, const std::string& postData = "");
    
    bool setKV(const std::string& key, const std::string& value);
    std::string getKV(const std::string& key);
    
    std::string encodeAudioPacket(const std::vector<float>& audioData, double sampleRate, int channels);
    std::vector<float> decodeAudioPacket(const std::string& jsonData);
    
    bool canAccess(const std::string& key);
};

// Implementation
PuterBackend::PuterBackend() : curl_(nullptr) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

PuterBackend::~PuterBackend() {
    shutdown();
    curl_global_cleanup();
}

bool PuterBackend::initialize() {
    curl_ = curl_easy_init();
    if (!curl_) return false;
    
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 5L);
    curl_easy_setopt(curl_, CURLOPT_CONNECTTIMEOUT, 2L);
    curl_easy_setopt(curl_, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl_, CURLOPT_USERAGENT, "NetworkAudioRelay/1.0");
    
    // Test connection
    auto response = makeRequest("https://api.puter.com/health");
    connected_.store(response.responseCode == 200);
    
    return connected_.load();
}

void PuterBackend::shutdown() {
    connected_.store(false);
    if (curl_) {
        curl_easy_cleanup(curl_);
        curl_ = nullptr;
    }
}

bool PuterBackend::createRoom(const std::string& roomCode) {
    if (!connected_.load() || roomCode.empty()) return false;
    
    nlohmann::json roomData;
    roomData["created"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    roomData["active"] = true;
    roomData["participants"] = nlohmann::json::object();
    
    return setKV("flroom_" + roomCode, roomData.dump());
}

bool PuterBackend::joinRoom(const std::string& roomCode, const std::string& userType) {
    if (!connected_.load() || roomCode.empty() || userType.empty()) return false;
    
    std::string roomKey = "flroom_" + roomCode;
    std::string roomDataStr = getKV(roomKey);
    
    nlohmann::json roomData;
    if (!roomDataStr.empty()) {
        try {
            roomData = nlohmann::json::parse(roomDataStr);
        } catch (...) {
            return false;
        }
    } else {
        // Create new room if it doesn't exist
        if (!createRoom(roomCode)) return false;
        roomDataStr = getKV(roomKey);
        if (roomDataStr.empty()) return false;
        roomData = nlohmann::json::parse(roomDataStr);
    }
    
    // Add user to room
    roomData["participants"][userType] = {
        {"joined", std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()},
        {"lastSeen", std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()},
        {"active", true}
    };
    
    return setKV(roomKey, roomData.dump());
}

bool PuterBackend::leaveRoom(const std::string& roomCode, const std::string& userType) {
    if (!connected_.load() || roomCode.empty() || userType.empty()) return false;
    
    std::string roomKey = "flroom_" + roomCode;
    std::string roomDataStr = getKV(roomKey);
    
    if (roomDataStr.empty()) return true; // Room doesn't exist, consider it left
    
    try {
        nlohmann::json roomData = nlohmann::json::parse(roomDataStr);
        if (roomData.contains("participants") && roomData["participants"].contains(userType)) {
            roomData["participants"].erase(userType);
            
            // Clean up audio data for this user
            std::string audioKey = "flroom_" + roomCode + "_" + userType + "_audio";
            setKV(audioKey, ""); // Clear audio data
            
            return setKV(roomKey, roomData.dump());
        }
    } catch (...) {
        return false;
    }
    
    return true;
}

bool PuterBackend::storeAudioData(const std::string& roomCode, const std::string& userType, 
                                 const std::vector<float>& audioData, double sampleRate, int channels) {
    if (!connected_.load() || roomCode.empty() || userType.empty() || audioData.empty()) {
        return false;
    }
    
    std::string key = "flroom_" + roomCode + "_" + userType + "_audio";
    
    // Rate limiting - don't update more than 50Hz
    if (!canAccess(key)) return false;
    
    std::string encodedData = encodeAudioPacket(audioData, sampleRate, channels);
    return setKV(key, encodedData);
}

std::vector<float> PuterBackend::retrieveAudioData(const std::string& roomCode, const std::string& userType) {
    if (!connected_.load() || roomCode.empty() || userType.empty()) {
        return {};
    }
    
    std::string key = "flroom_" + roomCode + "_" + userType + "_audio";
    std::string jsonData = getKV(key);
    
    if (jsonData.empty()) return {};
    
    return decodeAudioPacket(jsonData);
}

bool PuterBackend::updateHeartbeat(const std::string& roomCode, const std::string& userType) {
    if (!connected_.load() || roomCode.empty() || userType.empty()) return false;
    
    std::string roomKey = "flroom_" + roomCode;
    std::string roomDataStr = getKV(roomKey);
    
    if (roomDataStr.empty()) return false;
    
    try {
        nlohmann::json roomData = nlohmann::json::parse(roomDataStr);
        if (roomData.contains("participants") && roomData["participants"].contains(userType)) {
            roomData["participants"][userType]["lastSeen"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            
            return setKV(roomKey, roomData.dump());
        }
    } catch (...) {
        return false;
    }
    
    return false;
}

bool PuterBackend::isRoomActive(const std::string& roomCode) {
    if (!connected_.load() || roomCode.empty()) return false;
    
    std::string roomKey = "flroom_" + roomCode;
    std::string roomDataStr = getKV(roomKey);
    
    if (roomDataStr.empty()) return false;
    
    try {
        nlohmann::json roomData = nlohmann::json::parse(roomDataStr);
        return roomData.value("active", false);
    } catch (...) {
        return false;
    }
}

std::string PuterBackend::encodeAudioPacket(const std::vector<float>& audioData, double sampleRate, int channels) {
    nlohmann::json packet;
    packet["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    packet["sampleRate"] = sampleRate;
    packet["channels"] = channels;
    packet["samples"] = audioData.size() / channels;
    
    // Convert to 16-bit integers for efficiency
    std::vector<int> intData;
    intData.reserve(audioData.size());
    for (float sample : audioData) {
        intData.push_back(static_cast<int>(sample * 32767.0f));
    }
    packet["data"] = intData;
    
    return packet.dump();
}

std::vector<float> PuterBackend::decodeAudioPacket(const std::string& jsonData) {
    try {
        nlohmann::json packet = nlohmann::json::parse(jsonData);
        
        if (!packet.contains("data") || !packet["data"].is_array()) {
            return {};
        }
        
        std::vector<float> audioData;
        for (const auto& intSample : packet["data"]) {
            if (intSample.is_number()) {
                float floatSample = static_cast<float>(intSample.get<int>()) / 32767.0f;
                audioData.push_back(floatSample);
            }
        }
        
        return audioData;
    } catch (...) {
        return {};
    }
}

bool PuterBackend::canAccess(const std::string& key) {
    std::lock_guard<std::mutex> lock(accessMutex_);
    auto now = std::chrono::steady_clock::now();
    auto it = lastAccess_.find(key);
    
    if (it == lastAccess_.end() || 
        std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second).count() > 20) {
        lastAccess_[key] = now;
        return true;
    }
    
    return false;
}

bool PuterBackend::setKV(const std::string& key, const std::string& value) {
    if (!curl_ || !connected_.load()) return false;
    
    try {
        nlohmann::json payload;
        payload["key"] = key;
        payload["value"] = value;
        
        std::string jsonData = payload.dump();
        auto response = makeRequest(baseUrl_ + "/set", jsonData);
        
        if (response.responseCode == 200) {
            try {
                auto result = nlohmann::json::parse(response.data);
                return result.value("success", false);
            } catch (...) {
                return false;
            }
        }
        
        return false;
    } catch (...) {
        connected_.store(false);
        return false;
    }
}

std::string PuterBackend::getKV(const std::string& key) {
    if (!curl_ || !connected_.load()) return "";
    
    try {
        nlohmann::json payload;
        payload["key"] = key;
        
        std::string jsonData = payload.dump();
        auto response = makeRequest(baseUrl_ + "/get", jsonData);
        
        if (response.responseCode == 200) {
            try {
                auto result = nlohmann::json::parse(response.data);
                if (result.contains("value")) {
                    return result["value"].get<std::string>();
                }
            } catch (...) {
                return "";
            }
        }
        
        return "";
    } catch (...) {
        connected_.store(false);
        return "";
    }
}

PuterBackend::HTTPResponse PuterBackend::makeRequest(const std::string& url, const std::string& postData) {
    HTTPResponse response;
    if (!curl_) return response;
    
    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response);
    
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers);
    
    if (!postData.empty()) {
        curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, postData.c_str());
        curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE, postData.length());
    } else {
        curl_easy_setopt(curl_, CURLOPT_HTTPGET, 1L);
    }
    
    CURLcode res = curl_easy_perform(curl_);
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &response.responseCode);
    }
    
    curl_slist_free_all(headers);
    return response;
}

size_t PuterBackend::writeCallback(void* contents, size_t size, size_t nmemb, PuterBackend::HTTPResponse* response) {
    size_t totalSize = size * nmemb;
    std::string chunk(static_cast<const char*>(contents), totalSize);
    response->data += chunk;
    return totalSize;
}