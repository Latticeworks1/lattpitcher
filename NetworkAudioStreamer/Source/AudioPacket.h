#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include <cstdint>

using namespace juce;

struct AudioPacketHeader {
    uint32_t sessionId;
    uint32_t userId; 
    uint32_t sequence;
    uint32_t timestamp;
    uint16_t dataSize;
    uint8_t channels;
    uint8_t reserved;
} __attribute__((packed));

class AudioPacket {
public:
    static constexpr size_t MAX_AUDIO_SIZE = 1920;
    
    AudioPacketHeader header{};
    std::vector<uint8_t> audioData;
    
    AudioPacket() = default;
    
    AudioPacket(uint32_t sessionId, uint32_t userId, uint8_t channels) {
        header.sessionId = sessionId;
        header.userId = userId;
        header.channels = channels;
        header.sequence = 0;
        header.timestamp = 0;
        header.dataSize = 0;
        header.reserved = 0;
    }
    
    void setAudioData(const std::vector<uint8_t>& data) {
        audioData = data;
        header.dataSize = static_cast<uint16_t>(data.size());
    }
    
    bool isValid() const {
        return header.dataSize <= MAX_AUDIO_SIZE && header.dataSize == audioData.size();
    }
    
    size_t totalSize() const {
        return sizeof(AudioPacketHeader) + header.dataSize;
    }
};